# SDL Viewer

## 프로젝트 설명

SDL3와 libmpv를 사용한 C++ 영상 뷰어 포트폴리오 프로젝트입니다.
Windows에서는 애플리케이션의 D3D11 디바이스와 텍스처를 mpv 렌더링에 연결하고, 영상과 UI를 합성한 화면을 DirectComposition으로 표시합니다.

- AVI·MP4 파일 재생 및 파일명 규칙에 따른 다중 채널 묶음 처리
- 1·2·4·6분할 화면, 채널 배치 변경 및 전체 화면 전환
- 재생·일시 정지, 탐색, 재생 속도, 프레임 이동 및 음량 제어
- 영상 좌우·상하 반전, 선택 채널 테두리 표시
- JSON 기반 UI 레이아웃, ImPlot 센서 그래프 및 WebView2 지도 화면

센서 그래프는 현재 샘플 데이터를 사용합니다.
macOS는 UI 실행 경로와 SDK 빌드 스크립트가 있으며, OpenGL 영상 렌더링은 아직 구현되지 않았습니다.

## 프로젝트 사용 기술

| 기술 | 용도 |
|---|---|
| C++17, SDL3 | 애플리케이션 로직, 창 생성, 입력·이벤트 처리 |
| libmpv, FFmpeg | 영상 디코딩과 재생 제어 |
| D3D11, DXGI, DirectComposition | Windows 영상 렌더링과 화면 합성 |
| Dear ImGui, ImPlot | UI 구성과 센서 그래프 표시 |
| nlohmann/json, stb_image, NanoSVG | UI 설정과 이미지·SVG 리소스 처리 |
| WebView2 | Windows 지도 웹 화면 표시 |
| Objective-C++, OpenGL | macOS 창 연동과 UI 렌더링 |
| CMake, PowerShell, Python, Meson, Ninja | 앱·외부 라이브러리 빌드와 패키징 |

외부 라이브러리와 관련 빌드 설정은 `third_party/`에서 관리합니다.
Windows의 `bootstrap.ps1`은 `third_party/mpv_build/windows-lock.json`에 지정된 공개 SDL3·mpv 저장소를 각각 `third_party/SDL/`, `third_party/mpv/`에 clone하고 지정 커밋을 준비합니다.
이후 `third_party/mpv_build/patches/0001-custom-mpv.patch`를 적용합니다. 패치에는 외부 D3D11 리소스를 사용하는 Render API 확장과 영상 반전·테두리 등의 옵션이 포함됩니다.

mpv는 MSYS2 MINGW64에서 DLL로 빌드하고 MSVC에서 링크할 수 있도록 `mpv.lib`를 생성합니다. SDL3는 MSVC로 빌드합니다.
소스 커밋과 패치 해시는 고정해 검사하지만, 도구·의존성 버전은 참고값이며 실제 설치된 버전으로 호환성을 확인합니다.

### mpv D3D11 Render API 확장

mpv의 기존 D3D11 렌더러를 기반으로, 애플리케이션이 만든 디바이스와 출력 텍스처를 libmpv Render API에 전달하는 경로를 추가했습니다.

- **API 입력 추가**: `d3d11` 렌더링 방식과 초기화·출력 대상 파라미터를 등록하고, `render_d3.h`에 디바이스와 텍스처를 전달하는 구조체를 정의했습니다.
- **외부 디바이스 연결**: 앱이 전달한 `ID3D11Device`로 mpv의 D3D11 렌더링 계층을 초기화하고 기존 GPU 영상 처리 경로에 연결합니다.
- **외부 텍스처 출력**: 앱이 지정한 `ID3D11Texture2D`와 가로·세로 크기를 출력 대상으로 받아, mpv가 해당 텍스처에 프레임을 렌더링합니다.
- **렌더링 동작 연결**: 출력 크기 조회, 화면 크기 변경, 영상 형식 재설정, ICC 프로필, OSD, 스크린샷 처리를 기존 mpv 렌더러에 연결합니다.
- **빌드·SDK 연결**: D3D11 기능을 켠 빌드에 새 Render API 백엔드를 등록하고, `render_d3.h`를 SDK 헤더에 포함합니다.

앱에서는 SDL3가 창과 입력을 관리하고, 채널마다 mpv 렌더 컨텍스트와 출력 텍스처를 생성합니다.
렌더링된 텍스처와 ImGui UI를 앱의 D3D11 화면에 그리고, 그 결과를 DirectComposition에 연결합니다. WebView2 지도도 별도 합성 요소로 배치합니다.
이 앱의 화면 구성과 다중 채널 관리는 mpv의 Render API 확장과 구분됩니다.

## 빌드 방법

### Windows 준비 사항

- Windows x64와 64비트 PowerShell
- Visual Studio 2022의 **C++를 사용한 데스크톱 개발** 도구 및 Windows SDK
- PATH에 등록된 Git과 CMake **3.26 이상**
- MSYS2 **MINGW64**의 Python, GCC/G++, Meson, Ninja, pkg-config, binutils, FFmpeg
- MINGW64용 FFmpeg 개발 라이브러리, libass, libplacebo 등 `third_party/mpv_build/windows-lock.json`의 `reference_dependencies` 항목
- 지도 화면 실행에 필요한 Microsoft Edge WebView2 Runtime

Windows bootstrap은 도구나 의존성 라이브러리를 자동 설치·업데이트하지 않습니다.
MSYS2 기본 경로는 `C:\msys64`입니다. 다른 위치에 설치했다면 아래 두 bootstrap 명령에 `-MsysRoot D:\msys64`처럼 경로를 추가합니다.

bootstrap은 설치된 최신 Visual Studio를 선택하므로 해당 버전을 지원하는 CMake가 필요합니다. 앱 빌드 프리셋은 Visual Studio 2022를 사용합니다.
환경 준비의 자세한 내용은 `third_party/mpv_build/README.md`를 참고합니다.

### 1. SDL3·mpv SDK 준비

프로젝트 루트에서 다음 명령을 순서대로 실행합니다.

```powershell
# 필요한 도구와 의존성 확인
powershell -ExecutionPolicy Bypass -File .\bootstrap.ps1 -CheckOnly

# 소스 clone, mpv 패치 적용, SDK 빌드 및 검증
powershell -ExecutionPolicy Bypass -File .\bootstrap.ps1
```

`-CheckOnly`는 다운로드·패치 적용·빌드를 수행하지 않으며, 통과하더라도 실제 빌드 성공을 보장하지 않습니다.
실제 bootstrap은 마지막에 별도 SDK 테스트로 DLL 로딩과 D3D 프레임 출력을 확인하므로 Windows 그래픽 세션이 필요합니다.

| 생성 경로 | 내용 |
|---|---|
| `third_party/prebuilt/windows-x64/sdl3/` | SDL3 헤더·라이브러리 |
| `third_party/prebuilt/windows-x64/mpv/` | mpv 헤더·MSVC 링크 라이브러리 |
| `third_party/bin/` | SDL3·mpv와 의존성 런타임 DLL |

SDK 빌드 과정의 진단 정보와 명령 로그는 `third_party/mpv_build/build/logs/windows-bootstrap-report.json`과 `third_party/mpv_build/build/windows-x64/environments/<환경ID>/logs/`에 기록됩니다.
`-CheckOnly`나 초기 환경 검사에서 중단되면 로그 파일이 생성되지 않을 수 있으므로 콘솔 오류 메시지를 확인합니다.

### 2. Windows 앱 빌드 및 배포 폴더 생성

SDK 준비가 끝나면 Release 빌드 스크립트 `build_script/release_windows.ps1`을 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\build_script\release_windows.ps1
```

스크립트는 `windows-msvc-release` 프리셋으로 앱을 빌드하고, CMake 설치 단계에서 실행 파일·리소스·런타임 DLL을 모아 `Output/windows/`에 배치합니다.
기존 `Output/windows/`는 교체되므로 개인 파일을 보관하지 않습니다.
앱은 Release로 빌드되며, mpv는 bootstrap의 Debug/shared 설정으로 만든 SDK를 그대로 사용합니다.

```powershell
Push-Location .\Output\windows
.\sdl_window_test.exe
Pop-Location
```

배포할 때는 `Output/windows/` 전체를 사용합니다. Release 스크립트는 CTest나 앱 실행을 수행하지 않으므로 생성된 앱의 재생·UI 동작은 별도로 확인합니다.

### macOS SDK 준비

`bootstrap.sh`는 **Apple Silicon arm64 / macOS 14.0 이상**을 대상으로 SDL3·mpv SDK를 준비합니다.
macOS SDK·Clang·Swift, Git, Python 3, CMake, Meson, Ninja, pkg-config가 필요합니다.

```sh
sh ./bootstrap.sh --check-only
sh ./bootstrap.sh
```

SDK는 `third_party/prebuilt/mac_arm64/`에 생성됩니다. 누락된 의존성 라이브러리는 실제 빌드 과정에서 사용자 동의를 받아 Homebrew로 설치할 수 있습니다.
앱 빌드에는 SDK 경로를 별도로 지정해야 하며, 현재 macOS 앱은 UI 실행 단계입니다.
자세한 환경과 옵션은 `third_party/mpv_build/README_MACOS.md`를 참고합니다.
