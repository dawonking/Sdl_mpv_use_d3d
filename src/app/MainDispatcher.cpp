#include "MainDispatcher.h"

namespace sdlviewer
{
    void MainDispatcher::Init()
    {
        eventType_ = SDL_RegisterEvents(1);
    }

    void MainDispatcher::PostTask(std::function<void()> task)
    {
        if (!task)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(taskMutex_);
            tasks_.push_back(std::move(task));
        }

        SDL_Event event{};
        event.type = eventType_;
        SDL_PushEvent(&event);
    }

    void MainDispatcher::ProcessPendingTasks()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(taskMutex_);
            tasks.swap(tasks_);
        }

        for (auto &task : tasks)
        {
            if (task)
            {
                task();
            }
        }
    }
} // namespace sdlviewer
