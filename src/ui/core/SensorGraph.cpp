#include "SensorGraph.h"

#include <algorithm>

namespace sdlviewer
{
    namespace
    {
        struct SensorSeriesData
        {
            const std::vector<ChartDataRaw> *values;
            float ChartDataRaw::*member;
        };

        ImPlotPoint GetSensorPoint(int index, void *data)
        {
            const SensorSeriesData *series = static_cast<SensorSeriesData *>(data);
            const ChartDataRaw &item = (*series->values)[static_cast<size_t>(index)];
            return ImPlotPoint(item.index, item.*(series->member));
        }
    } // namespace

    SensorGraph::SensorGraph(int id, WidgetConfig *opt)
        : id_(id)
    {
        opt_ = opt;
    }

    void SensorGraph::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = ImVec2(opt_->size.width, opt_->size.height);
        ClearStyles();

        titleFlags_ = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;

        axisFlags_ = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock;
    }

    void SensorGraph::Render()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        const int count = static_cast<int>(sensorData_.size());

        const double xMin = 0.0;
        const double xMax = sensorData_.empty()
                                ? 1.0
                                : std::max(1.0, static_cast<double>(sensorData_.back().index));

        const double dataYMin = -kChartMaxValue;
        const double dataYMax = kChartMaxValue;
        const double plotYMin = dataYMin - kChartMarginValue;
        const double plotYMax = dataYMax + kChartMarginValue;

        const ImVec2 pad(10.0f, 10.0f);

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, pad);
        ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, backgroundColor_);
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, backgroundColor_);

        if (ImPlot::BeginPlot(opt_->objName.c_str(), size_, titleFlags_))
        {
            ImPlot::SetupAxis(ImAxis_X1, nullptr, axisFlags_);
            ImPlot::SetupAxis(ImAxis_Y1, nullptr, axisFlags_);

            ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, plotYMin, plotYMax, ImPlotCond_Always);

            ImPlot::SetupFinish();

            ImDrawList *drawList = ImPlot::GetPlotDrawList();

            ImPlot::PushPlotClipRect();

            for (double y : backgroundLines_)
            {
                const ImVec2 p1 = ImPlot::PlotToPixels(xMin, y);
                const ImVec2 p2 = ImPlot::PlotToPixels(xMax, y);

                drawList->AddLine(p1, p2, gridColor_, lineThickness_);
            }

            ImPlot::PopPlotClipRect();

            if (!sensorData_.empty())
            {
                SensorSeriesData xSeries{&sensorData_, &ChartDataRaw::x};
                SensorSeriesData ySeries{&sensorData_, &ChartDataRaw::y};
                SensorSeriesData zSeries{&sensorData_, &ChartDataRaw::z};

                ImPlot::PlotLineG(
                    "##GX", GetSensorPoint, &xSeries, count,
                    ImPlotSpec(
                        ImPlotProp_LineColor, IM_COL32(228, 69, 69, 255),
                        ImPlotProp_LineWeight, 1.0f));

                ImPlot::PlotLineG(
                    "##GY", GetSensorPoint, &ySeries, count,
                    ImPlotSpec(
                        ImPlotProp_LineColor, IM_COL32(255, 255, 110, 255),
                        ImPlotProp_LineWeight, 1.0f));

                ImPlot::PlotLineG(
                    "##GZ", GetSensorPoint, &zSeries, count,
                    ImPlotSpec(
                        ImPlotProp_LineColor, IM_COL32(30, 134, 255, 255),
                        ImPlotProp_LineWeight, 1.0f));
            }

            if (!sensorData_.empty() && playbackPositionProvider_)
            {
                const PlaybackPosition playback = playbackPositionProvider_();
                if (playback.isActive && playback.durationSec > 0.0)
                {
                    const double ratio = std::clamp(playback.positionSec / playback.durationSec, 0.0, 1.0);
                    const double playheadX = xMin + ((xMax - xMin) * ratio);

                    const ImVec2 p1 = ImPlot::PlotToPixels(playheadX, plotYMin);
                    const ImVec2 p2 = ImPlot::PlotToPixels(playheadX, plotYMax);

                    drawList->AddLine(p1, p2, IM_COL32(255, 255, 255, 255), 1.0f);
                }
            }

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleVar(2);
        ImPlot::PopStyleColor(2);
    }
} // namespace sdlviewer
