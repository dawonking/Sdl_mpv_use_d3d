#include "FileListManager.h"

#include "CustomFileList.h"
#include "media/VideoFileRules.h"
#include "SensorGraph.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace sdlviewer
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string Mp4GroupKey(const char *videoPath)
        {
            const fs::path path = fs::u8path(videoPath);
            const VideoFileInfo info = InspectVideoFileName(path);
            if (info.kind != VideoFileKind::LegacyMp4)
                return {};
            return (path.parent_path() / fs::u8path(info.groupPrefix)).u8string();
        }

        FileType ParseFileType(const std::string &typeStr)
        {
            if (typeStr == "INF")
                return FileType::INF;
            if (typeStr == "PRK")
                return FileType::PRK;
            if (typeStr == "EVT")
                return FileType::EVT;
            if (typeStr == "USR")
                return FileType::USR;
            return FileType::UNKNOWN;
        }

        void CopyField(char *dest, size_t destSize, const std::string &value)
        {
            const size_t count = std::min(destSize - 1, value.size());
            std::memcpy(dest, value.data(), count);
            dest[count] = '\0';
        }
    } // namespace

    FileListManager::FileListManager(CustomFileList *fileListWidget, SensorGraph *sensorGraphWidget)
        : fileListWidget_(fileListWidget), sensorGraphWidget_(sensorGraphWidget)
    {
    }

    void FileListManager::OpenFileList()
    {
        if (!folderPicker_)
        {
            return;
        }

        const std::string videoPath = folderPicker_();
        if (videoPath.empty())
        {
            return;
        }

        ReadFileList(videoPath);
    }

    void FileListManager::ReadFileList(const std::string &rootPath)
    {
        namespace fs = std::filesystem;

        const fs::path root = fs::u8path(rootPath);
        std::vector<FileItemRaw> collected;

        for (const auto &entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const fs::path &path = entry.path();
            const VideoFileInfo info = InspectVideoFileName(path);
            if (info.kind != VideoFileKind::Unsupported &&
                info.kind != VideoFileKind::IgnoredMp4 &&
                (info.kind != VideoFileKind::NumberedMp4 || info.channelIndex == 0))
            {
                collected.push_back(FileItemCreate(
                    path.u8string(),
                    path.string(),
                    path.stem().u8string()));
            }
        }

        std::sort(collected.begin(), collected.end(),
                  [](const FileItemRaw &a, const FileItemRaw &b)
                  {
                      const int cmpDate = std::memcmp(a.date, b.date, sizeof(a.date));
                      if (cmpDate != 0)
                      {
                          return cmpDate < 0;
                      }
                      const int cmpTime = std::memcmp(a.time1, b.time1, sizeof(a.time1));
                      if (cmpTime != 0)
                      {
                          return cmpTime < 0;
                      }
                      return std::strcmp(a.filepath, b.filepath) < 0;
                  });

        ApplyToUi(
            [this, list = std::move(collected)]() mutable
            {
                fileList_ = std::move(list);
                currentIndex_ = -1;
                if (fileListWidget_ != nullptr)
                {
                    fileListWidget_->SetFileList(fileList_);
                }
            });
    }

    FileItemRaw FileListManager::FileItemCreate(
        const std::string &path,
        const std::string &nativePath,
        const std::string &filename)
    {
        FileItemRaw item{};

        const VideoFileInfo info = InspectVideoFileName(fs::u8path(path));
        item.type = static_cast<int32_t>(ParseFileType(info.type));
        CopyField(item.date, sizeof(item.date), info.date);
        CopyField(item.time1, sizeof(item.time1), info.time);
        CopyField(item.time2, sizeof(item.time2), "00:00");
        CopyField(item.filename, sizeof(item.filename), filename);
        CopyField(item.filepath, sizeof(item.filepath), path);
        CopyField(item.nativepath, sizeof(item.nativepath), nativePath);

        return item;
    }

    bool FileListManager::LoadSensorData(int index)
    {
        if (index < 0 || index >= static_cast<int>(fileList_.size()))
        {
            return false;
        }

        chartData_.clear();
        gpsData_.clear();

        if (sensorDataSource_)
        {
            sensorDataSource_(fileList_[index].filepath, chartData_, gpsData_);
        }

        currentIndex_ = index;

        if (fileListWidget_ != nullptr)
        {
            fileListWidget_->SetPlayingIndex(index);
        }

        if (sensorGraphWidget_ != nullptr)
        {
            sensorGraphWidget_->SetSensorData(chartData_);
        }

        if (mapDataCallback_)
        {
            mapDataCallback_(gpsData_);
        }

        return true;
    }

    void FileListManager::PlayNextFile()
    {
        if (fileList_.empty())
        {
            return;
        }

        int nextIndex = currentIndex_ + 1;
        if (nextIndex >= static_cast<int>(fileList_.size()))
        {
            nextIndex = 0;
        }

        LoadSensorData(nextIndex);
    }

    void FileListManager::PlayBackFile()
    {
        const int prevIndex = currentIndex_ - 1;
        if (prevIndex < 0)
        {
            return;
        }

        LoadSensorData(prevIndex);
    }

    int FileListManager::GetAdjacentFileIndex(int direction, bool wrap) const
    {
        const int count = GetFileCount();
        if (count == 0 || (direction != -1 && direction != 1))
        {
            return -1;
        }

        if (currentIndex_ < 0 || currentIndex_ >= count)
        {
            return direction > 0 ? 0 : -1;
        }

        const std::string currentGroup = Mp4GroupKey(fileList_[currentIndex_].filepath);
        int index = currentIndex_;
        for (int visited = 0; visited < count; ++visited)
        {
            index += direction;
            if (index < 0 || index >= count)
            {
                if (!wrap)
                {
                    return -1;
                }
                index = direction > 0 ? 0 : count - 1;
            }

            // SetVideo opens these MP4 channel files together.
            if (currentGroup.empty() || Mp4GroupKey(fileList_[index].filepath) != currentGroup)
            {
                return index;
            }
        }

        return wrap ? (direction > 0 ? 0 : count - 1) : -1;
    }

    std::string FileListManager::GetVideoPath(int index) const
    {
        if (index < 0 || index >= static_cast<int>(fileList_.size()))
        {
            return {};
        }

        return fileList_[index].filepath;
    }

    std::vector<int> FileListManager::GetCheckedIndices() const
    {
        return fileListWidget_ ? fileListWidget_->GetCheckedIndices() : std::vector<int>{};
    }

    void FileListManager::ClearCurrentFile()
    {
        currentIndex_ = -1;
        chartData_.clear();
        gpsData_.clear();
        if (fileListWidget_)
            fileListWidget_->SetPlayingIndex(-1);
        if (sensorGraphWidget_)
            sensorGraphWidget_->SetSensorData(chartData_);
        if (mapDataCallback_)
            mapDataCallback_(gpsData_);
    }

    void FileListManager::RemoveFiles(const std::vector<int> &indices)
    {
        std::vector<bool> remove(fileList_.size(), false);
        bool any = false;
        for (int index : indices)
        {
            if (index >= 0 && index < GetFileCount())
            {
                remove[index] = true;
                any = true;
            }
        }
        if (!any)
            return;

        std::vector<FileItemRaw> remaining;
        remaining.reserve(fileList_.size());
        int nextCurrent = -1;
        for (int index = 0; index < GetFileCount(); ++index)
        {
            if (remove[index])
                continue;
            if (index == currentIndex_)
                nextCurrent = static_cast<int>(remaining.size());
            remaining.push_back(fileList_[index]);
        }
        if (currentIndex_ >= 0 && nextCurrent < 0)
            ClearCurrentFile();
        currentIndex_ = nextCurrent;
        fileList_ = std::move(remaining);
        if (fileListWidget_)
        {
            fileListWidget_->SetFileList(fileList_);
            fileListWidget_->SetPlayingIndex(currentIndex_);
        }
    }

    void FileListManager::ApplyToUi(std::function<void()> task) const
    {
        if (!task)
        {
            return;
        }

        if (uiApplyDispatcher_)
        {
            uiApplyDispatcher_(std::move(task));
        }
        else
        {
            task();
        }
    }

} // namespace sdlviewer
