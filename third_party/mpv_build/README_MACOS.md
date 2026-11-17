# macOS SDL3/mpv SDK 빌드 안내

`bootstrap.sh`는 Python 스크립트를 통해 SDL3와 커스텀 mpv를 빌드합니다.
현재 대상은 **native Apple Silicon arm64 / macOS 14.0**입니다.
Intel·Rosetta·universal 빌드는 포함하지 않습니다.
Windows는 [Windows 안내](README.md)를 참고하세요.

## 처음 실행하는 순서

Xcode의 macOS SDK·Clang·Swift, Git, Python, CMake, Meson, Ninja, pkg-config를 먼저 준비합니다.
아래 라이브러리가 없으면 실제 빌드 실행 중 Homebrew 설치 여부를 묻습니다.
Homebrew 자체와 빌드 도구는 이 기능의 설치 대상이 아닙니다.

```sh
./bootstrap.sh --check-only
./bootstrap.sh
```

첫 명령은 설치 상태만 검사합니다. 다운로드·소스 커밋 확인·패치 적용·구성·빌드·설치와
파일 쓰기는 하지 않습니다. 검사 PASS는 실제 라이브러리 빌드 성공을 뜻하지 않습니다.
두 번째 명령은 누락 라이브러리 설치 동의를 거쳐 실제 SDK를 빌드합니다.
라이브러리가 이미 준비되어 있으면 설치 질문 없이 해당 버전으로 빌드를 시작합니다.
처음에는 Homebrew 패키지 정보와 공식 소스 저장소에 접근할 네트워크가 필요합니다.

프로젝트 밖에서는 `sh /path/to/project/bootstrap.sh`처럼 실행할 수 있습니다.
`PYTHON` 환경 변수로 Python 실행 파일을 선택하고, `--jobs 4`로 병렬 작업 수를 조절합니다.
mpv의 cplayer와 Cocoa를 유지하므로 Swift도 필요합니다.

## FFmpeg/libass 등의 준비와 검색 경로

빌드 대상은 **mpv와 SDL3**입니다. FFmpeg·libass·libplacebo·libarchive·libjpeg·zlib는
설치된 arm64 shared 라이브러리와 pkg-config 메타데이터를 사용합니다.
mpv가 이 의존성들을 자동으로 빌드하는 것은 아닙니다.

Homebrew 환경의 해당 패키지 이름은 `ffmpeg`, `libass`, `libplacebo`, `libarchive`,
`jpeg-turbo`, `zlib`입니다. 기본 검색 루트는 `/opt/homebrew`이며 해당 formula의
`opt/.../lib/pkgconfig`와 루트의 `lib/pkgconfig`, `share/pkgconfig`를 사용합니다.
상속된 pkg-config 경로는 제거하여 `/usr/local`의 다른 설치본이 자동 선택되지 않게 합니다.
설치된 `opt/ffmpeg@...` 등의 버전별 경로도 탐색합니다. 전역 `brew link`는 변경하지 않습니다.

```sh
./bootstrap.sh --check-only --deps-prefix /path/to/arm64/dependencies
./bootstrap.sh --check-only --pkg-config-path /path/to/ffmpeg/lib/pkgconfig
./bootstrap.sh --jobs 4
```

`--pkg-config-path`는 여러 번 지정할 수 있으며 우선 검색합니다.
별도 경로를 지정해 검사했다면 실제 빌드 명령에도 같은 인자를 전달하세요.

## 라이브러리가 없을 때의 설치 동의

1. 필요한 라이브러리 전체를 검사하고 누락 목록을 표시합니다. 검색 경로에 설치본이 있으면 재사용합니다.
2. 누락 목록을 Homebrew 패키지로 묶습니다. FFmpeg의 7개 라이브러리는 `ffmpeg` 설치 한 번으로 처리합니다.
3. 공식 `homebrew/core`의 stable 버전 중 레시피의 패키지 최소 버전을 만족하는 후보를 선택합니다.
   기본 formula가 사용할 수 없는 버전이거나 비활성화되어 있으면 공식 버전별 formula도 확인합니다.
4. 설치할 패키지·예정 버전·필요 조건과 `brew install --dry-run`의 연관 패키지 변경 계획을 표시합니다.
5. 아래 질문에서 `y`, `yes`, `예`, `네`를 입력한 경우에만 설치합니다.

```text
위 패키지와 연관 의존성을 설치하고 빌드를 계속할까요? [y/N]:
```

`n`, 빈 입력, 다른 응답, EOF 또는 질문 중 Ctrl+C는 거부로 처리하고 종료합니다.
비대화형 실행에서는 설치 동의를 받을 수 없으므로 종료합니다. 자동 동의 옵션은 없습니다.
거부하면 Homebrew 설치와 SDK 빌드는 실행하지 않습니다. 후보 확인 중 Homebrew의
메타데이터 조회/캐시 접근은 있을 수 있지만 라이브러리 설치 명령은 동의 후에만 실행합니다.

동의 후에도 패키지 정의/버전이나 설치 계획이 바뀌었으면 중단하여 다시 확인하도록 합니다.
설치가 성공하면 승인한 패키지 버전을 확인하고 **모든 도구·라이브러리를 다시 탐색**합니다.
새로 설치한 라이브러리는 실제 pkg-config 최소 버전, arm64, dylib 및 배포 대상 검사를 통과해야 합니다.
설치 실패·여전한 누락·재검사 실패 시 SDK를 빌드하지 않으며 반복 설치하지 않습니다.

| 검사하는 라이브러리 | Homebrew 패키지 | 새 설치 후보의 패키지 최소 버전 |
|---|---|---|
| libavcodec/libavfilter/libavformat/libavutil/libswresample/libswscale/libavdevice | ffmpeg | 6.1 |
| libass | libass | 0.12.2 |
| libplacebo | libplacebo | 7.360.1 |
| libarchive | libarchive | 3.4.0 |
| libjpeg | jpeg-turbo | 1.0.0 |
| zlib | zlib | 1.2.11 |

패키지 최소 버전은 `homebrew_formulas`, 라이브러리별 최소 버전은 `dependencies`에 기록합니다.
예를 들어 FFmpeg 패키지 버전 `6.1`과 libavcodec 버전 `60.31.102`는 서로 다른 번호입니다.
정확한 과거 버전을 강제하거나 별도 tap을 자동 추가하지 않습니다. 설치 후보가 없으면 중단합니다.
패키지 버전 조건을 만족해도 현재 OS 지원 여부나 소스/API 호환성은 설치·재검사·실제 빌드에서 실패할 수 있습니다.

Homebrew가 없거나 `brew --prefix`가 선택한 `--deps-prefix`와 다르면 자동 설치하지 않고 안내합니다.
Homebrew에는 설치되어 있는데 pkg-config/dylib가 없으면 설치 경로/패키지 손상 문제로 보고
자동 재설치·업그레이드하지 않습니다. 명시한 `--pkg-config-path`는 설치 후에도 우선합니다.

스크립트는 전체 `brew upgrade`, 자동 update/cleanup, 관련 없는 dependents 갱신을 실행하지 않습니다.
다만 승인한 패키지에 필요한 **연관 의존성은 설치·갱신될 수 있으며**, 이를 동의 화면에서 안내합니다.
설치가 일부 진행된 후 실패하더라도 설치된 Homebrew 패키지를 자동 삭제하거나 롤백하지 않습니다.

## 버전 정책과 유지하는 검사

**설치된 도구·의존성 버전으로 빌드를 시도합니다.**
이미 설치되어 있는 항목은 `reference_tools`/`upstream_minimum`과 버전이 다르다는 이유만으로
미리 중단하지 않습니다. 새 Homebrew 설치 후보 및 설치 후에는 위 최소 조건을 검사합니다.
버전을 읽지 못한 도구는 UNKNOWN으로 표시하고
실제 실행에서 확인합니다. 실제 소스가 요구하는 버전/API 호환성은 Meson/CMake와
컴파일러의 구성·빌드·링크 결과로 판정합니다.

예를 들어 pinned mpv의 FFmpeg 요구 조건은 libavcodec 60.31.102,
libavfilter 9.12.100, libavformat 60.16.100, libavutil 58.29.100,
libswresample 4.12.100, libswscale 7.5.100, libavdevice 60.3.100 이상입니다.
더 오래된 설치본도 스크립트의 버전 비교로 차단하지 않지만 upstream 구성 단계에서
거절될 수 있습니다. Libplacebo의 참고 요구 조건은 7.360.1 이상입니다.

누락 라이브러리는 위 동의 절차를 거치며, 필수 도구의 부재, arm64 누락,
SDK/실행 OS 및 dylib 최소 OS와 배포 대상의 충돌, 소스·패치·필수 기능 불일치는 중단합니다.
배포 대상 14.0을 지정한다고 해서
macOS 15 전용 의존성이 14에서 실행 가능한 것은 아니기 때문입니다.

## 소스와 빌드 설정

Windows와 같은 소스 및 패치를 사용합니다.

- SDL3: `6243fa2ffd8de2cf310ec4851eb0d9faef83b2fa`.
- mpv: `b51131dff917fbccbedc62a8719bd976a05b5160`.
- 공유 패치: `patches/0001-custom-mpv.patch`, SHA256 검사 유지.
- 소스 경로: `third_party/SDL`, `third_party/mpv`.

기존 소스의 HEAD/origin은 일치해야 하지만 로컬 파일 수정은 허용합니다.
이미 적용된 패치는 reverse-check로 확인하고 미적용 패치는 forward-check 후 적용합니다.
부분 적용이나 충돌은 중단하며 reset/clean·소스 파일 해시 대조는 하지 않습니다.

SDL3는 Release dylib, Cocoa/OpenGL 활성화, framework/static/test 비활성화입니다.
mpv는 Debug/shared, libmpv/cplayer 활성화, GPL 비활성화와 커스텀 flip/border 패치를 유지합니다.
Cocoa·CoreAudio·AVFoundation·OpenGL·plain-gl·VideoToolbox-GL을 활성화하고
D3D/Win32·VAAPI·Vulkan·shaderc·SPIRV-Cross를 비활성화합니다.
문서 생성과 mpv 자체 media-player/Touch Bar UI는 사용하지 않습니다.
Meson 의존성 fallback 다운로드도 비활성화합니다.

## 출력 단계와 실패 확인

환경 검사 단계에서는 각 도구·의존성의 실제 버전과 경로, 참고 버전을 표시합니다.
실제 빌드에서는 소스 준비 후 다음 명령을 순서대로 실행합니다.

1. mpv 구성 → 필수 HAVE_* 확인 → 컴파일 → 설치.
2. SDL3 구성 → 컴파일 → 설치.
3. 설치된 헤더·패키지 정보·arm64·최소 OS·필수 공개 함수·직접 dylib 경로 검사.

각 명령의 시작/완료, 실행 명령, 전체 로그 경로와 소요 시간을 한글로 안내합니다.
Meson/CMake/컴파일러의 원본 오류는 그대로 보존합니다.
실패하면 실패 단계, 사용한 버전·경로, 원본 출력 끝부분과 로그 위치를 함께 표시합니다.
원본 오류에 버전 요구 조건이 명시되어 있으면 `[버전 관련 원본 진단]`에서 강조합니다.
원인이 확인되지 않으면 특정 버전을 원인으로 단정하지 않습니다.

실제 실행의 마지막 진단은 다음 파일에 저장합니다.

```text
third_party/mpv_build/build/logs/macos-bootstrap-report.json
third_party/mpv_build/build/logs/homebrew-install.log   # 동의 후 설치 명령의 원본 출력
```

검사 전용 실행은 진단 JSON도 쓰지 않습니다. 오류가 나면 콘솔의 패키지 이름과 검색 경로를
확인하세요. 설치를 원하면 `--check-only` 없이 실행하고, 경로/패키지 손상은 먼저 수동으로 해결하세요.

## 환경 변경과 산출물

```text
third_party/mpv_build/build/mac_arm64/
  bootstrap-environment.json        # 마지막으로 빌드를 시도한 환경
  environments/<환경ID>/
    bootstrap-environment.json      # 이 캐시의 실제 입력
    mpv/                            # Meson 빌드
    sdl3/                           # CMake/Ninja 빌드
    logs/
    mpv-features.json

third_party/prebuilt/mac_arm64/
  mpv/include/mpv/
  mpv/lib/libmpv*.dylib
  mpv/lib/pkgconfig/mpv.pc
  mpv/bin/mpv
  sdl3/include/SDL3/
  sdl3/lib/libSDL3*.dylib
  sdl3/lib/cmake/SDL3/
  bootstrap-environment.json
  build-receipt.json
```

도구·SDK·의존성 버전/경로와 직접 dylib 해시 등을 환경 기록에 저장합니다.
이 기록은 도구 버전을 고정하는 잠금 파일이 아닙니다.
환경이 달라지면 새 환경ID의 캐시로 구성·빌드하며 이전 캐시를 보존합니다.
이전 스크립트의 관리 표식이 있는 SDK도 같은 소스/설정이면 새 환경으로 빌드를 시도합니다.
동일 환경에서는 증분 빌드를 하고, 그 환경에서 감지한 전체 HAVE_* 결과도 비교합니다.

소스·필수 기능·배포 대상 등 빌드 계약을 바꾸려는 경우에는 기존 SDK와 빌드 폴더를
보존하여 별도로 옮긴 후 변경을 검토하세요. 관리 기록이 없는 비어 있지 않은 SDK 폴더는
덮어쓰지 않습니다. 동일 Mac bootstrap의 동시 실행도 차단합니다.

SDK 설치 위치는 유지하며 실제 빌드에서는 해당 위치의 파일을 갱신합니다.
기존 성공 receipt는 재빌드 시작 시 제거하고, 산출물 검사까지 성공해야 새 receipt를 작성합니다.
실패하면 부분 SDK가 남을 수 있으므로 receipt 없이 완료된 빌드로 판단하지 마세요.
receipt의 `sdk_build=PASS`는 정적 검사를 포함한 SDK 빌드 성공이고,
`native_smoke`, `application`, `relocatable_package`는 계속 `NOT TESTED`입니다.

## 검증 범위

```sh
sh -n bootstrap.sh
python3 -B third_party/mpv_build/tests/test_macos_bootstrap.py
python3 -B third_party/mpv_build/tests/test_macos_brew_bootstrap.py
./bootstrap.sh --check-only
```

Python 검증은 임시 Git fixture와 mock 명령을 사용합니다. 실제 다운로드나 컴파일은 하지 않습니다.
2026-10-30: Homebrew 동의/설치 제어 흐름 23개, 기존 Mac 19개와 Windows/공통 16개를 포함해
자동 검증 58개 및 셸 문법 PASS. Homebrew 설치와 SDK 명령은 mock으로 검증했습니다.
실제 `--check-only`는 FFmpeg의 7개 라이브러리와 zlib 누락을 표시하고 FAIL로 종료했으며,
설치 질문·Homebrew 설치·프로젝트 파일 생성을 하지 않았습니다.
실제 Homebrew 설치, SDK configure/build/install과 runtime은 NOT TESTED입니다.

SDK는 사전 설치된 dylib를 계속 사용합니다. 전이 의존성 전체 수집·재배치·서명·앱 패키징,
커스텀 속성·OpenGL 재생·VideoToolbox 디코딩은 별도 검증입니다.
앱 CMake는 기존 설치 패키지 탐색을 유지하며 bootstrap 실행만으로 이 SDK를 자동 선택하지 않습니다.

설정 확인에 사용한 upstream 소스:

- [mpv 의존성·플랫폼 구성](https://github.com/mpv-player/mpv/blob/b51131dff917fbccbedc62a8719bd976a05b5160/meson.build)
- [mpv 옵션](https://github.com/mpv-player/mpv/blob/b51131dff917fbccbedc62a8719bd976a05b5160/meson.options)
- [SDL3 CMake 설정](https://github.com/libsdl-org/SDL/blob/6243fa2ffd8de2cf310ec4851eb0d9faef83b2fa/CMakeLists.txt)
- [Homebrew 설치·dry-run·JSON 정보 명령](https://docs.brew.sh/Manpage)
- [FFmpeg 6.1의 libavcodec 버전 정의](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavcodec/version.h)
