"""Windows/macOS bootstrap의 한글 출력, 실행 로그 및 버전 진단."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
import zipfile


STAGES = {
    'SDL-clone': 'SDL3 소스 다운로드', 'SDL-checkout': 'SDL3 소스 커밋 선택',
    'mpv-clone': 'mpv 소스 다운로드', 'mpv-checkout': 'mpv 소스 커밋 선택',
    'mpv-configure': 'mpv 구성: 컴파일러·의존성·기능 검사',
    'mpv-build': 'mpv 컴파일 및 링크', 'mpv-install': 'mpv SDK 설치',
    'mpv-import-library': 'MSVC용 mpv import library 생성',
    'sdl-configure': 'SDL3 구성: 컴파일러·플랫폼 설정',
    'sdl-build': 'SDL3 컴파일 및 링크', 'sdl-install': 'SDL3 SDK 설치',
    'smoke-configure': 'Windows D3D 검증 프로그램 구성',
    'smoke-build': 'Windows D3D 검증 프로그램 빌드',
    'smoke-fixture': 'FFmpeg로 검증용 영상 생성',
    'smoke-run': '설치된 DLL의 D3D 출력·픽셀 읽기 검사',
}


def command_text(command):
    return subprocess.list2cmdline(command) if os.name == 'nt' else shlex.join(command)


class CommandFailure(RuntimeError):
    def __init__(self, command, output, *, log=None, returncode=None, timeout=None):
        self.command = command
        self.output = output
        self.log = str(log) if log else None
        self.returncode = returncode
        self.stage = STAGES.get(Path(log).stem, Path(log).stem) if log else '명령 실행'
        reason = (f'제한 시간 {timeout}초 초과' if timeout is not None else
                  f'종료 코드 {returncode}' if returncode is not None else '프로세스를 시작하지 못함')
        tail = '\n'.join(output.splitlines()[-65:])
        super().__init__(f'{self.stage} 실패 ({reason})\n실행 명령: {command_text(command)}\n'
                         f'전체 로그: {log or "콘솔 출력 참조"}\n원본 오류/출력:\n{tail}')


def run(command, *, env=None, cwd=None, log=None, timeout=None):
    command = [str(arg) for arg in command]
    started = time.monotonic()
    if log is not None:
        label = STAGES.get(log.stem, log.stem)
        print(f'\n[실행] {label}\n  명령: {command_text(command)}\n  전체 로그: {log}\n'
              '  도구의 원본 출력은 로그에 저장합니다. 완료까지 기다려 주세요.', flush=True)
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w', encoding='utf-8') as output:
            output.write(f'단계: {label}\n명령: {command_text(command)}\n\n')
            output.flush()
            try:
                result = subprocess.run(command, cwd=cwd, env=env, stdout=output,
                                        stderr=subprocess.STDOUT, timeout=timeout)
            except subprocess.TimeoutExpired:
                output.flush()
                raise CommandFailure(command, log.read_text(encoding='utf-8', errors='replace'),
                                     log=log, timeout=timeout) from None
            except OSError as error:
                raise CommandFailure(command, str(error), log=log) from error
        if result.returncode:
            raise CommandFailure(command, log.read_text(encoding='utf-8', errors='replace'),
                                 log=log, returncode=result.returncode)
        print(f'[완료] {label} ({time.monotonic() - started:.1f}초)', flush=True)
        return ''
    try:
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b''
        if isinstance(output, bytes):
            output = output.decode('utf-8', errors='replace')
        raise CommandFailure(command, output, timeout=timeout) from None
    except OSError as error:
        raise CommandFailure(command, str(error)) from error
    if result.returncode:
        raise CommandFailure(command, result.stdout + '\n' + result.stderr, returncode=result.returncode)
    return result.stdout.strip()


def environment_key(snapshot):
    return hashlib.sha256(json.dumps(snapshot, sort_keys=True).encode('utf-8')).hexdigest()[:16]


class BuildReport:
    """각 bootstrap 실행이 소유하는 진단 정보. 검사 전용 실행은 파일을 쓰지 않는다."""
    def __init__(self):
        self.stage = '시작'
        self.items = {}
        self.path = None

    def step(self, title, explanation):
        self.stage = title
        print(f'\n[단계] {title}\n  {explanation}', flush=True)

    def record(self, name, version, path, reference=None):
        self.items[name] = {'version': version, 'path': str(path), 'reference': reference}
        print(f'[환경] {name}: {version}\n  경로: {path}', flush=True)
        if reference:
            print(f'  참고 버전: {reference} (일치 여부로 중단하지 않습니다)', flush=True)

    def probe(self, name, command, *, runner=run, env=None, reference=None):
        # Some installed tools do not implement --version. Record that fact and
        # let their actual configure/build command decide compatibility.
        try:
            value = runner(command, env=env).splitlines()[0]
        except (RuntimeError, OSError, IndexError) as error:
            value = 'UNKNOWN (버전 조회 실패)'
            print(f'[안내] {name} 버전을 읽지 못했습니다. 설치된 도구로 계속 시도합니다.\n  {error}', flush=True)
        self.record(name, value, command[0], reference)
        return value

    def finish(self, status, error=None):
        data = {'status': status, 'stage': self.stage, 'environment': self.items}
        if error is not None:
            data['error'] = str(error)
            if isinstance(error, CommandFailure):
                data.update(stage=error.stage, command=error.command, log=error.log,
                            returncode=error.returncode)
            # Only highlight explicit incompatibility statements. A list of
            # installed versions is evidence of inputs, not proof of causation.
            original = error.output if isinstance(error, CommandFailure) else str(error)
            evidence = [line.strip() for line in original.splitlines()
                        if re.search(r'(?i)(version.*(?:requires?|need|at least|unsupported|incompatible|too old|but)|'
                                     r'(?:requires?|need|at least|unsupported|incompatible|too old).*version|'
                                     r'found.*but need|version mismatch|'
                                     r'CMake.*Visual Studio.*generator를 제공하지 않습니다)', line)]
            data['version_diagnostics'] = evidence[:20]
            print(f'\n[FAIL] 실패 단계: {data["stage"]}\n{error}', file=sys.stderr, flush=True)
            print('\n[사용한 도구·의존성 버전]', file=sys.stderr)
            for name, item in self.items.items():
                print(f'  {name}: {item["version"]} | {item["path"]}', file=sys.stderr)
            if evidence:
                print('\n[버전 관련 원본 진단] 아래 항목의 실제 버전과 요구 조건을 비교하세요.\n  ' +
                      '\n  '.join(evidence[:20]), file=sys.stderr)
            else:
                print('\n[원인] 버전 문제를 확정할 진단이 없습니다. 위 실패 명령과 원본 오류를 먼저 확인하세요.',
                      file=sys.stderr)
            print('[다음 조치] 원인을 확인한 후 같은 명령으로 재시도하세요. 도구·의존성은 자동 교체하지 않습니다.',
                  file=sys.stderr, flush=True)
        if self.path is not None:
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                self.path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                print(f'[진단 기록] {self.path}', flush=True)
            except OSError as write_error:
                print(f'[안내] 진단 파일을 저장하지 못했습니다: {write_error}', file=sys.stderr)


def run_main(main):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='replace')
    report = BuildReport()
    try:
        main(report=report)
    except (RuntimeError, OSError, ValueError, zipfile.BadZipFile) as error:
        report.finish('FAIL', error)
        return 1
    report.finish('PASS')
    return 0
