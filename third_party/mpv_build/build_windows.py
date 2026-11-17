"""설치된 Windows 도구·의존성으로 고정 소스의 SDL3/mpv SDK를 빌드합니다."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import zipfile

from bootstrap_common import BuildReport, environment_key, run, run_main


def require_file(path):
    if not path.is_file():
        raise RuntimeError(f'필수 파일이 없습니다: {path}\n설치 경로와 패키지 내용을 확인하세요.')
    return path


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_equal(label, actual, expected):
    if actual != expected:
        raise RuntimeError(f'{label}: 필요한 값 {expected!r}, 실제 값 {actual!r}. '
                           '소스·패치·대상 플랫폼을 확인하세요. 기존 파일은 자동 복원하지 않습니다.')


def apply_mpv_patch(source, patch, git, env):
    # Check only the patch hunks; unrelated local changes are allowed.
    try:
        run([git, '-C', source, 'apply', '--reverse', '--check', patch], env=env)
        print('[확인] mpv 패치가 이미 적용되어 있습니다. 중복 적용하지 않습니다.', flush=True)
    except RuntimeError:
        # A partial patch or conflicting edit must fail before writing any files.
        run([git, '-C', source, 'apply', '--check', patch], env=env)
        run([git, '-C', source, 'apply', patch], env=env)
        print('[완료] mpv 커스텀 패치를 적용했습니다. 관련 없는 로컬 수정은 보존합니다.', flush=True)


def prepare_source(source, spec, git, env, logs):
    if not source.exists():
        run([git, 'clone', '--filter=blob:none', '--no-checkout', spec['url'], source],
            env=env, log=logs / f'{source.name}-clone.log')
        run([git, '-C', source, 'config', 'core.autocrlf', 'false'], env=env)
        run([git, '-C', source, 'checkout', '--detach', spec['commit']], env=env,
            log=logs / f'{source.name}-checkout.log')
    if not (source / '.git').is_dir():
        raise RuntimeError(f'기존 경로가 Git clone이 아닙니다: {source}. 파일을 보존하고 경로를 확인하세요.')
    require_equal(f'{source.name} commit', run([git, '-C', source, 'rev-parse', 'HEAD'], env=env), spec['commit'])
    require_equal(f'{source.name} origin', run([git, '-C', source, 'remote', 'get-url', 'origin'], env=env), spec['url'])


def install_webview(third, packages):
    # The complete fixed NuGet archives are the source, including ignored .lib files.
    for relative in packages:
        package = third / relative
        with zipfile.ZipFile(package) as archive:
            for member in archive.infolist():
                path = (package.parent / member.filename).resolve()
                if not path.is_relative_to(package.parent.resolve()):
                    raise RuntimeError(f'패키지 디렉터리 밖을 가리키는 항목입니다: {member.filename}')
            archive.extractall(package.parent)


def copy_runtime(dlls, runtime, mingw_bin, objdump, env):
    runtime.mkdir(parents=True, exist_ok=True)
    pending = list(dlls)
    visited = set()
    copied = {}
    system = Path(os.environ['SystemRoot']) / 'System32'
    while pending:
        dll = pending.pop()
        name = dll.name.lower()
        if name in visited:
            continue
        visited.add(name)
        require_file(dll)
        dependencies = re.findall(r'DLL Name:\s*(\S+)', run([objdump, '-p', dll], env=env))
        for dependency in dependencies:
            if dependency.lower().startswith(('api-ms-', 'ext-ms-')) or (system / dependency).is_file():
                continue
            # Resolve only from the installed SDK or the pinned MINGW64 environment.
            candidates = [dll.parent / dependency, mingw_bin / dependency]
            found = next((candidate for candidate in candidates if candidate.is_file()), None)
            if found is None:
                raise RuntimeError(f'{dll}에 필요한 런타임 DLL을 찾지 못했습니다: {dependency}')
            pending.append(found)
        destination = runtime / dll.name
        if dll.resolve() != destination.resolve():
            shutil.copy2(dll, destination)
        copied[dll.name] = sha256(destination)
    return copied


def main(argv=None, report=None):
    report = report or BuildReport()
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser._optionals.title = '옵션'
    parser.add_argument('-h', '--help', action='help', help='사용 방법을 표시하고 종료')
    parser.add_argument('--project', required=True, type=Path, help='프로젝트 루트')
    parser.add_argument('--msys', required=True, type=Path, help='설치된 MSYS2 루트')
    parser.add_argument('--visual-studio', required=True, type=Path, help='설치된 Visual Studio 경로')
    parser.add_argument('--visual-studio-version', required=True, help='vswhere가 확인한 Visual Studio 버전')
    parser.add_argument('--cmake', required=True, type=Path, help='설치된 cmake.exe 경로')
    parser.add_argument('--git', required=True, type=Path, help='설치된 git.exe 경로')
    parser.add_argument('--check-only', action='store_true', help='설치 상태만 확인; 다운로드·빌드·쓰기 없음')
    args = parser.parse_args(argv)
    if sys.platform != 'win32':
        raise RuntimeError('Windows x64에서 bootstrap.ps1을 실행하세요. macOS는 bootstrap.sh를 사용합니다.')
    project = args.project.resolve()
    third = project / 'third_party'
    recipe = third / 'mpv_build'
    if not args.check_only:
        report.path = recipe / 'build/logs/windows-bootstrap-report.json'
    report.step('1. 도구와 의존성 확인', '설치된 버전으로 시도합니다. 참고 버전 차이는 중단 조건이 아닙니다.')
    lock = json.loads(require_file(recipe / 'windows-lock.json').read_text(encoding='utf-8'))
    patch = require_file(recipe / 'patches/0001-custom-mpv.patch')
    require_equal('mpv patch SHA256', sha256(patch), lock['mpv']['patch_sha256'])
    mingw = args.msys.resolve() / 'mingw64'
    tools = {name: require_file(mingw / 'bin' / filename) for name, filename in {
        'cc': 'cc.exe', 'cxx': 'c++.exe', 'meson': 'meson.exe', 'ninja': 'ninja.exe',
        'pkg-config': 'pkg-config.exe', 'objdump': 'objdump.exe', 'python': 'python.exe',
        'windres': 'windres.exe', 'ffmpeg': 'ffmpeg.exe'}.items()}
    require_file(args.git)
    require_file(args.cmake)
    vc_version = require_file(args.visual_studio / 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt').read_text().strip()
    vc_bin = args.visual_studio / 'VC/Tools/MSVC' / vc_version / 'bin/Hostx64/x64'
    lib = require_file(vc_bin / 'lib.exe')
    dumpbin = require_file(vc_bin / 'dumpbin.exe')
    require_file(vc_bin / 'cl.exe')
    require_file(args.visual_studio / 'MSBuild/Current/Bin/MSBuild.exe')
    # Do not inherit paths that could select a different FFmpeg/pkg-config/toolchain.
    env = os.environ.copy()
    for key in ['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'PKG_CONFIG_PATH',
                'PKG_CONFIG_SYSROOT_DIR', 'CMAKE_PREFIX_PATH']:
        env.pop(key, None)
    env['PATH'] = os.pathsep.join([str(mingw / 'bin'), str(args.git.parent),
                                  str(args.msys / 'usr/bin'), str(vc_bin), env.get('PATH', '')])
    env['PKG_CONFIG_LIBDIR'] = os.pathsep.join([str(mingw / 'lib/pkgconfig'), str(mingw / 'share/pkgconfig')])
    env['CC'] = str(tools['cc'])
    env['CXX'] = str(tools['cxx'])
    versions = {}
    for name, command in {
        'gcc': [tools['cc'], '-dumpfullversion'], 'meson': [tools['meson'], '--version'],
        'ninja': [tools['ninja'], '--version'], 'python': [tools['python'], '--version'],
        'pkg-config': [tools['pkg-config'], '--version'], 'cmake': [args.cmake, '--version'],
        'git': [args.git, '--version'], 'ffmpeg': [tools['ffmpeg'], '-version'],
        'cxx': [tools['cxx'], '-dumpfullversion'], 'objdump': [tools['objdump'], '--version'],
        'windres': [tools['windres'], '--version'],
    }.items():
        versions[name] = report.probe(name, command, runner=run, env=env,
                                     reference=lock['reference_tools'].get(name))
    report.record('Visual Studio', args.visual_studio_version, args.visual_studio)
    report.record('MSVC', vc_version, vc_bin)
    report.record('bootstrap Python', sys.version.split()[0], sys.executable)
    require_equal('MinGW target', run([tools['cc'], '-dumpmachine'], env=env), 'x86_64-w64-mingw32')
    dependencies = {}
    for name, reference in lock['reference_dependencies'].items():
        try:
            version = run([tools['pkg-config'], '--modversion', name], env=env)
        except RuntimeError as error:
            raise RuntimeError(f'필수 의존성 {name}을 찾지 못했습니다.\n'
                               f'검색 경로: {env["PKG_CONFIG_LIBDIR"]}\n{error}') from error
        pcdir = run([tools['pkg-config'], '--variable=pcfiledir', name], env=env)
        dependencies[name] = {'version': version, 'pc_directory': pcdir}
        report.record(name, version, pcdir, reference)
    capabilities = json.loads(run([args.cmake, '-E', 'capabilities']))
    prefix = f'Visual Studio {args.visual_studio_version.split(".")[0]} '
    generator = next((item['name'] for item in capabilities['generators']
                      if item['name'].startswith(prefix)), None)
    if generator is None:
        raise RuntimeError(f'설치된 CMake {versions["cmake"]}가 Visual Studio '
                           f'{args.visual_studio_version}용 generator를 제공하지 않습니다.\n'
                           '두 도구가 함께 지원되는 조합인지 확인하세요.')
    print(f'[확인] SDL3/MSVC 빌드 generator: {generator}', flush=True)
    report.step('2. 입력 파일 검사', '공유 mpv 패치·NuGet 체크섬과 앱의 필수 소스를 확인합니다.')
    for relative, expected in lock['webview_packages'].items():
        require_equal(relative, sha256(require_file(third / relative)), expected)
    # These bundled source libraries are required by the existing application CMake.
    for relative in ['imgui/imgui.cpp', 'imgui/implot.cpp', 'json/include/nlohmann/json.hpp',
                     'stb_image/stb_image.h', 'nanosvg/src/nanosvg.h']:
        require_file(third / relative)
    print('[PASS] 필수 입력을 확인했습니다. 버전 호환성은 실제 구성·빌드에서 판정합니다.', flush=True)
    if args.check_only:
        print('[완료] 검사 전용 실행입니다. 소스 다운로드·패치 적용·빌드·파일 쓰기는 하지 않았습니다.', flush=True)
        return

    snapshot = {'tools': versions, 'tool_paths': {k: str(v) for k, v in tools.items()},
                'cmake': str(args.cmake), 'git': str(args.git), 'msvc': vc_version,
                'visual_studio': str(args.visual_studio), 'visual_studio_version': args.visual_studio_version,
                'dependencies': dependencies, 'recipe': lock}
    work = recipe / 'build/windows-x64/environments' / environment_key(snapshot)
    logs = work / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    (work / 'bootstrap-environment.json').write_text(json.dumps(snapshot, indent=2) + '\n', encoding='utf-8')
    print(f'[경로] 현재 도구·의존성 조합의 빌드 폴더: {work}\n'
          '  버전이나 경로가 바뀌면 별도 캐시를 사용하며 이전 빌드 폴더는 보존합니다.', flush=True)
    sdk = third / 'prebuilt/windows-x64'
    receipt_path = sdk / 'bootstrap-receipt.json'
    if receipt_path.exists():
        receipt_path.unlink()
    report.step('3. SDK 입력 준비', 'NuGet을 풀고 SDL3/mpv 소스를 준비합니다. 기존 로컬 수정은 보존합니다.')
    install_webview(third, lock['webview_packages'])
    for relative in ['WebView2/MSWebView2/build/native/include/WebView2.h',
                     'WebView2/MSWebView2/build/native/x64/WebView2LoaderStatic.lib',
                     'WebView2/MSImplementLibrary/include/wil/com.h']:
        require_file(third / relative)
    sdl_source = third / 'SDL'
    mpv_source = third / 'mpv'
    for source, spec in [(sdl_source, lock['sdl3']), (mpv_source, lock['mpv'])]:
        prepare_source(source, spec, args.git, env, logs)
    apply_mpv_patch(mpv_source, patch, args.git, env)
    print('[PASS] 소스 커밋과 mpv 패치를 확인했습니다.', flush=True)

    mpv_sdk = sdk / 'mpv'
    mpv_build = work / 'mpv'
    report.step('4. mpv SDK 빌드', 'Meson 구성 → 기능 확인 → 컴파일 → 설치 → MSVC 링크 파일 생성 순서입니다.')
    meson_args = []
    for key, value in lock['meson_options'].items():
        if isinstance(value, bool):
            value = str(value).lower()
        elif isinstance(value, list):
            value = json.dumps(value)
        meson_args.append(f'-D{key}={value}')
    setup = [tools['meson'], 'setup', mpv_build, mpv_source,
             f'--prefix={mpv_sdk.as_posix()}', *meson_args]
    if (mpv_build / 'meson-private/coredata.dat').exists():
        setup.append('--reconfigure')
    run(setup, env=env, log=logs / 'mpv-configure.log')
    config = (mpv_build / 'config.h').read_text(encoding='utf-8')
    actual_features = dict(re.findall(r'^#define (HAVE_\w+) (\d+)$', config, re.M))
    differences = {key: (value, actual_features.get(key)) for key, value in lock['features'].items()
                   if actual_features.get(key) != value}
    if differences:
        raise RuntimeError(f'mpv 기능 구성이 기준과 다릅니다 (기준값, 실제값): {differences}\n'
                           f'버전만으로 원인을 단정할 수 없습니다. 기능 검사 로그: {logs / "mpv-configure.log"}')
    print('[PASS] mpv 기능 구성이 기준과 일치합니다.', flush=True)
    run([tools['meson'], 'compile', '-C', mpv_build], env=env, log=logs / 'mpv-build.log')
    run([tools['meson'], 'install', '-C', mpv_build, '--no-rebuild'], env=env, log=logs / 'mpv-install.log')
    mpv_dll = require_file(mpv_sdk / 'bin/libmpv-2.dll')
    for name in ['client.h', 'render.h', 'render_gl.h', 'render_d3.h', 'stream_cb.h']:
        require_file(mpv_sdk / 'include/mpv' / name)
    exports = run([dumpbin, '/nologo', '/exports', mpv_dll], env=env)
    names = re.findall(r'^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(mpv_\w+)\s*$', exports, re.M)
    if not {'mpv_create', 'mpv_render_context_create', 'mpv_render_context_render'}.issubset(names):
        raise RuntimeError('필수 mpv 공개 함수가 없어 MSVC import library를 만들 수 없습니다.')
    definition = mpv_build / 'mpv.def'
    definition.write_text('LIBRARY libmpv-2.dll\nEXPORTS\n' + '\n'.join(names) + '\n', encoding='ascii')
    (mpv_sdk / 'lib').mkdir(exist_ok=True)
    run([lib, '/nologo', '/machine:x64', f'/def:{definition}', f'/out:{mpv_sdk / "lib/mpv.lib"}'],
        env=env, log=logs / 'mpv-import-library.log')

    report.step('5. SDL3 SDK 빌드', 'Visual Studio/MSVC로 Release shared 라이브러리를 구성·빌드·설치합니다.')
    sdl_build = work / 'sdl3'
    sdl_sdk = sdk / 'sdl3'
    # The application is MSVC; keep MinGW environment variables out of SDL's CMake.
    msvc_env = os.environ.copy()
    for key in ['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS']:
        msvc_env.pop(key, None)
    run([args.cmake, '-S', sdl_source, '-B', sdl_build, '-G', generator, '-A', 'x64',
         f'-DCMAKE_GENERATOR_INSTANCE={args.visual_studio}', f'-DCMAKE_INSTALL_PREFIX={sdl_sdk.as_posix()}',
         '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST_LIBRARY=OFF', '-DSDL_TESTS=OFF'],
        env=msvc_env, log=logs / 'sdl-configure.log')
    run([args.cmake, '--build', sdl_build, '--config', 'Release', '--parallel'],
        env=msvc_env, log=logs / 'sdl-build.log')
    run([args.cmake, '--install', sdl_build, '--config', 'Release'],
        env=msvc_env, log=logs / 'sdl-install.log')
    require_file(sdl_sdk / 'lib/SDL3.lib')
    require_file(sdl_sdk / 'include/SDL3/SDL.h')
    runtime = third / 'bin'
    report.step('6. 런타임 DLL 수집', 'SDK 및 MINGW64에서 비시스템 DLL 의존성을 재귀적으로 찾아 복사합니다.')
    copied = copy_runtime([mpv_dll, require_file(sdl_sdk / 'bin/SDL3.dll')], runtime,
                          mingw / 'bin', tools['objdump'], env)

    report.step('7. 설치 결과 검증', 'MSVC 링크·DLL 로딩·D3D 프레임 검사를 실행합니다. 실제 Windows 그래픽 세션이 필요합니다.')
    test_build = work / 'd3d-smoke'
    run([args.cmake, '-S', recipe / 'tests', '-B', test_build, '-G', generator, '-A', 'x64',
         f'-DCMAKE_GENERATOR_INSTANCE={args.visual_studio}', f'-DSDK_ROOT={sdk.as_posix()}',
         f'-DRUNTIME_ROOT={runtime.as_posix()}'], env=msvc_env, log=logs / 'smoke-configure.log')
    run([args.cmake, '--build', test_build, '--config', 'Debug', '--parallel'],
        env=msvc_env, log=logs / 'smoke-build.log')
    fixture = test_build / 'fixture.mkv'
    run([tools['ffmpeg'], '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
         'testsrc2=size=320x180:rate=30', '-t', '3', '-c:v', 'ffv1', fixture], env=env,
        log=logs / 'smoke-fixture.log')
    # Do not allow the developer's MINGW64 PATH to hide missing deployed DLLs.
    test_env = os.environ.copy()
    test_env['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])
    run([test_build / 'Debug/mpv_d3d_smoke.exe', fixture], env=test_env,
        log=logs / 'smoke-run.log', timeout=45)
    receipt = {'mpv': lock['mpv']['commit'], 'sdl3': lock['sdl3']['commit'],
               'patch_sha256': lock['mpv']['patch_sha256'], 'tools': versions,
               'msvc': vc_version, 'dependencies': dependencies, 'runtime_sha256': copied,
               'build_directory': str(work), 'inputs': snapshot, 'd3d_smoke': 'PASS'}
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'[PASS] SDL3/mpv SDK·MSVC 링크·배치 DLL·D3D 프레임 검사를 완료했습니다.\n'
          f'  SDK: {sdk}\n  런타임 DLL: {runtime}\n  검증 기록: {receipt_path}', flush=True)
    print('[다음 작업] 앱 빌드가 필요하면 별도로 실행하세요.\n'
          '  cmake --preset windows-msvc-debug\n  cmake --build --preset windows-msvc-debug', flush=True)


if __name__ == '__main__':
    sys.exit(run_main(main))
