#pragma once

#include "imgui.h"

namespace sdlviewer
{
class IUiComponent
{
public:
    virtual ~IUiComponent() = default;

    virtual void Init() = 0;
    virtual void Render() = 0;
    virtual void Refresh(float x, float y, float w, float h) = 0;
    virtual void WindowResize(int w, int h) = 0;
    virtual void Arrange(float x, float y, float w, float h) = 0;

    virtual void Move(float x, float y) = 0;
    virtual void Resize(float w, float h) = 0;
    virtual void MoveAddPoint(float x, float y) = 0;
    virtual void ResizeAddPoint(float w, float h) = 0;

    virtual ImVec2 GetPos() const = 0;
    virtual ImVec2 GetSize() const = 0;
    virtual ImVec2 GetPreferredSize() const = 0;
};
} // namespace sdlviewer
