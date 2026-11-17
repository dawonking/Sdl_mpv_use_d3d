#pragma once

struct mpv_handle;

namespace sdlviewer
{
    // handle의 소유권을 가져가지 않는다.
    // 성공하면 0 이상, 실패하면 mpv 오류 코드(< 0)를 반환한다.
    int ApplyPlatformMpvOptions(
        mpv_handle* handle);
}