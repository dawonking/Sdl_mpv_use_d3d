#include "VideoFileRules.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace sdlviewer
{
    namespace
    {
        bool ValidRecordingTime(const std::string &date, const std::string &time)
        {
            const int year = std::stoi(date.substr(0, 4));
            const int month = std::stoi(date.substr(4, 2));
            const int day = std::stoi(date.substr(6, 2));
            if (year < 1900 || year > 2099 || month < 1 || month > 12)
                return false;
            const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
            const int limit = monthDays[month - 1] + (month == 2 && leap ? 1 : 0);
            return day >= 1 && day <= limit &&
                   std::stoi(time.substr(0, 2)) < 24 &&
                   std::stoi(time.substr(2, 2)) < 60 &&
                   std::stoi(time.substr(4, 2)) < 60;
        }
    }

    VideoFileInfo InspectVideoFileName(const std::filesystem::path &path)
    {
        VideoFileInfo info;
        info.extension = path.extension().u8string();
        std::transform(info.extension.begin(), info.extension.end(), info.extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (info.extension != ".mp4" && info.extension != ".avi")
            return info;

        info.kind = VideoFileKind::Single;
        const std::string stem = path.stem().u8string();
        static const std::regex recording(
            R"(([0-9]{8})_([0-9]{6})_(INF|PRK|EVT|USR)_([1-6](?:_[1-6]|_CH[1-6])?|[FIR]_[NL]))");
        std::smatch fields;
        const bool validRecording = std::regex_match(stem, fields, recording) &&
                                    ValidRecordingTime(fields[1].str(), fields[2].str());
        if (validRecording)
        {
            const std::string date = fields[1].str();
            const std::string time = fields[2].str();
            info.type = fields[3].str();
            info.date = date.substr(2, 2) + "." + date.substr(4, 2) + "." + date.substr(6, 2);
            info.time = time.substr(0, 2) + ":" + time.substr(2, 2) + ":" + time.substr(4, 2);
        }

        if (info.extension == ".avi")
        {
            if (validRecording && fields[4].length() == 1)
            {
                info.kind = VideoFileKind::MultitrackAvi;
                info.trackCount = fields[4].str()[0] - '0';
            }
            return info;
        }

        // Preserve the existing F_N/F_L/I_N/I_L/R_N/R_L playback convention.
        static const std::regex legacy(R"(([^_]+_[^_]+_[^_]+)_[FIR]_[NL])");
        std::smatch group;
        if (std::regex_match(stem, group, legacy))
        {
            info.kind = VideoFileKind::LegacyMp4;
            info.groupPrefix = group[1].str();
            return info;
        }

        const auto underscore = stem.find_last_of('_');
        if (underscore == std::string::npos)
            return info;
        const std::string suffix = stem.substr(underscore + 1);
        if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(),
                                         [](char ch) { return ch >= '0' && ch <= '9'; }))
            return info;

        info.kind = VideoFileKind::IgnoredMp4;
        // Reject noncanonical numbers without integer conversion (including overflow).
        if (underscore > 0 && suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '6')
        {
            info.kind = VideoFileKind::NumberedMp4;
            info.channelIndex = suffix[0] - '1';
            info.groupPrefix = stem.substr(0, underscore);
        }
        return info;
    }
}
