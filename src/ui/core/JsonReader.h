#pragma once

#include "WidgetConfig.h"
#include "render/RenderTypes.h"

#include <map>
#include <string>

namespace sdlviewer
{
RenderStatus LoadSettingValue(
    std::map<std::string, StartData> &outProgramConfigs,
    std::map<std::string, WidgetConfig> &outTop,
    std::map<std::string, WidgetConfig> &outLeft,
    std::map<std::string, WidgetConfig> &outBottom,
    std::map<std::string, WidgetConfig> &outRight,
    std::map<std::string, WidgetConfig> &outCh);

RenderStatus LoadJsonValue(std::map<std::string, WidgetConfig> &outValue, const std::string &fileName);

RenderStatus ReadSettingFile(const std::string &path, std::map<std::string, WidgetConfig> &outConfig);
RenderStatus ReadSettingFile(const std::string &path, std::map<std::string, StartData> &outConfig);
} // namespace sdlviewer
