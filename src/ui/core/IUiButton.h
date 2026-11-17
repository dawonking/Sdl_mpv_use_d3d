#pragma once

namespace sdlviewer
{
class IUiButton
{
public:
    virtual ~IUiButton() = default;

    virtual void btnClick() = 0;
    virtual void btnRelease() = 0;
    virtual void btnDragStart() = 0;
    virtual void btnDragEnd() = 0;
};
} // namespace sdlviewer
