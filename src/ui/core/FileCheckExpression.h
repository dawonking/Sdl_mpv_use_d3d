#pragma once

#include <string>

namespace sdlviewer
{
    class FileCheckExpression
    {
    public:
        const std::string CheckFile_regExpAVI = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_([123456]).avi)";
        const std::string CheckFile_regExpAVI_Cut = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_([123456])_([123456]).avi)";
        const std::string CheckFile_regExpAVI_Cut2 = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_([123456])_([C][H][123456]).avi)";
        const std::string CheckFile_regExpAVI_ChannelSuffix = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_[FIR]_[NL].avi)";
        const std::string CheckFile_regExpMP4 = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_([12345]).mp4)";
        const std::string CheckFile_regExpMP4_ChannelSuffix = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_[FIR]_[NL].mp4)";
        const std::string LoadFile_MP4_ChannelSuffix = R"((19|20)\d{2}(0[1-9]|1[012])(0[1-9]|1[0-9]|2[0-9]|3[01])_(0[0-9]|1[0-9]|2[0-3])([0-5][0-9])([0-5][0-9])_([EIPU][NVRS][FTKR])_[FIR]_[NL].mp4)";

        FileCheckExpression() = default;
    };
} // namespace sdlviewer
