#pragma once

#include "ChartDataRaw.h"
#include "FileItemRaw.h"
#include "GPSDataRaw.h"

#include <functional>
#include <string>
#include <vector>

namespace sdlviewer
{
    class CustomFileList;
    class SensorGraph;

    class FileListManager
    {
    public:
        using FolderPicker = std::function<std::string()>;
        using SensorDataSource = std::function<void(
            const std::string &videoPath,
            std::vector<ChartDataRaw> &outChart,
            std::vector<GPSDataRaw> &outGps)>;
        using MapDataCallback = std::function<void(const std::vector<GPSDataRaw> &)>;

        FileListManager(CustomFileList *fileListWidget, SensorGraph *sensorGraphWidget);

        void SetFolderPicker(FolderPicker picker) { folderPicker_ = std::move(picker); }
        void SetSensorDataSource(SensorDataSource source) { sensorDataSource_ = std::move(source); }
        void SetMapDataCallback(MapDataCallback callback) { mapDataCallback_ = std::move(callback); }

        using UiApplyDispatcher = std::function<void(std::function<void()>)>;
        void SetUiApplyDispatcher(UiApplyDispatcher dispatcher) { uiApplyDispatcher_ = std::move(dispatcher); }

        void OpenFileList();
        bool LoadSensorData(int index);
        void PlayNextFile();
        void PlayBackFile();

        std::string GetVideoPath(int index) const;
        int GetFileCount() const { return static_cast<int>(fileList_.size()); }
        int GetCurrentIndex() const { return currentIndex_; }
        int GetAdjacentFileIndex(int direction, bool wrap) const;
        std::vector<int> GetCheckedIndices() const;
        void RemoveFiles(const std::vector<int> &indices);
        void ClearCurrentFile();

    private:
        void ReadFileList(const std::string &rootPath);
        void ApplyToUi(std::function<void()> task) const;

        static FileItemRaw FileItemCreate(
            const std::string &path,
            const std::string &nativePath,
            const std::string &filename);

        CustomFileList *fileListWidget_ = nullptr;
        SensorGraph *sensorGraphWidget_ = nullptr;

        FolderPicker folderPicker_;
        SensorDataSource sensorDataSource_;
        MapDataCallback mapDataCallback_;
        UiApplyDispatcher uiApplyDispatcher_;

        std::vector<FileItemRaw> fileList_;
        std::vector<ChartDataRaw> chartData_;
        std::vector<GPSDataRaw> gpsData_;
        int currentIndex_ = -1;
    };
} // namespace sdlviewer
