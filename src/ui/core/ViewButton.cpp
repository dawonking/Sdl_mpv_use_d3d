#include "ViewButton.h"

#include <iostream>

namespace sdlviewer
{
    ViewButton::ViewButton(int id, WidgetConfig *opt, std::string text)
        : id_(id), buttonText_(std::move(text))
    {
        opt_ = opt;
    }

    void ViewButton::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = ImVec2(opt_->size.width, opt_->size.height);

        ClearStyles();
        btnType_ = opt_->btnType;

        AddStyle(ImGuiCol_Button, ImVec4(opt_->color.Normal.r, opt_->color.Normal.g, opt_->color.Normal.b, 1.0f));
        AddStyle(ImGuiCol_ButtonHovered, ImVec4(opt_->color.Select.r, opt_->color.Select.g, opt_->color.Select.b, 1.0f));
        AddStyle(ImGuiCol_ButtonActive, ImVec4(opt_->color.Hover.r, opt_->color.Hover.g, opt_->color.Hover.b, 1.0f));
    }

    void ViewButton::Render()
    {
        ImGui::SetCursorPos(pos_);

        PushStyles();

        btnRelease();
        btnClick();

        PopStyles();
    }

    void ViewButton::btnClick()
    {
    }

    void ViewButton::btnRelease()
    {
        const bool selected = IsSelected();
        if (selected)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(
                    opt_->color.Select.r,
                    opt_->color.Select.g,
                    opt_->color.Select.b,
                    opt_->color.Select.a));
        }

        const bool pressed = ImGui::Button(buttonText_.c_str(), size_);

        if (selected)
        {
            ImGui::PopStyleColor();
        }

        if (pressed)
        {
            if (btnType_ == BtnType::Toggle && !HasSelectedProvider())
            {
                StateChange(selected ? ButtonState::Normal : ButtonState::Click);
            }

            InvokeClick();
        }
    }

    void ViewButton::btnDragStart()
    {
        if (btnType_ != BtnType::DragType)
        {
            return;
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("IMG_BUTTON_DRAG", &id_, sizeof(int));
            ImGui::Text("Dragging Button ID: %d", id_);
            ImGui::EndDragDropSource();
        }
    }

    void ViewButton::btnDragEnd()
    {
        if (btnType_ != BtnType::DragType)
        {
            return;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("IMG_BUTTON_DRAG"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                const int sourceId = *static_cast<const int *>(payload->Data);
                std::cout << "Dragged from ID: " << sourceId << " to ID: " << id_ << "\n";
            }
            ImGui::EndDragDropTarget();
        }
    }
} // namespace sdlviewer
