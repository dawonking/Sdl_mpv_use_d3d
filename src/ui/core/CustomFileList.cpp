#include "CustomFileList.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace sdlviewer
{
    namespace
    {
        const char *FileTypeToString(int32_t type)
        {
            switch (static_cast<FileType>(type))
            {
            case FileType::INF:
                return "INF";
            case FileType::PRK:
                return "PRK";
            case FileType::EVT:
                return "EVT";
            case FileType::USR:
                return "USR";
            default:
                return "";
            }
        }

        void RenderCenteredScaledText(const char *text, float cellHeight)
        {
            const float startY = ImGui::GetCursorPosY();
            const float columnWidth = ImGui::GetColumnWidth();
            float textWidth = ImGui::CalcTextSize(text).x;
            float textHeight = ImGui::CalcTextSize(text).y;

            if (textWidth > columnWidth && columnWidth > 0.0f)
            {
                const float scale = columnWidth / textWidth;
                ImGui::SetWindowFontScale(scale);
                textWidth = columnWidth;
                textHeight = ImGui::CalcTextSize(text).y;
            }

            const float offsetX = (columnWidth - textWidth) * 0.5f;
            const float offsetY = (cellHeight - textHeight) * 0.5f;

            if (offsetX > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }
            if (offsetY > 0.0f)
            {
                ImGui::SetCursorPosY(startY + offsetY);
            }

            ImGui::TextUnformatted(text);
            ImGui::SetWindowFontScale(1.5f);
        }

        bool RenderCenteredCheckbox(const char *id, bool *value, float cellHeight)
        {
            const float startY = ImGui::GetCursorPosY();

            ImGui::SetWindowFontScale(0.5f);

            const float columnWidth = ImGui::GetColumnWidth();
            const float checkSize = ImGui::GetFrameHeight();

            const float offsetX = (columnWidth - checkSize) * 0.5f;
            const float offsetY = (cellHeight - checkSize) * 0.5f;

            if (offsetX > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }
            if (offsetY > 0.0f)
            {
                ImGui::SetCursorPosY(startY + offsetY);
            }

            const bool changed = ImGui::Checkbox(id, value);
            ImGui::SetWindowFontScale(1.5f);
            return changed;
        }
    } // namespace

    CustomFileList::CustomFileList(int id, WidgetConfig *opt)
        : id_(id)
    {
        opt_ = opt;
    }

    void CustomFileList::Init()
    {
        headList_.clear();
    }

    void CustomFileList::SetFileList(const std::vector<FileItemRaw> &items)
    {
        fileList_ = items;
        columnChecked_.assign(fileList_.size(), false);
        selectionAnchor_ = -1;
        playingIndex_ = -1;
    }

    void CustomFileList::SetPlayingIndex(int index)
    {
        playingIndex_ = index >= 0 && index < static_cast<int>(fileList_.size()) ? index : -1;
    }

    void CustomFileList::SetChecked(int index, bool checked)
    {
        if (index >= 0 && index < static_cast<int>(columnChecked_.size()))
            columnChecked_[index] = checked;
    }

    void CustomFileList::SelectChecked(int index, bool checked, bool extendRange, bool toggleSelection)
    {
        if (index < 0 || index >= static_cast<int>(columnChecked_.size()))
            return;

        if (!extendRange || selectionAnchor_ < 0)
            selectionAnchor_ = index;

        if (extendRange)
        {
            std::fill(columnChecked_.begin(), columnChecked_.end(), false);
            const int first = std::min(selectionAnchor_, index);
            const int last = std::max(selectionAnchor_, index);
            std::fill(columnChecked_.begin() + first, columnChecked_.begin() + last + 1, true);
        }
        else
        {
            if (!toggleSelection)
                std::fill(columnChecked_.begin(), columnChecked_.end(), false);
            SetChecked(index, toggleSelection ? checked : true);
        }
    }

    std::vector<int> CustomFileList::GetCheckedIndices() const
    {
        std::vector<int> indices;
        for (int index = 0; index < static_cast<int>(columnChecked_.size()); ++index)
            if (columnChecked_[index])
                indices.push_back(index);
        return indices;
    }

    void CustomFileList::HeadResize()
    {
        headList_.push_back(ImGui::CalcTextSize("INF"));
        headList_.push_back(ImGui::CalcTextSize("00.00.00"));
        headList_.push_back(ImGui::CalcTextSize("00:00:00"));
        headList_.push_back(ImGui::CalcTextSize("00:00"));
        headList_.push_back(ImGui::CalcTextSize("20210924_104847_INF_3"));
    }

    void CustomFileList::Render()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        if (headList_.empty())
        {
            HeadResize();
        }

        if (!ImGui::BeginTable(opt_->objName.c_str(), 6, tableFlags_))
        {
            return;
        }

        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A) && !ImGui::GetIO().WantTextInput)
            std::fill(columnChecked_.begin(), columnChecked_.end(), true);

        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::SetWindowFontScale(0.1f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn(headOne_.c_str(), ImGuiTableColumnFlags_WidthFixed, headList_[0].x);
        ImGui::TableSetupColumn(headTwo_.c_str(), ImGuiTableColumnFlags_WidthFixed, headList_[1].x);
        ImGui::TableSetupColumn(headThree_.c_str(), ImGuiTableColumnFlags_WidthFixed, headList_[2].x);
        ImGui::TableSetupColumn(headFour_.c_str(), ImGuiTableColumnFlags_WidthFixed, headList_[3].x);
        ImGui::TableSetupColumn(headFive_.c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::SetWindowFontScale(1.0f);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int column = 0; column < 6; ++column)
        {
            ImGui::TableSetColumnIndex(column);
            const char *columnName = ImGui::TableGetColumnName(column);

            const float columnWidth = ImGui::GetColumnWidth();
            const float textWidth = ImGui::CalcTextSize(columnName).x;
            const float offsetX = (columnWidth - textWidth) * 0.5f;

            if (offsetX > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }
            ImGui::TextUnformatted(columnName);
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(fileList_.size()));

        constexpr float rowHeight = 34.0f;
        constexpr ImU32 normalColor = IM_COL32(40, 40, 50, 255);
        const ImVec4 hoverColor(0.3f, 0.4f, 0.5f, 1.0f);
        const ImVec4 checkedColor(0.2f, 0.5f, 0.8f, 1.0f);

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const FileItemRaw &item = fileList_[i];

                char dateBuf[13];
                char time1Buf[9];
                char time2Buf[9];
                char nameBuf[129];
                snprintf(dateBuf, sizeof(dateBuf), "%.*s", static_cast<int>(sizeof(item.date)), item.date);
                snprintf(time1Buf, sizeof(time1Buf), "%.*s", static_cast<int>(sizeof(item.time1)), item.time1);
                snprintf(time2Buf, sizeof(time2Buf), "%.*s", static_cast<int>(sizeof(item.time2)), item.time2);
                snprintf(nameBuf, sizeof(nameBuf), "%.*s", static_cast<int>(sizeof(item.filename)), item.filename);

                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

                if (!columnChecked_[i])
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                          i == playingIndex_ ? IM_COL32(40, 65, 75, 255) : normalColor);
                }

                ImGui::TableSetColumnIndex(0);

                const float startY = ImGui::GetCursorPosY();

                const std::string rowId = "##row_" + std::to_string(i);
                const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;

                ImGui::PushStyleColor(ImGuiCol_Header, checkedColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, checkedColor);

                if (ImGui::Selectable(rowId.c_str(), columnChecked_[i], selectFlags, ImVec2(0, rowHeight)))
                {
                    if (!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        SelectChecked(i, true, false, false);
                        std::cout << "[" << i << "] is double clicked\n";
                        std::cout << "Start : " << clipper.DisplayStart << " , End : " << clipper.DisplayEnd << "\n";

                        if (HasDoubleClickCallback() &&
                            (!playStateProvider_ || playStateProvider_()))
                        {
                            InvokeDoubleClick(i);
                        }
                    }
                    else
                    {
                        std::cout << "[" << i << "] is clicked\n";
                        SelectChecked(i, !columnChecked_[i], ImGui::GetIO().KeyShift, ImGui::GetIO().KeyCtrl);
                    }
                }

                ImGui::PopStyleColor(3);

                ImGui::SetCursorPosY(startY);

                const std::string hiddenLabel = "##checkbox_" + std::to_string(i);
                bool checked = columnChecked_[i];
                if (RenderCenteredCheckbox(hiddenLabel.c_str(), &checked, rowHeight))
                    SelectChecked(i, checked, ImGui::GetIO().KeyShift, ImGui::GetIO().KeyCtrl);

                ImGui::TableSetColumnIndex(1);
                RenderCenteredScaledText(FileTypeToString(item.type), rowHeight);

                ImGui::TableSetColumnIndex(2);
                RenderCenteredScaledText(dateBuf, rowHeight);

                ImGui::TableSetColumnIndex(3);
                RenderCenteredScaledText(time1Buf, rowHeight);

                ImGui::TableSetColumnIndex(4);
                RenderCenteredScaledText(time2Buf, rowHeight);

                ImGui::TableSetColumnIndex(5);
                RenderCenteredScaledText(nameBuf, rowHeight);
            }
        }

        clipper.End();
        ImGui::EndTable();
    }
} // namespace sdlviewer
