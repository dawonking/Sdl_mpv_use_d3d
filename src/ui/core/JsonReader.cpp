#include "JsonReader.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace sdlviewer
{
namespace
{
std::string SettingsDirectory()
{
    return (std::filesystem::current_path() / "resources" / "json").string();
}
} // namespace

RenderStatus ReadSettingFile(const std::string &path, std::map<std::string, WidgetConfig> &outConfig)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return RenderStatus::Error("Setting file not found: " + path);
    }

    outConfig.clear();

    nlohmann::json parsed;
    try
    {
        file >> parsed;
        outConfig = parsed.get<std::map<std::string, WidgetConfig>>();
    }
    catch (const nlohmann::json::exception &e)
    {
        return RenderStatus::Error("Failed to parse " + path + ": " + e.what());
    }

    return RenderStatus::Ok();
}

RenderStatus ReadSettingFile(const std::string &path, std::map<std::string, StartData> &outConfig)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return RenderStatus::Error("Setting file not found: " + path);
    }

    outConfig.clear();

    nlohmann::json parsed;
    try
    {
        file >> parsed;
        outConfig = parsed.get<std::map<std::string, StartData>>();
    }
    catch (const nlohmann::json::exception &e)
    {
        return RenderStatus::Error("Failed to parse " + path + ": " + e.what());
    }

    return RenderStatus::Ok();
}

RenderStatus LoadSettingValue(
    std::map<std::string, StartData> &outProgramConfigs,
    std::map<std::string, WidgetConfig> &outTop,
    std::map<std::string, WidgetConfig> &outLeft,
    std::map<std::string, WidgetConfig> &outBottom,
    std::map<std::string, WidgetConfig> &outRight,
    std::map<std::string, WidgetConfig> &outCh)
{
    const std::string dir = SettingsDirectory() + "/";

    std::vector<std::string> failed;

    RenderStatus globalStatus = ReadSettingFile(dir + "globalSetting.json", outProgramConfigs);
    if (!globalStatus)
    {
        failed.push_back("globalSetting.json");
    }

    RenderStatus topStatus = ReadSettingFile(dir + "topside.json", outTop);
    if (!topStatus)
    {
        failed.push_back("topside.json");
    }

    RenderStatus leftStatus = ReadSettingFile(dir + "leftside.json", outLeft);
    if (!leftStatus)
    {
        failed.push_back("leftside.json");
    }

    RenderStatus bottomStatus = ReadSettingFile(dir + "bottomside.json", outBottom);
    if (!bottomStatus)
    {
        failed.push_back("bottomside.json");
    }

    RenderStatus rightStatus = ReadSettingFile(dir + "rightside.json", outRight);
    if (!rightStatus)
    {
        failed.push_back("rightside.json");
    }

    RenderStatus chStatus = ReadSettingFile(dir + "ChSelectArea.json", outCh);
    if (!chStatus)
    {
        failed.push_back("ChSelectArea.json");
    }

    if (!failed.empty())
    {
        std::string message = "Failed to load setting file(s): ";
        for (size_t i = 0; i < failed.size(); ++i)
        {
            message += (i == 0 ? "" : ", ") + failed[i];
        }
        return RenderStatus::Error(message);
    }

    return RenderStatus::Ok();
}

RenderStatus LoadJsonValue(std::map<std::string, WidgetConfig> &outValue, const std::string &fileName)
{
    return ReadSettingFile(SettingsDirectory() + "/" + fileName, outValue);
}
} // namespace sdlviewer
