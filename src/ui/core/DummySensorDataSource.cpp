#include "DummySensorDataSource.h"

#include <algorithm>
#include <functional>
#include <random>

namespace sdlviewer
{
    namespace
    {
        constexpr int kDummySampleCount = 1000;
        constexpr double kBaseLatitude = 37.5665;
        constexpr double kBaseLongitude = 126.9780;
    } // namespace

    void GenerateDummySensorData(
        const std::string &videoPath,
        std::vector<ChartDataRaw> &outChart,
        std::vector<GPSDataRaw> &outGps)
    {
        outChart.clear();
        outGps.clear();
        outChart.reserve(kDummySampleCount);
        outGps.reserve(kDummySampleCount);

        std::mt19937 rng(static_cast<unsigned int>(std::hash<std::string>{}(videoPath)));
        std::normal_distribution<float> gForceNoise(0.0f, 0.2f);
        std::normal_distribution<double> gpsStep(0.0, 0.00005);

        float speed = 0.0f;
        double latitude = kBaseLatitude;
        double longitude = kBaseLongitude;

        for (int i = 0; i < kDummySampleCount; ++i)
        {
            speed = std::clamp(speed + gForceNoise(rng) * 5.0f, 0.0f, 120.0f);
            latitude += gpsStep(rng);
            longitude += gpsStep(rng);

            ChartDataRaw chartItem{};
            chartItem.index = i;
            chartItem.x = gForceNoise(rng);
            chartItem.y = gForceNoise(rng);
            chartItem.z = 1.0f + gForceNoise(rng);
            chartItem.speed = speed;
            chartItem.signal = 1;
            outChart.push_back(chartItem);

            GPSDataRaw gpsItem{};
            gpsItem.index = i;
            gpsItem.latitude = static_cast<float>(latitude);
            gpsItem.longitude = static_cast<float>(longitude);
            gpsItem.speed = speed;
            outGps.push_back(gpsItem);
        }
    }
} // namespace sdlviewer
