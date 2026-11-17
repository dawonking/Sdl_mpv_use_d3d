#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <mutex>
#include <vector>

namespace sdlviewer
{
    class MainDispatcher
    {
    public:
        void Init();

        Uint32 EventType() const { return eventType_; }

        void PostTask(std::function<void()> task);
        void ProcessPendingTasks();

    private:
        std::mutex taskMutex_;
        std::vector<std::function<void()>> tasks_;
        Uint32 eventType_ = 0;
    };
} // namespace sdlviewer
