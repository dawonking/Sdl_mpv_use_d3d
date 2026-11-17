"""arm64 SDL3/mpv SDK를 빌드합니다. 누락 라이브러리는 동의 후 Homebrew로 설치합니다."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys

from bootstrap_common import BuildReport, environment_key, run, run_main
from macos_homebrew import installed_pkgconfig_paths, install_missing_dependencies


def require_file(path):
    if not path.is_file():
        raise RuntimeError(f'필수 파일이 없습니다: {path}\n설치 경로와 패키지 내용을 확인하세요.')
    return path


def sha256(path):
    return hashlib.sha256(require_file(path).read_bytes()).hexdigest()


def version_tuple(text):
    match = re.search(r'\d+(?:\.\d+)+', text)
    if not match:
        raise RuntimeError(f'버전 번호를 읽을 수 없습니다: {text!r}')
    parts = tuple(int(part) for part in match.group().split('.'))
    return parts + (0,) * max(0, 4 - len(parts))


def require_version(name, actual, minimum):
    if version_tuple(actual) < version_tuple(minimum):
        raise RuntimeError(f'{name}: 빌드 대상 OS는 {minimum} 이상이어야 합니다. 실제 값: {actual}. '
                           '배포 대상과 SDK/실행 OS 설정을 함께 확인하세요.')


def require_equal(name, actual, expected):
    if actual != expected:
        raise RuntimeError(f'{name}: 필요한 값 {expected!r}, 실제 값 {actual!r}. '
                           '기존 작업을 보존하고 소스·패치·설정을 확인하세요.')


def write_json(path, data):
    # Replace only this script's metadata, never a source file or SDK directory.
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    temporary.replace(path)


def check_architecture(path, tools, env):
    arches = run([tools['lipo'], '-archs', require_file(path)], env=env).split()
    if 'arm64' not in arches:
        raise RuntimeError(f'arm64 아키텍처가 없습니다: {path} ({" ".join(arches)})')
    return arches


def deployment_versions(load_commands):
    # LC_BUILD_VERSION uses minos; older Mach-O files use LC_VERSION_MIN_MACOSX.
    versions = []
    for block in re.split(r'Load command \d+', load_commands):
        if 'cmd LC_BUILD_VERSION\n' in block:
            match = re.search(r'^\s*minos (\S+)', block, re.M)
        elif 'cmd LC_VERSION_MIN_MACOSX\n' in block:
            match = re.search(r'^\s*version (\S+)', block, re.M)
        else:
            continue
        if match:
            versions.append(match.group(1))
    return versions


def check_macho(path, tools, env, target):
    arches = check_architecture(path, tools, env)
    commands = run([tools['otool'], '-arch', 'arm64', '-l', path], env=env)
    for minimum in deployment_versions(commands):
        if version_tuple(minimum) > version_tuple(target):
            raise RuntimeError(f'{path}: macOS {minimum} 이상을 요구하지만 배포 대상은 {target}입니다. '
                               '배포 대상과 의존성의 최소 OS를 함께 확인하세요.')
    return arches, commands


def collect_environment(args, recipe, report=None, *, allow_install=True, installed_formulas=()):
    report = report or BuildReport()
    if platform.system() != 'Darwin' or platform.machine() != recipe['architecture']:
        raise RuntimeError('native macOS arm64 환경이 필요합니다. Rosetta/x86_64 터미널인지 확인하세요.')
    tools = {}
    for name in ['git', 'cmake', 'meson', 'ninja', 'pkg-config', 'xcrun']:
        tool = shutil.which(name)
        if tool is None:
            raise RuntimeError(f'필수 도구 {name}을 찾지 못했습니다. 설치 상태와 PATH를 확인하세요.')
        tools[name] = str(Path(tool).resolve())
    for name in ['clang', 'clang++', 'swiftc', 'lipo', 'otool', 'nm']:
        tools[name] = run([tools['xcrun'], '--sdk', 'macosx', '--find', name])
        require_file(Path(tools[name]))
    sdk_path = run([tools['xcrun'], '--sdk', 'macosx', '--show-sdk-path'])
    sdk_version = run([tools['xcrun'], '--sdk', 'macosx', '--show-sdk-version'])
    target = recipe['deployment_target']
    report.record('macOS SDK', sdk_version, sdk_path)
    report.record('macOS', platform.mac_ver()[0], '현재 실행 OS')
    require_version('macOS SDK', sdk_version, target)
    require_version('macOS host', platform.mac_ver()[0], target)

    # Do not let inherited flags or /usr/local pkg-config files select another SDK.
    env = os.environ.copy()
    for key in ['CC', 'CXX', 'CPP', 'OBJC', 'OBJCXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS',
                'OBJCFLAGS', 'OBJCXXFLAGS', 'LDFLAGS', 'CPATH', 'LIBRARY_PATH',
                'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET',
                'CMAKE_PREFIX_PATH', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR',
                'PKG_CONFIG_SYSROOT_DIR', 'PKG_CONFIG', 'DYLD_LIBRARY_PATH',
                'DYLD_FALLBACK_LIBRARY_PATH']:
        env.pop(key, None)
    prefix = args.deps_prefix.resolve()
    dirs = [p.resolve() for p in args.pkg_config_path]
    dirs += [prefix / subdir for subdir in recipe['pkg_config_subdirs']]
    dirs += installed_pkgconfig_paths(prefix, recipe.get('homebrew_formulas', {}))
    for path in args.pkg_config_path:
        if not path.is_dir():
            raise RuntimeError(f'지정한 pkg-config 디렉터리가 없습니다: {path}')
    dirs = list(dict.fromkeys(str(p) for p in dirs if p.is_dir()))
    if not dirs:
        # A new Homebrew installation may have no .pc files yet. Keep the
        # search path nonempty and isolated while collecting all missing names.
        dirs = [str(prefix / 'lib/pkgconfig')]
    env['PKG_CONFIG_LIBDIR'] = os.pathsep.join(dirs)
    env['PKG_CONFIG_PATH'] = ''
    env['PKG_CONFIG'] = tools['pkg-config']
    env['SDKROOT'] = sdk_path
    env['MACOSX_DEPLOYMENT_TARGET'] = target
    env['CC'] = env['OBJC'] = tools['clang']
    env['CXX'] = env['OBJCXX'] = tools['clang++']
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    bin_dirs = [str(Path(sys.executable).resolve().parent)]
    bin_dirs += [str(Path(p).parent) for p in tools.values()]
    env['PATH'] = os.pathsep.join(dict.fromkeys(bin_dirs + ['/usr/bin', '/bin', '/usr/sbin', '/sbin']))

    versions = {'python': platform.python_version()}
    report.record('python', versions['python'], sys.executable, recipe['reference_tools'].get('python'))
    for name in ['git', 'cmake', 'meson', 'ninja', 'pkg-config', 'clang', 'clang++', 'swiftc']:
        versions[name] = report.probe(name, [tools[name], '--version'], runner=run, env=env,
                                      reference=recipe['reference_tools'].get(name))
    print(f'[검색 경로] pkg-config: {env["PKG_CONFIG_LIBDIR"]}', flush=True)

    dependencies, missing = {}, {}
    for name, spec in recipe['dependencies'].items():
        try:
            version = run([tools['pkg-config'], '--modversion', name], env=env)
        except RuntimeError as error:
            missing[name] = str(error)
            continue
        libdir = Path(run([tools['pkg-config'], '--variable=libdir', name], env=env))
        if not libdir.is_absolute():
            raise RuntimeError(f'{name}: pkg-config의 libdir는 절대 경로여야 합니다. 실제 값: {libdir}')
        library = libdir / spec['dylib']
        if not library.is_file():
            missing[name] = f'pkg-config는 있지만 dylib가 없습니다: {library}'
            continue
        library = library.resolve()
        report.record(name, version, library, 'upstream 요구 참고: >= ' + spec['upstream_minimum'])
        if spec.get('brew_formula') in installed_formulas and version_tuple(version) < version_tuple(spec['upstream_minimum']):
            raise RuntimeError(f'새로 설치한 {spec["brew_formula"]}의 {name}이 요구 조건을 만족하지 않습니다.\n'
                               f'실제 버전: {version}, 필요 버전: >= {spec["upstream_minimum"]}. 빌드를 중단합니다.')
        check_macho(library, tools, env, target)
        pcdir = run([tools['pkg-config'], '--variable=pcfiledir', name], env=env)
        dependencies[name] = {'version': version, 'pc_directory': pcdir,
                              'library': str(library), 'sha256': sha256(library)}
    if missing:
        details = '\n'.join(f'  {name}: {reason}' for name, reason in missing.items())
        print(f'[누락] 필수 라이브러리: {", ".join(missing)}\n'
              f'  검색 경로: {env["PKG_CONFIG_LIBDIR"]}\n{details}', flush=True)
        if args.check_only:
            raise RuntimeError(f'검사 전용 실행: 누락 라이브러리 {", ".join(missing)}. 설치 동의/다운로드/빌드를 진행하지 않습니다.\n'
                               '실제 설치 확인과 빌드를 진행하려면 --check-only 없이 실행하세요.')
        if not allow_install:
            raise RuntimeError('Homebrew 설치 후에도 필요한 라이브러리를 찾지 못했습니다. 반복 설치하지 않고 종료합니다.\n' + details)
        installed = install_missing_dependencies(missing, args, recipe, report)
        # Brew can add/update transitive dependencies and tools. Recompute all
        # paths, versions and hashes; never reuse the pre-install snapshot.
        return collect_environment(args, recipe, report, allow_install=False, installed_formulas=installed)
    snapshot = {'architecture': 'arm64', 'deployment_target': target,
                'sdk_path': sdk_path, 'sdk_version': sdk_version,
                'python': str(Path(sys.executable).resolve()), 'tools': tools,
                'versions': versions, 'pkg_config_directories': dirs,
                'dependencies': dependencies, 'recipe': recipe}
    return tools, env, snapshot


def check_workspace(path, snapshot):
    if path.is_symlink():
        raise RuntimeError(f'생성 디렉터리는 심볼릭 링크일 수 없습니다: {path}')
    marker = path / 'bootstrap-environment.json'
    if marker.exists():
        previous = json.loads(marker.read_text())
        # Tool/dependency changes select a fresh cache, not a fatal mismatch.
        # Keep the source/configuration contract for SDKs managed by this script.
        keys = ['architecture', 'deployment_target', 'mpv', 'sdl3', 'meson_options',
                'required_features', 'sdl_cmake_options']
        for key in keys:
            require_equal(f'SDK 소스/설정 {key} ({path})',
                          snapshot.get('recipe', {}).get(key), previous.get('recipe', {}).get(key))
    elif path.exists() and any(path.iterdir()):
        raise RuntimeError(f'관리 기록이 없는 비어 있지 않은 디렉터리입니다: {path}. 기존 파일을 보존하고 별도로 옮기세요.')


def prepare_source(source, spec, git, env, logs):
    if not source.exists():
        run([git, 'clone', '--filter=blob:none', '--no-checkout', spec['url'], source],
            env=env, log=logs / f'{source.name}-clone.log')
        run([git, '-C', source, 'config', 'core.autocrlf', 'false'], env=env)
        run([git, '-C', source, 'checkout', '--detach', spec['commit']],
            env=env, log=logs / f'{source.name}-checkout.log')
    if not (source / '.git').is_dir():
        raise RuntimeError(f'기존 경로가 Git clone이 아닙니다: {source}. 기존 파일을 보존하고 확인하세요.')
    require_equal(f'{source.name} commit', run([git, '-C', source, 'rev-parse', 'HEAD'], env=env), spec['commit'])
    require_equal(f'{source.name} origin', run([git, '-C', source, 'remote', 'get-url', 'origin'], env=env), spec['url'])


def apply_mpv_patch(source, patch, git, env):
    try:
        run([git, '-C', source, 'apply', '--reverse', '--check', patch], env=env)
        print('[확인] mpv 패치가 이미 적용되어 있습니다. 중복 적용하지 않습니다.', flush=True)
    except RuntimeError:
        run([git, '-C', source, 'apply', '--check', patch], env=env)
        run([git, '-C', source, 'apply', patch], env=env)
        print('[완료] mpv 커스텀 패치를 적용했습니다. 관련 없는 로컬 수정은 보존합니다.', flush=True)


def check_features(config, expected, baseline):
    actual = dict(re.findall(r'^#define (HAVE_\w+) (\d+)$', config.read_text(), re.M))
    for name, value in expected.items():
        require_equal(name, actual.get(name), value)
    if baseline.exists():
        require_equal('동일 환경의 mpv 기능 검사', actual, json.loads(baseline.read_text()))
    else:
        write_json(baseline, actual)
    return actual


def meson_value(value):
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, list):
        return json.dumps(value)
    return str(value)


def check_link_paths(library, linked, rpaths):
    def expand_loader(value):
        if value == '@loader_path':
            return library.parent
        if value.startswith('@loader_path/'):
            return library.parent / value[len('@loader_path/'):]
        return Path(value)

    names = re.findall(r'^\s+(.+?) \(compatibility version ', linked, re.M)
    resolved = {}
    # A dylib's first entry is its own install name, not a dependency.
    for name in names[1:]:
        if name.startswith(('/usr/lib/', '/System/Library/')):
            resolved[name] = 'system (may reside in the dyld shared cache)'
            continue
        if name.startswith('@rpath/'):
            candidates = [expand_loader(rpath) / name[len('@rpath/'):] for rpath in rpaths]
        else:
            candidates = [expand_loader(name)]
        found = next((p for p in candidates if p.is_absolute() and p.is_file()), None)
        if found is None:
            raise RuntimeError(f'{library}에 필요한 직접 dylib 경로를 찾지 못했습니다: {name}')
        resolved[name] = str(found.resolve())
    return resolved


def inspect_library(path, tools, env, target, required_symbols):
    path = require_file(path).resolve()
    arches, commands = check_macho(path, tools, env, target)
    exports = set(run([tools['nm'], '-arch', 'arm64', '-gUj', path], env=env).split())
    missing = {f'_{name}' for name in required_symbols} - exports
    if missing:
        raise RuntimeError(f'{path}에 필수 공개 함수가 없습니다: {sorted(missing)}')
    # Record load commands for later bundle work. This SDK intentionally uses
    # installed dependencies; it is not a relocatable runtime package.
    linked = run([tools['otool'], '-arch', 'arm64', '-L', path], env=env)
    rpaths = []
    for block in re.split(r'Load command \d+', commands):
        if 'cmd LC_RPATH\n' in block:
            match = re.search(r'^\s*path (.+) \(offset \d+\)', block, re.M)
            if match:
                rpaths.append(match.group(1))
    resolved = check_link_paths(path, linked, rpaths)
    return {'path': str(path), 'sha256': sha256(path), 'architectures': arches,
            'linked_libraries': linked, 'rpaths': rpaths, 'direct_dependencies': resolved}


def build_sdks(project, recipe, tools, env, jobs, build, sdk):
    logs = build / 'logs'
    print('[준비] SDL3/mpv 고정 소스를 확인하고 커스텀 패치를 준비합니다.', flush=True)
    source_mpv = project / 'third_party/mpv'
    source_sdl = project / 'third_party/SDL'
    for path, spec in [(source_sdl, recipe['sdl3']), (source_mpv, recipe['mpv'])]:
        prepare_source(path, spec, tools['git'], env, logs)
    apply_mpv_patch(source_mpv, project / 'third_party/mpv_build/patches/0001-custom-mpv.patch', tools['git'], env)

    mpv_build, mpv_sdk = build / 'mpv', sdk / 'mpv'
    options = dict(recipe['meson_options'])
    options['c_args'] = ['-arch', 'arm64']
    options['cpp_args'] = ['-arch', 'arm64']
    options['objc_args'] = ['-arch', 'arm64']
    options['c_link_args'] = ['-arch', 'arm64']
    options['cpp_link_args'] = ['-arch', 'arm64']
    options['objc_link_args'] = ['-arch', 'arm64']
    options['swift-flags'] = f'-target arm64-apple-macosx{recipe["deployment_target"]}'
    setup = [tools['meson'], 'setup', mpv_build, source_mpv,
             f'--prefix={mpv_sdk}', '--libdir=lib', '--wrap-mode=nofallback']
    setup += [f'-D{key}={meson_value(value)}' for key, value in options.items()]
    if (mpv_build / 'meson-private/coredata.dat').exists():
        setup.append('--reconfigure')
    run(setup, env=env, log=logs / 'mpv-configure.log')
    try:
        features = check_features(mpv_build / 'config.h', recipe['required_features'], build / 'mpv-features.json')
    except RuntimeError as error:
        raise RuntimeError(f'{error}\n기능 검사 설정: {mpv_build / "config.h"}\n'
                           f'구성 로그: {logs / "mpv-configure.log"}') from error
    print('[PASS] mpv 필수 기능과 현재 환경의 감지 결과를 확인했습니다.', flush=True)
    run([tools['meson'], 'compile', '-C', mpv_build, '-j', jobs], env=env, log=logs / 'mpv-build.log')
    run([tools['meson'], 'install', '-C', mpv_build, '--no-rebuild'], env=env, log=logs / 'mpv-install.log')

    sdl_build, sdl_sdk = build / 'sdl3', sdk / 'sdl3'
    configure = [tools['cmake'], '-S', source_sdl, '-B', sdl_build, '-G', 'Ninja',
                 f'-DCMAKE_MAKE_PROGRAM={tools["ninja"]}',
                 f'-DCMAKE_C_COMPILER={tools["clang"]}', f'-DCMAKE_CXX_COMPILER={tools["clang++"]}',
                 f'-DCMAKE_OSX_SYSROOT={env["SDKROOT"]}', '-DCMAKE_OSX_ARCHITECTURES=arm64',
                 f'-DCMAKE_OSX_DEPLOYMENT_TARGET={recipe["deployment_target"]}',
                 f'-DCMAKE_INSTALL_PREFIX={sdl_sdk}', '-DCMAKE_INSTALL_LIBDIR=lib']
    configure += [f'-D{key}={value}' for key, value in recipe['sdl_cmake_options'].items()]
    run(configure, env=env, log=logs / 'sdl-configure.log')
    run([tools['cmake'], '--build', sdl_build, '--parallel', jobs], env=env, log=logs / 'sdl-build.log')
    run([tools['cmake'], '--install', sdl_build], env=env, log=logs / 'sdl-install.log')

    print('[검사] 설치된 헤더·패키지 정보·arm64·공개 함수·dylib 경로를 확인합니다.', flush=True)
    for name in ['client.h', 'render.h', 'render_gl.h', 'stream_cb.h']:
        require_file(mpv_sdk / 'include/mpv' / name)
    require_file(mpv_sdk / 'lib/pkgconfig/mpv.pc')
    require_file(mpv_sdk / 'bin/mpv')
    require_file(sdl_sdk / 'include/SDL3/SDL.h')
    require_file(sdl_sdk / 'lib/cmake/SDL3/SDL3Config.cmake')
    artifacts = {
        'mpv': inspect_library(mpv_sdk / 'lib/libmpv.dylib', tools, env, recipe['deployment_target'],
                               ['mpv_create', 'mpv_initialize', 'mpv_render_context_create', 'mpv_render_context_render']),
        'sdl3': inspect_library(sdl_sdk / 'lib/libSDL3.dylib', tools, env, recipe['deployment_target'],
                                ['SDL_Init', 'SDL_CreateWindow', 'SDL_GL_CreateContext']),
    }
    return {'sdk_build': 'PASS', 'native_smoke': 'NOT TESTED', 'application': 'NOT TESTED',
            'relocatable_package': 'NOT TESTED', 'features': features, 'artifacts': artifacts}


def execute_build(project, recipe, tools, env, snapshot, jobs, build, sdk):
    import fcntl  # macOS only; command-line help and static imports remain portable.
    for path in [build, sdk]:
        check_workspace(path, snapshot)
    build.mkdir(parents=True, exist_ok=True)
    with (build / 'bootstrap.lock').open('a') as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('다른 macOS bootstrap이 실행 중입니다. 해당 실행이 끝난 뒤 재시도하세요.') from error
        # Recheck after locking in case another process finished during preflight.
        if (build / 'bootstrap-environment.json').exists():
            check_workspace(build, snapshot)
        elif any(p.name != 'bootstrap.lock' for p in build.iterdir()):
            raise RuntimeError(f'환경 검사 중 빌드 디렉터리 내용이 변경되었습니다: {build}')
        check_workspace(sdk, snapshot)
        for path in [build, sdk]:
            path.mkdir(parents=True, exist_ok=True)
            write_json(path / 'bootstrap-environment.json', snapshot)
        receipt = sdk / 'build-receipt.json'
        if receipt.exists():
            receipt.unlink()  # A failed rebuild must not leave a stale PASS receipt.
        work = build / 'environments' / environment_key(snapshot)
        work.mkdir(parents=True, exist_ok=True)
        write_json(work / 'bootstrap-environment.json', snapshot)
        print(f'[경로] 현재 도구·의존성 조합의 빌드 폴더: {work}\n'
              '  버전이나 경로가 바뀌면 별도 캐시를 사용하며 이전 폴더는 보존합니다.', flush=True)
        result = build_sdks(project, recipe, tools, env, jobs, work, sdk)
        result['build_directory'] = str(work)
        result['inputs'] = snapshot
        write_json(receipt, result)
    print(f'[PASS] SDK 빌드와 산출물 정적 검사를 완료했습니다.\n  SDK: {sdk}\n  검증 기록: {receipt}', flush=True)
    print('[NOT TESTED] OpenGL 재생·VideoToolbox 디코딩·앱·배포 패키지는 별도 검증이 필요합니다.', flush=True)


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError('1 이상의 정수를 입력하세요')
    return number


def main(argv=None, report=None):
    report = report or BuildReport()
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser._optionals.title = '옵션'
    parser.add_argument('-h', '--help', action='help', help='사용 방법을 표시하고 종료')
    parser.add_argument('--project', required=True, type=Path, help='프로젝트 루트')
    parser.add_argument('--deps-prefix', type=Path, default=Path('/opt/homebrew'),
                        help='설치된 arm64 의존성의 루트 (기본값: /opt/homebrew)')
    parser.add_argument('--pkg-config-path', action='append', type=Path, default=[],
                        help='우선 탐색할 pkg-config 디렉터리; 여러 번 지정 가능')
    parser.add_argument('--jobs', type=positive_int, default=min(os.cpu_count() or 1, 8), help='병렬 빌드 작업 수')
    parser.add_argument('--check-only', action='store_true', help='설치 상태만 확인; 다운로드·빌드·쓰기 없음')
    args = parser.parse_args(argv)
    project = args.project.resolve()
    if not args.check_only:
        report.path = project / 'third_party/mpv_build/build/logs/macos-bootstrap-report.json'
    report.step('1. 입력·환경 검사', '설치된 도구·의존성 버전으로 시도합니다. 참고 버전 차이는 중단 조건이 아닙니다.')
    recipe_path = require_file(project / 'third_party/mpv_build/macos-lock.json')
    recipe = json.loads(recipe_path.read_text(encoding='utf-8'))
    require_equal('recipe schema', recipe['schema_version'], 1)
    patch = project / 'third_party/mpv_build/patches/0001-custom-mpv.patch'
    require_equal('mpv patch SHA256', sha256(patch), recipe['mpv']['patch_sha256'])
    build = project / 'third_party/mpv_build/build/mac_arm64'
    sdk = project / 'third_party/prebuilt/mac_arm64'
    # Reject incompatible/unmanaged SDKs before offering system package changes.
    for path in [build, sdk]:
        check_workspace(path, {'recipe': recipe})
    tools, env, snapshot = collect_environment(args, recipe, report=report)
    for path in [build, sdk]:
        check_workspace(path, snapshot)
    print('[PASS] 필수 입력·아키텍처·배포 대상·패치를 확인했습니다. 버전 호환성은 실제 구성·빌드에서 판정합니다.', flush=True)
    if args.check_only:
        print('[완료] 검사 전용 실행입니다. 소스 다운로드·패치 적용·빌드·파일 쓰기는 하지 않았습니다.', flush=True)
        return
    report.step('2. 소스 준비·SDK 빌드·검사', 'mpv와 SDL3를 순서대로 빌드합니다. 각 명령과 로그 경로를 아래에 표시합니다.')
    execute_build(project, recipe, tools, env, snapshot, args.jobs, build, sdk)


if __name__ == '__main__':
    sys.exit(run_main(main))
