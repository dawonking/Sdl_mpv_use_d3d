#pragma once

#include "ChartDataRaw.h"
#include "GPSDataRaw.h"

#include <string>
#include <vector>

namespace sdlviewer
{
    void GenerateDummySensorData(
        const std::string &videoPath,
        std::vector<ChartDataRaw> &outChart,
        std::vector<GPSDataRaw> &outGps);
} // namespace sdlviewer
