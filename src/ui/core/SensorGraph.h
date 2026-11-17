#pragma once

#include "ChartDataRaw.h"
#include "UiComponentBase.h"
#include "WidgetConfig.h"

#include "implot.h"

#include <functional>
#include <string>
#include <vector>

namespace sdlviewer
{
    class SensorGraph final : public UiComponentBase
    {
    public:
        struct PlaybackPosition
        {
            double positionSec = 0.0;
            double durationSec = 0.0;
            bool isActive = false; // Playing 또는 Pause 상태일 때만 true
        };

        using PlaybackPositionProvider = std::function<PlaybackPosition()>;

        SensorGraph(int id, WidgetConfig *opt);

        void Init() override;
        void Render() override;

        void SetSensorData(const std::vector<ChartDataRaw> &data)
        {
            sensorData_ = data;
        }

        void SetPlaybackPositionProvider(PlaybackPositionProvider provider)
        {
            playbackPositionProvider_ = std::move(provider);
        }

        void ClearPlaybackPositionProvider()
        {
            playbackPositionProvider_ = nullptr;
        }

    private:
        static constexpr double kChartMaxValue = 2.0;
        static constexpr double kChartMarginValue = 0.15;

        int id_;

        ImPlotFlags titleFlags_ = 0;
        ImPlotAxisFlags axisFlags_ = 0;

        const double backgroundLines_[5] = {2.0, 1.0, 0.0, -1.0, -2.0};

        std::vector<ChartDataRaw> sensorData_;
        PlaybackPositionProvider playbackPositionProvider_;

        ImU32 gridColor_ = IM_COL32(48, 48, 48, 255);
        ImVec4 backgroundColor_ = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        float lineThickness_ = 1.0f;
    };
} // namespace sdlviewer
