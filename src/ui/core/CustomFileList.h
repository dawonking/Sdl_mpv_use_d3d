#pragma once

#include "FileItemRaw.h"
#include "UiComponentBase.h"
#include "WidgetConfig.h"

#include "imgui.h"

#include <functional>
#include <string>
#include <vector>

namespace sdlviewer
{
    class CustomFileList final : public UiComponentBase
    {
    public:
        using DoubleClickCallback = std::function<void(int index)>;
        using PlayStateProvider = std::function<bool()>;

        CustomFileList(int id, WidgetConfig *opt);

        void Init() override;
        void Render() override;

        void SetDoubleClickCallback(DoubleClickCallback callback)
        {
            onDoubleClick_ = std::move(callback);
        }

        void ClearDoubleClickCallback()
        {
            onDoubleClick_ = nullptr;
        }

        bool HasDoubleClickCallback() const
        {
            return static_cast<bool>(onDoubleClick_);
        }

        void SetSelectedProvider(PlayStateProvider provider)
        {
            playStateProvider_ = std::move(provider);
        }

        void ClearSelectedProvider()
        {
            playStateProvider_ = nullptr;
        }

        bool HasSelectedProvider() const
        {
            return static_cast<bool>(playStateProvider_);
        }

        void SetFileList(const std::vector<FileItemRaw> &items);
        void SetPlayingIndex(int index);
        int GetPlayingIndex() const { return playingIndex_; }
        void SetChecked(int index, bool checked);
        std::vector<int> GetCheckedIndices() const;

        std::vector<FileItemRaw> &GetUiList()
        {
            return fileList_;
        }

    private:
        void InvokeDoubleClick(int index)
        {
            if (onDoubleClick_)
            {
                onDoubleClick_(index);
            }
        }

        void HeadResize();
        void SelectChecked(int index, bool checked, bool extendRange, bool toggleSelection);

        ImGuiTableFlags tableFlags_ = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

        int id_;

        std::vector<ImVec2> headList_;

        std::string headOne_ = "종류";
        std::string headTwo_ = "날짜";
        std::string headThree_ = "시간";
        std::string headFour_ = "재생시간";
        std::string headFive_ = "파일명";

        std::vector<bool> columnChecked_;
        int selectionAnchor_ = -1;
        int playingIndex_ = -1;
        std::vector<FileItemRaw> fileList_;

        DoubleClickCallback onDoubleClick_;
        PlayStateProvider playStateProvider_;
    };
} // namespace sdlviewer
