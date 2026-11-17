"""macOS bootstrap에서 누락된 라이브러리만 명시적 동의 후 Homebrew로 설치."""
import json
import os
from pathlib import Path
import re
import shutil
import sys

from bootstrap_common import command_text, run


def installed_pkgconfig_paths(prefix, formulae):
    """keg-only/versioned 설치본도 전역 brew link 변경 없이 탐색한다."""
    paths = []
    for name in formulae:
        roots = [prefix / 'opt' / name]
        roots += sorted((prefix / 'opt').glob(name + '@*'), reverse=True)
        for root in roots:
            for subdir in ['lib/pkgconfig', 'share/pkgconfig']:
                path = root / subdir
                if path.is_dir():
                    paths.append(path)
    return paths


def brew_environment():
    env = os.environ.copy()
    for key in ['CC', 'CXX', 'CPP', 'OBJC', 'OBJCXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS',
                'OBJCFLAGS', 'OBJCXXFLAGS', 'LDFLAGS', 'CPATH', 'LIBRARY_PATH',
                'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET',
                'CMAKE_PREFIX_PATH', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG',
                'PKG_CONFIG_SYSROOT_DIR', 'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH',
                'HOMEBREW_ASK']:
        env.pop(key, None)
    # Do not silently refresh the install plan or clean up existing SDK inputs.
    # Required transitive dependencies can still change; show brew's dry-run.
    for key in ['HOMEBREW_NO_AUTO_UPDATE', 'HOMEBREW_NO_INSTALL_CLEANUP',
                'HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK', 'HOMEBREW_NO_INSTALL_UPGRADE',
                'HOMEBREW_NO_ANALYTICS', 'HOMEBREW_NO_ENV_HINTS']:
        env[key] = '1'
    return env


def formula_info(brew, name, env):
    data = json.loads(run([brew, 'info', '--json=v2', '--formula', 'homebrew/core/' + name], env=env))
    items = data.get('formulae', [])
    if len(items) != 1 or items[0].get('name') != name or items[0].get('tap') != 'homebrew/core':
        raise RuntimeError(f'Homebrew 공식 패키지 정보를 확인할 수 없습니다: {name}')
    return items[0]


def numeric_version(value):
    match = re.fullmatch(r'\d+(?:\.\d+)*(?:_\d+)?', value)
    if not match:
        raise ValueError(f'비교할 수 없는 stable 버전: {value}')
    parts = tuple(int(p) for p in value.split('_')[0].split('.'))
    return parts + (0,) * max(0, 4 - len(parts))


def formula_identity(info):
    # Exclude analytics/installed state: only compare the approved definition.
    return {key: info.get(key) for key in ['name', 'versions', 'revision', 'version_scheme',
            'urls', 'ruby_source_checksum', 'dependencies', 'build_dependencies', 'requirements',
            'variations', 'disabled', 'deprecated']}


def choose_formula(brew, name, minimum, env):
    base = formula_info(brew, name, env)
    candidates = [name] + [item for item in base.get('versioned_formulae', [])
                           if re.fullmatch(re.escape(name) + r'@\d+(?:\.\d+)*', item)]
    reasons = []
    for candidate in candidates:
        try:
            info = base if candidate == name else formula_info(brew, candidate, env)
            version = info.get('versions', {}).get('stable')
            if info.get('disabled'):
                reasons.append(f'{candidate}: 설치 중단된 formula')
                continue
            if not version or numeric_version(version) < numeric_version(minimum):
                reasons.append(f'{candidate}: {version}, 요구 조건 >= {minimum}')
                continue
        except (RuntimeError, ValueError) as error:
            reasons.append(f'{candidate}: {error}')
            continue
        return info
    raise RuntimeError(f'{name}: 요구 조건 >= {minimum}을 만족하는 Homebrew 설치 후보가 없습니다.\n' +
                       '\n'.join(reasons) + '\n별도 tap이나 과거 버전을 자동으로 설치하지 않습니다.')


def install_missing_dependencies(missing, args, recipe, report):
    """Return the base formula names installed; caller must re-probe everything."""
    if args.check_only:
        raise RuntimeError('검사 전용 실행에서는 Homebrew 설치를 요청하거나 실행하지 않습니다.')
    if not sys.stdin.isatty():
        raise RuntimeError('설치 동의를 받을 대화형 터미널이 없습니다. 누락 항목을 준비하거나 터미널에서 재실행하세요.')
    formulae = list(dict.fromkeys(recipe['dependencies'][name]['brew_formula'] for name in missing))
    brew_path = args.deps_prefix / 'bin/brew'
    brew = str(brew_path.resolve()) if brew_path.is_file() else shutil.which('brew')
    if not brew:
        raise RuntimeError('Homebrew가 없습니다. Homebrew 자체는 자동 설치하지 않습니다.')
    env = brew_environment()
    prefix = Path(run([brew, '--prefix'], env=env)).resolve()
    if prefix != args.deps_prefix.resolve():
        raise RuntimeError(f'선택한 의존성 루트 {args.deps_prefix}와 Homebrew 루트 {prefix}가 다릅니다.\n'
                           '해당 루트에 직접 준비하거나 --deps-prefix를 Homebrew 경로로 지정하세요.')
    report.probe('Homebrew', [brew, '--version'], runner=run, env=env)
    installed = {line.split()[0] for line in run([brew, 'list', '--formula', '--versions'], env=env).splitlines()
                 if line.split()}
    plans = []
    for name in formulae:
        # Missing metadata/dylib in an installed formula is a broken input, not
        # permission to upgrade or reinstall somebody's working environment.
        existing = sorted(item for item in installed if item == name or item.startswith(name + '@'))
        if existing:
            raise RuntimeError(f'{", ".join(existing)}는 설치되어 있지만 필요한 pkg-config/dylib를 찾지 못했습니다.\n'
                               '설치 경로와 패키지 상태를 확인하세요. 자동 재설치·업그레이드는 하지 않습니다.')
        plans.append(choose_formula(brew, name, recipe['homebrew_formulas'][name]['minimum'], env))

    report.step('누락 라이브러리 설치 확인', '아래 패키지와 필요한 연관 의존성을 Homebrew로 설치할 수 있습니다.')
    for name, info in zip(formulae, plans):
        version = info['versions']['stable']
        revision = info.get('revision', 0)
        modules = [module for module in missing if recipe['dependencies'][module]['brew_formula'] == name]
        print(f'[설치 예정] {info["name"]} {version}' + (f'_{revision}' if revision else '') +
              f'\n  패키지 요구 조건: >= {recipe["homebrew_formulas"][name]["minimum"]}\n'
              f'  누락 라이브러리: {", ".join(modules)}', flush=True)
        if info.get('deprecated'):
            print(f'  Homebrew 지원 종료 예정: {info.get("deprecation_reason", "공식 formula 정보 참조")}', flush=True)
    names = ['homebrew/core/' + info['name'] for info in plans]
    print('[설치 계획] Homebrew가 계산한 연관 패키지 변경 내역입니다.', flush=True)
    preview = run([brew, 'install', '--dry-run', '--formula', *names], env=env)
    print(preview, flush=True)
    print('필요한 연관 의존성은 추가 설치·갱신될 수 있습니다. 전체 brew upgrade/cleanup은 실행하지 않습니다.\n'
          '거부하면 설치와 SDK 빌드를 진행하지 않습니다.', flush=True)
    try:
        answer = input('위 패키지와 연관 의존성을 설치하고 빌드를 계속할까요? [y/N]: ').strip().casefold()
    except (EOFError, KeyboardInterrupt):
        answer = ''
    if answer not in ['y', 'yes', '예', '네']:
        raise RuntimeError('설치에 동의하지 않아 종료합니다. Homebrew 설치와 SDK 빌드를 실행하지 않았습니다.')

    for info in plans:
        if formula_identity(formula_info(brew, info['name'], env)) != formula_identity(info):
            raise RuntimeError('동의 대기 중 Homebrew 패키지 정의/버전이 바뀌었습니다. 변경된 계획을 확인하도록 재실행하세요.')
    current_preview = run([brew, 'install', '--dry-run', '--formula', *names], env=env)
    if current_preview != preview:
        raise RuntimeError('동의 대기 중 Homebrew 설치 계획이 바뀌었습니다. 변경된 계획을 확인하도록 재실행하세요.')
    # Our explicit consent covers this plan; prevent Homebrew from asking again
    # while its output is redirected to a log (older brew versions ignore this).
    env['HOMEBREW_NO_ASK'] = '1'
    log = args.project.resolve() / 'third_party/mpv_build/build/logs/homebrew-install.log'
    command = [brew, 'install', '--formula', *names]
    report.step('Homebrew 라이브러리 설치', f'승인된 명령: {command_text(command)}\n  설치 로그: {log}')
    run(command, env=env, log=log)
    for info in plans:
        actual = run([brew, 'list', '--versions', '--formula', 'homebrew/core/' + info['name']], env=env).split()
        version = info['versions']['stable']
        if info.get('revision', 0):
            version += '_' + str(info['revision'])
        if version not in actual[1:]:
            raise RuntimeError(f'{info["name"]}: 승인한 설치 버전 {version}을 확인하지 못했습니다. 실제 목록: {actual}')
    print('[완료] Homebrew 설치가 끝났습니다. 도구와 모든 라이브러리 경로·버전·아키텍처를 다시 검사합니다.', flush=True)
    return formulae
