#!/bin/sh
# 설치된 도구·의존성으로 macOS SDL3/mpv SDK를 빌드합니다.
set -eu

project_root=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
python_command=${PYTHON:-python3}
if ! command -v "$python_command" >/dev/null 2>&1; then
    echo "[FAIL] Python 3를 찾지 못했습니다. 설치 경로를 PYTHON 환경 변수로 지정하세요." >&2
    exit 1
fi
exec "$python_command" -B "$project_root/third_party/mpv_build/build_macos.py" \
    --project "$project_root" "$@"
