# Windows SDL3/mpv SDK 빌드 안내

이 스크립트는 기존 개발 환경에서 SDL3와 커스텀 mpv를 빌드하고, 앱에 필요한
헤더·라이브러리·런타임 DLL을 프로젝트 안에 준비합니다. 앱 자체는 별도로 빌드합니다.
macOS는 [Mac 안내](README_MACOS.md)를 참고하세요.

## 처음 실행하는 순서

1. 아래 도구와 의존성이 설치되어 있는지 확인합니다.
2. PowerShell에서 검사 전용 명령을 실행합니다.
3. 누락된 항목이 있으면 표시된 설치/검색 경로를 확인합니다.
4. 검사 통과 후 빌드 명령을 실행합니다. 최초 실행에는 공식 저장소에 접근할 네트워크가 필요합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\bootstrap.ps1 -CheckOnly
powershell -ExecutionPolicy Bypass -File .\bootstrap.ps1
```

실행 위치가 프로젝트 루트가 아니면 스크립트의 전체 경로를 지정할 수 있습니다.
MSYS2가 다른 위치에 설치되어 있으면 두 명령에 `-MsysRoot D:\msys64`를 추가합니다.
검사 전용 실행은 소스를 받거나 패치를 적용하지 않고, 빌드·설치·진단 파일 쓰기도 하지 않습니다.
검사 통과는 실제 컴파일이나 재생 성공을 의미하지 않습니다.

## 필요한 설치 항목

| 항목 | 역할과 확인 사항 |
|---|---|
| Windows x64 / Visual Studio C++ 데스크톱 도구 / Windows SDK | SDL3와 D3D 검증 프로그램을 MSVC로 빌드합니다. |
| Git / CMake | PATH에 등록되어 있어야 합니다. 설치된 CMake가 설치된 Visual Studio의 generator를 지원해야 합니다. |
| MSYS2 MINGW64 | Python, GCC/G++, Meson, Ninja, pkg-config, binutils, FFmpeg 실행 파일과 mpv 의존성이 필요합니다. UCRT64/CLANG64와 혼용하지 않습니다. |
| FFmpeg·libass·libplacebo 등 | `windows-lock.json`의 `reference_dependencies` 항목에 해당하는 pkg-config 패키지가 MINGW64에 있어야 합니다. mpv 내부 의존성이며 이 스크립트가 빌드하지 않습니다. |
| WebView2/WIL NuGet 파일 | `third_party/WebView2` 아래의 지정된 두 패키지와 SHA256이 필요합니다. |
| 저장소의 imgui/implot/json/stb_image/nanosvg | 기존 앱 구성에 필요한 소스입니다. |

스크립트는 도구·의존성을 자동 설치하거나 업그레이드하지 않습니다.
`bootstrap.ps1`은 설치된 Visual Studio를 조회하고, CMake가 제공하는 generator 중
그 Visual Studio에 대응하는 것을 선택합니다. 앱의 기존 VS 2022 프리셋은 변경하지 않습니다.

## 버전 정책

**설치된 버전으로 빌드를 시도합니다.** `reference_tools`와 `reference_dependencies`는
이전 환경을 설명하는 참고값이며, 값이 다르다는 이유로 중단하지 않습니다.
버전 조회를 지원하지 않는 도구는 UNKNOWN으로 기록하고 실제 실행에서 확인합니다.

소스가 요구하는 Meson/API 등의 호환성은 구성·컴파일·링크 과정에서 판정됩니다.
설치되지 않은 필수 항목, 잘못된 아키텍처, 소스/패치/패키지 불일치 및 기능/검증 실패는
계속 오류로 처리합니다. 버전 허용은 기능 검사나 검증 프로그램을 생략한다는 뜻이 아닙니다.

다음 항목은 계속 고정합니다.

- mpv 커밋: `b51131dff917fbccbedc62a8719bd976a05b5160`.
- SDL3 커밋: `6243fa2ffd8de2cf310ec4851eb0d9faef83b2fa`.
- 공유 패치 `patches/0001-custom-mpv.patch`의 SHA256 및 NuGet SHA256.
- mpv Debug/shared, libmpv/cplayer 활성화, GPL 비활성화, GL/D3D11 설정과 기록된 HAVE_* 기능 결과.
- SDL3 MSVC x64 Release/shared 설정.

## 화면에 표시되는 단계

1. **도구와 의존성 확인**: 실제 버전·실행 파일 경로·pkg-config 위치와 참고 버전을 표시합니다.
2. **입력 파일 검사**: 패치·NuGet 체크섬과 필수 앱 소스를 확인합니다.
3. **SDK 입력 준비**: NuGet을 풀고 고정 커밋의 SDL3/mpv를 준비하며 커스텀 패치를 적용합니다.
4. **mpv SDK 빌드**: Meson 구성, 기능 검사, 컴파일, 설치, MSVC용 `mpv.lib` 생성을 진행합니다.
5. **SDL3 SDK 빌드**: CMake/MSVC로 구성·빌드·설치합니다.
6. **런타임 DLL 수집**: SDK/MINGW64에서 필요한 비시스템 DLL을 재귀적으로 수집합니다.
7. **설치 결과 검증**: 별도 MSVC 프로그램으로 DLL 로딩·커스텀 속성·D3D 프레임 출력과 픽셀 읽기를 확인합니다.

오래 걸리는 명령은 시작 전에 전체 명령과 로그 경로를 표시하고, 완료 후 소요 시간을 표시합니다.
스크립트 안내는 한글이며, Meson/CMake/컴파일러의 원본 진단은 검색·오류 확인을 위해 그대로 보존합니다.

## 실패 메시지를 읽는 방법

실패 시 종료 코드는 0이 아닌 값입니다. 다음 순서로 확인하세요.

1. `[FAIL] 실패 단계`와 실행 명령: 구성 실패인지 컴파일/링크/검증 실패인지 확인합니다.
2. `[사용한 도구·의존성 버전]`: 실제 설치 버전과 경로입니다. 참고값을 실제 사용값으로 기록하지 않습니다.
3. `[버전 관련 원본 진단]`: 원본 오류에 명시된 버전 요구 조건이 있으면 강조합니다.
4. `전체 로그`와 진단 JSON: 콘솔에는 원본 출력 끝부분을 표시하므로 전체 내용은 파일에서 확인합니다.

예를 들어 다음과 같은 **원본 오류가 있는 경우** Meson 버전과 요구 조건을 함께 표시합니다.

```text
Meson version is 0.64.0 but project requires >=1.3.0
```

네트워크 오류, 누락된 헤더, 링크 오류처럼 버전 원인을 확정할 근거가 없으면
특정 버전이 원인이라고 단정하지 않습니다. 버전 목록과 원본 오류로 추가 확인해야 합니다.
필요한 항목을 수동으로 준비한 뒤 같은 명령을 재실행하세요.

## 출력과 재실행

| 경로 | 내용 |
|---|---|
| `third_party/SDL`, `third_party/mpv` | 소스 checkout |
| `third_party/prebuilt/windows-x64/{mpv,sdl3}` | 앱에서 사용하는 SDK |
| `third_party/bin` | 배치할 런타임 DLL |
| `third_party/mpv_build/build/windows-x64/environments/<환경ID>/` | 현재 도구·의존성 조합의 mpv/sdl3/d3d-smoke 빌드 및 logs |
| 위 환경 폴더의 `bootstrap-environment.json` | 실제 버전·경로·빌드 설정 |
| `third_party/mpv_build/build/logs/windows-bootstrap-report.json` | 마지막 실제 실행의 성공/실패 및 진단 |
| SDK 루트의 `bootstrap-receipt.json` | D3D 검사까지 성공한 결과·실제 버전·빌드 폴더 |

도구·의존성의 버전이나 경로 등이 바뀌면 새 환경ID의 캐시를 사용합니다.
이전 빌드 폴더와 예전 방식의 캐시는 자동 삭제하지 않습니다. 같은 환경에서는 증분 빌드합니다.
SDK 출력 위치는 유지하므로 실제 재빌드가 시작되면 SDK 파일은 갱신됩니다.
이전 성공 receipt를 먼저 제거하고, 마지막 검증까지 통과해야 새 receipt를 작성합니다.
실패 후 일부 SDK 파일이 남을 수 있으므로 receipt가 없으면 완료된 SDK로 간주하지 마세요.

기존 소스는 HEAD/origin이 일치해야 하지만 로컬 파일 수정은 허용합니다.
이미 적용된 패치는 reverse-check로 확인하고, 미적용 패치는 forward-check 후 적용합니다.
부분 적용/충돌은 중단하며 reset/clean이나 소스 해시 대조는 하지 않습니다.
소스/패치 정책을 바꾸려면 기존 작업을 보존하고 해당 변경을 별도로 검토하세요.

## 검증 범위와 앱 빌드

D3D 검증에는 실제 Windows 그래픽 세션이 필요합니다. MINGW64를 제거한 PATH에서
배치된 DLL을 로드하고, 소프트웨어 디코딩한 테스트 영상을 D3D로 표시합니다.
D3D11VA·실제 영상·다채널·resize와 앱의 동작 전체를 검증하는 것은 아닙니다.

```powershell
C:\msys64\mingw64\bin\python.exe -B third_party/mpv_build/tests/test_bootstrap.py
```

위 Python 테스트는 임시 파일·mock 기반의 스크립트 검증이며 실제 SDK 빌드를 수행하지 않습니다.
성공한 SDK의 검증 프로그램/영상 위치는 receipt의 `build_directory` 아래 `d3d-smoke`입니다.
예전 고정 `build/d3d-smoke` 경로 대신 receipt를 기준으로 사용하세요.

```powershell
$receipt = Get-Content third_party/prebuilt/windows-x64/bootstrap-receipt.json -Raw | ConvertFrom-Json
& (Join-Path $receipt.build_directory 'd3d-smoke/Debug/mpv_d3d_smoke.exe') C:\path\video.mp4
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -E '^window_smoke$'
build/windows/debug/Debug/sdl_window_test.exe --smoke-test
```

`window_smoke`의 SDL dummy driver는 HWND를 제공하지 못하므로 D3D 검증으로 사용할 수 없습니다.
WebView2 Runtime 설치와 앱 Release 패키징도 별도 작업입니다.

2026-10-30: Windows 제어 흐름/공통 진단 자동 검증 PASS. 현재 Mac에서 실제 PowerShell,
MSVC/MinGW 빌드 및 D3D 검증은 NOT TESTED입니다. 기존 사용자 확인 결과를 수정된 스크립트의
실행 성공으로 확대하지 않습니다.
