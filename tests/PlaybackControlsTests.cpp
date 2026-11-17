#include "media/ControlManager.h"
#include "media/MpvEventThread.h"
#include "media/MpvPlayer.h"
#include "media/PlatformMpvOptions.h"
#include "media/VideoFileRules.h"
#include "ui/core/CustomFileList.h"
#include "ui/core/FileListManager.h"

#include <imgui_internal.h>
#include <mpv/client.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sdlviewer
{
    // Exercise the real player wrapper without opening a window or audio device.
    int ApplyPlatformMpvOptions(mpv_handle *handle)
    {
        for (const auto &option : {std::pair{"vo", "null"}, {"ao", "null"}, {"hwdec", "no"}})
        {
            const int result = mpv_set_option_string(handle, option.first, option.second);
            if (result < 0)
                return result;
        }
        return 0;
    }
}

namespace
{
    using namespace sdlviewer;
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;

    void Check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(double actual, double expected, double tolerance = 0.001)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    class TempPlaylist
    {
    public:
        TempPlaylist()
        {
            const fs::path root = fs::absolute(PLAYBACK_TEST_TEMP_ROOT).lexically_normal();
            fs::create_directories(root);
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                directory_ = root / ("playlist-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                if (fs::create_directory(directory_))
                    return;
            }
            throw std::runtime_error("Could not create an isolated playlist fixture");
        }

        ~TempPlaylist()
        {
            // Remove only files created by this fixture; never recursively delete a path.
            std::error_code error;
            for (const fs::path &file : files_)
                fs::remove(file, error);
            for (const fs::path &directory : subdirectories_)
                fs::remove(directory, error);
            fs::remove(directory_, error);
        }

        fs::path Add(const char *name, const char *folder = nullptr)
        {
            const fs::path parent = (folder ? directory_ / folder : directory_).lexically_normal();
            Check(parent == directory_ || parent.parent_path() == directory_, "Fixture folder escaped its directory");
            if (parent != directory_ && fs::create_directory(parent))
                subdirectories_.push_back(parent);
            const fs::path file = parent / fs::u8path(name);
            Check(file.parent_path() == parent, "Fixture filename escaped its directory");
            files_.push_back(file);
            std::ofstream stream(file, std::ios::binary);
            Check(static_cast<bool>(stream), "Could not create playlist fixture file");
            return file;
        }

        void ReadInto(FileListManager &list) const
        {
            list.SetFolderPicker([path = directory_.u8string()] { return path; });
            list.OpenFileList();
        }

    private:
        fs::path directory_;
        std::vector<fs::path> files_;
        std::vector<fs::path> subdirectories_;
    };

    void AddPlaylist(TempPlaylist &fixture)
    {
        fixture.Add("20261025_120000_INF_1.avi");
        fixture.Add("20261025_120010_INF_F_N.mp4");
        fixture.Add("20261025_120010_INF_I_N.mp4");
        fixture.Add("20261025_120010_INF_R_N.mp4");
        fixture.Add("20261025_120020_INF_1.avi");
    }

    struct ControlSpy
    {
        int pauses = 0;
        int stops = 0;
        int frameDirection = 0;
        double speed = 1.0;
        double volume = -1.0;
        double seekTarget = -1.0;
        bool muted = false;
        bool accept = true;
        std::vector<std::string> opened;
        std::vector<std::string> loaded;

        void Bind(ControlManager &controls)
        {
            MpvControlCallbacks callbacks;
            callbacks.pauseAll = [this] { ++pauses; };
            callbacks.stopAll = [this] { ++stops; loaded.clear(); };
            callbacks.setVideo = [this](const std::string &path)
            {
                opened.push_back(path);
                loaded = accept ? std::vector<std::string>{path} : std::vector<std::string>{};
                return accept;
            };
            callbacks.isFileLoaded = [this](const std::string &path)
            {
                return std::find(loaded.begin(), loaded.end(), path) != loaded.end();
            };
            callbacks.setPlaybackSpeed = [this](double value) { speed = value; return accept; };
            callbacks.setAudio = [this](double value, bool mute)
            {
                volume = value;
                muted = mute;
                return accept;
            };
            callbacks.seekSeconds = [this](double value) { seekTarget = value; return accept; };
            callbacks.stepFrame = [this](int direction) { frameDirection = direction; return accept; };
            controls.SetMpvControlCallbacks(std::move(callbacks));
        }
    };

    void TestFileListSelectionShortcuts()
    {
        struct ContextScope
        {
            ContextScope() { ImGui::CreateContext(); }
            ~ContextScope() { ImGui::DestroyContext(); }
        } context;
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        WidgetConfig config;
        config.objName = "selection-list";
        CustomFileList widget(1, &config);
        std::vector<FileItemRaw> items(100);
        widget.SetFileList(items);
        widget.SetPlayingIndex(50);
        int doubleClicks = 0;
        widget.SetDoubleClickCallback([&](int) { ++doubleClicks; });
        ImGuiTable *table = nullptr;
        ImVec2 textPosition, buttonPosition;
        char text[32] = "edit";
        const auto frame = [&]
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            ImGui::Begin("selection-test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
            ImGui::BeginChild("list-group", ImVec2(760, 360));
            const ImGuiID tableId = ImGui::GetID(config.objName.c_str());
            widget.Render();
            table = ImGui::TableFindByID(tableId);
            ImGui::EndChild();
            ImGui::InputText("text", text, sizeof(text));
            textPosition = ImGui::GetItemRectMin();
            textPosition.x += 10;
            textPosition.y += 5;
            ImGui::Button("other control");
            buttonPosition = ImGui::GetItemRectMin();
            buttonPosition.x += 10;
            buttonPosition.y += 5;
            ImGui::End();
            ImGui::Render();
        };
        const auto click = [&](ImVec2 position, bool shift = false, bool ctrl = false)
        {
            io.AddKeyEvent(ImGuiMod_Shift, shift);
            io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
            io.AddMousePosEvent(position.x, position.y);
            frame();
            // Overlapping row/checkbox items require a settled hover before pressing.
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            io.AddKeyEvent(ImGuiMod_Shift, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };
        const auto rowPosition = [&](int index, bool checkbox = false)
        {
            const float rowHeight = (table->RowPosY2 - table->WorkRect.Min.y -
                                     table->InstanceDataFirst.LastTopHeadersRowHeight) / widget.GetUiList().size();
            const float y = table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                            (index + 0.5f) * rowHeight - table->InnerWindow->Scroll.y;
            const auto &column = table->Columns[checkbox ? 0 : 5];
            return ImVec2((column.WorkMinX + column.WorkMaxX) * 0.5f, y);
        };
        const auto selectAll = [&]
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };

        frame();
        frame();
        selectAll();
        Check(widget.GetCheckedIndices().empty(), "Unfocused list captured Ctrl+A");
        click(rowPosition(2));
        Check(widget.GetCheckedIndices() == std::vector<int>{2}, "Plain row click did not set selection anchor");
        click(rowPosition(4), false, true);
        Check(widget.GetCheckedIndices() == std::vector<int>({2, 4}), "Ctrl click did not add a row");
        click(rowPosition(4), false, true);
        Check(widget.GetCheckedIndices() == std::vector<int>{2}, "Repeated Ctrl click did not toggle off a row");
        click(rowPosition(1), false, true);
        click(rowPosition(2));
        Check(widget.GetCheckedIndices() == std::vector<int>{2}, "Plain click on a selected row did not clear other rows");
        click(rowPosition(5), true);
        Check(widget.GetCheckedIndices() == std::vector<int>({2, 3, 4, 5}), "Forward Shift range failed");
        click(rowPosition(0), true);
        Check(widget.GetCheckedIndices() == std::vector<int>({0, 1, 2}), "Reverse Shift range did not retain anchor or clear old range");
        click(rowPosition(1), true);
        Check(widget.GetCheckedIndices() == std::vector<int>({1, 2}), "Shift range did not shrink");
        click(rowPosition(4, true));
        Check(widget.GetCheckedIndices() == std::vector<int>{4}, "Plain checkbox click did not select only its row");
        click(rowPosition(2, true), false, true);
        Check(widget.GetCheckedIndices() == std::vector<int>({2, 4}), "Ctrl checkbox click did not add a row");
        click(rowPosition(2, true), false, true);
        Check(widget.GetCheckedIndices() == std::vector<int>{4}, "Ctrl checkbox click did not remove only its row");
        click(rowPosition(4));
        click(rowPosition(6, true), true);
        Check(widget.GetCheckedIndices() == std::vector<int>({4, 5, 6}), "Shift checkbox range failed");
        click(rowPosition(6, true), true);
        Check(widget.GetCheckedIndices() == std::vector<int>({4, 5, 6}), "Repeated Shift click toggled range or played file");
        Check(doubleClicks == 0 && widget.GetPlayingIndex() == 50, "Selection changed playback");
        selectAll();
        Check(widget.GetCheckedIndices().size() == items.size(), "Ctrl+A omitted clipped rows");

        click(rowPosition(0), true);
        ImGui::SetScrollY(table->InnerWindow, 600);
        frame();
        frame();
        click(rowPosition(20), true);
        const auto scrolledRange = widget.GetCheckedIndices();
        Check(scrolledRange.size() == 17 && scrolledRange.front() == 4 && scrolledRange.back() == 20,
              "Shift range after scrolling lost clipped anchor");
        ImGui::SetScrollY(table->InnerWindow, 0);
        frame();
        frame();
        const auto range = widget.GetCheckedIndices();
        click(textPosition);
        selectAll();
        Check(widget.GetCheckedIndices() == range, "Text editing Ctrl+A changed list checks");
        click(buttonPosition);
        selectAll();
        Check(widget.GetCheckedIndices() == range, "Sibling control focus changed list checks");

        click(rowPosition(7));
        widget.SetFileList(std::vector<FileItemRaw>(3));
        frame();
        click(rowPosition(1), true);
        Check(widget.GetCheckedIndices() == std::vector<int>{1}, "Replacing list retained stale selection anchor");
        click(rowPosition(0));
        click(rowPosition(0));
        Check(doubleClicks == 1, "Plain double click no longer invoked playback");
        Check(widget.GetCheckedIndices() == std::vector<int>{0}, "Repeated plain click deselected its row");
        widget.SetFileList({});
        frame();
        selectAll();
        Check(widget.GetCheckedIndices().empty(), "Empty list Ctrl+A created a selection");
    }

    void TestDeleteCheckedFiles()
    {
        TempPlaylist fixture;
        std::vector<fs::path> files;
        for (const char *name : {"a.avi", "b.avi", "c.avi", "d.avi", "e.avi"})
            files.push_back(fixture.Add(name));
        WidgetConfig config{};
        CustomFileList widget(0, &config);
        FileListManager list(&widget, nullptr);
        fixture.ReadInto(list);
        ControlManager controls(&list);
        ControlSpy spy;
        spy.Bind(controls);
        controls.DeleteCheckedFiles();
        Check(list.GetFileCount() == 5 && spy.stops == 0, "Unchecked deletion changed the playlist");
        widget.SetChecked(0, true);
        widget.SetChecked(4, true);
        controls.FilelistClickAndPlay(2);
        controls.SetPlayState(PlayState::Playing);
        Check(widget.GetCheckedIndices() == std::vector<int>({0, 4}) && widget.GetPlayingIndex() == 2,
              "Playback changed the user's deletion checkboxes");
        controls.DeleteCheckedFiles();
        Check(list.GetFileCount() == 3 && widget.GetUiList().size() == 3 &&
              list.GetCurrentIndex() == 1 && widget.GetPlayingIndex() == 1 && widget.GetCheckedIndices().empty(),
              "Batch deletion did not remap both playlist copies/current index");
        Check(spy.stops == 0 && controls.GetCurrentFileName() == "c.avi" && controls.GetPlayState() == PlayState::Playing,
              "Deleting other files interrupted playback");
        Check(list.GetVideoPath(0) == files[1].u8string() && list.GetVideoPath(2) == files[3].u8string(),
              "Batch deletion removed the wrong rows");
        controls.PlayPreviousFile();
        controls.SetPlayState(PlayState::Playing);
        Check(spy.opened.back() == files[1].u8string(), "Previous used a stale index after deletion");
        controls.PlayNextFile();
        controls.SetPlayState(PlayState::Playing);
        widget.SetChecked(0, true);
        controls.OnPlaybackEnded();
        Check(spy.opened.back() == files[3].u8string() && widget.GetCheckedIndices() == std::vector<int>{0},
              "EOF used a deleted row or changed deletion checkboxes");
        controls.SetPlayState(PlayState::Playing);
        controls.ToggleLooping();
        controls.OnPlaybackEnded();
        Check(spy.opened.back() == files[1].u8string(), "Loop did not wrap within the remaining playlist");
        controls.ToggleLooping();
        controls.SetPlayState(PlayState::Playing);
        controls.PlayNextFile();
        widget.SetChecked(0, false);
        controls.SetPlayState(PlayState::Pause);
        widget.SetChecked(1, true);
        controls.SetDurationFromMedia(100);
        const auto opens = spy.opened.size();
        bool staleFrame = false;
        controls.PostTaskFromMedia([&]
        {
            staleFrame = true;
            controls.SetPlayState(PlayState::Playing);
        });
        controls.PostTaskFromMedia([&] { controls.OnPlaybackEnded(); });
        controls.DeleteCheckedFiles();
        Check(spy.stops == 1 && controls.GetPlayState() == PlayState::NotLoad && !controls.HasActiveMedia() &&
              controls.GetCurrentFileName().empty() && controls.GetDurationSec() == 0 && list.GetCurrentIndex() == -1,
              "Deleting the paused file did not stop/reset playback");
        controls.ToggleLooping();
        controls.OnPlaybackEnded();
        controls.PostTimeFromWorker(50);
        controls.SetDurationFromMedia(100);
        controls.Update();
        Check(!staleFrame && spy.opened.size() == opens && controls.GetPositionSec() == 0 && controls.GetDurationSec() == 0,
              "Late EOF/time/duration resumed deleted media or restored its timeline");
        controls.PlayNextFile();
        Check(spy.opened.back() == files[1].u8string(), "Explicit next did not start the remaining list");
        Check(controls.GetPlayState() == PlayState::FileLoad, "Expected a loading file for deletion");
        widget.SetChecked(0, true);
        widget.SetChecked(1, true);
        controls.DeleteCheckedFiles();
        Check(spy.stops == 2 && list.GetFileCount() == 0 && widget.GetPlayingIndex() == -1,
              "Deleting all rows/loading file did not stop and empty the list");
        const auto emptyOpens = spy.opened.size();
        controls.PlayNextFile();
        controls.PlayPreviousFile();
        controls.OnPlaybackEnded();
        controls.DeleteCheckedFiles();
        Check(spy.opened.size() == emptyOpens && spy.stops == 2, "Empty playlist attempted playback/deletion");
        for (const auto &path : files)
            Check(fs::exists(path), "List deletion removed a disk file");

        fixture.ReadInto(list);
        Check(list.GetFileCount() == 5, "Folder refresh did not restore the disk entries");
        controls.FilelistClickAndPlay(2);
        controls.SetPlayState(PlayState::Playing);
        controls.PostTaskFromMedia([&] { staleFrame = true; });
        fixture.ReadInto(list);
        Check(list.GetCurrentIndex() == -1, "Expected refreshed list without a current index");
        widget.SetChecked(2, true);
        controls.DeleteCheckedFiles();
        Check(spy.stops == 3 && controls.GetPlayState() == PlayState::NotLoad, "Refreshed list deletion missed the playing path");
        controls.FilelistClickAndPlay(0);
        controls.Update();
        Check(!staleFrame && controls.GetPlayState() == PlayState::FileLoad, "Old queued media task applied to a new file");
        controls.SetPlayState(PlayState::Playing);

        TempPlaylist other;
        other.Add("c.avi");
        fixture.ReadInto(list);
        controls.FilelistClickAndPlay(2);
        controls.SetPlayState(PlayState::Playing);
        other.ReadInto(list);
        widget.SetChecked(0, true);
        controls.DeleteCheckedFiles();
        Check(spy.stops == 3 && controls.GetPlayState() == PlayState::Playing,
              "Same basename in another folder stopped the wrong file");

        TempPlaylist group;
        const auto front = group.Add("20261025_120010_INF_F_N.mp4");
        const auto inside = group.Add("20261025_120010_INF_I_N.mp4");
        const auto rear = group.Add("20261025_120010_INF_R_N.mp4");
        group.ReadInto(list);
        controls.FilelistClickAndPlay(0);
        controls.SetPlayState(PlayState::Playing);
        spy.loaded = {front.u8string(), inside.u8string(), rear.u8string()};
        widget.SetChecked(1, true);
        controls.DeleteCheckedFiles();
        Check(spy.stops == 4 && list.GetFileCount() == 2 && list.GetCurrentIndex() == -1,
              "Deleting a loaded MP4 companion did not stop the group/only remove the checked row");
        Check(fs::exists(inside), "Group deletion removed the companion from disk");
        std::cout << "PASS: checked batch deletion, playback/index separation, active/group/loading stop and disk preservation\n";
    }

    void TestVideoFileRules()
    {
        const auto inspect = [](const char *name) { return InspectVideoFileName(fs::u8path(name)); };
        for (const char *name : {"test.mp4", "test.middle.mp4", "test_1_backup.mp4", "test_.mp4", "test.MP4"})
            Check(inspect(name).kind == VideoFileKind::Single, "Ordinary MP4 was not a single file");
        for (const char *name : {"test_0.mp4", "test_01.mp4", "test_7.mp4", "test_8.mp4", "test_9.mp4",
                                 "test_10.mp4", "test_20.mp4", "test_999999999999999999999999999.mp4", "_1.mp4"})
            Check(inspect(name).kind == VideoFileKind::IgnoredMp4, "Invalid numeric MP4 suffix was accepted");
        for (int i = 1; i <= 6; ++i)
        {
            const auto info = InspectVideoFileName(fs::u8path("my.test_name_" + std::to_string(i) + ".mp4"));
            Check(info.kind == VideoFileKind::NumberedMp4 && info.channelIndex == i - 1 &&
                  info.groupPrefix == "my.test_name", "Numeric MP4 lost its prefix or channel number");
        }
        const auto avi = inspect("20261011_145725_INF_6.avi");
        Check(avi.kind == VideoFileKind::MultitrackAvi && avi.trackCount == 6 &&
              avi.type == "INF" && avi.date == "26.10.11" && avi.time == "14:57:25", "Standard AVI metadata or track count was incorrect");
        for (const char *name : {"test.avi", "test_6.avi", "20261011_145725_INF_7.avi",
                                 "20261011_145725_INF_6_2.avi", "20261011_145725_INF_6_CH2.avi",
                                 "20260230_145725_INF_6.avi", "20261011_245725_INF_6.avi",
                                 "20261011_145725_INT_6.avi"})
            Check(inspect(name).kind == VideoFileKind::Single && inspect(name).trackCount == 1,
                  "Nonstandard AVI incorrectly opened multiple tracks");
        Check(inspect("20240229_235959_EVT_3.AVI").trackCount == 3 &&
              inspect("20250229_235959_EVT_3.avi").date.empty(), "Leap-day validation failed");
        Check(inspect("20261011_145725_INF_F_N.mp4").kind == VideoFileKind::LegacyMp4,
              "Existing MP4 convention was lost");
        Check(inspect("video_1.mkv").kind == VideoFileKind::Unsupported, "Unsupported extension was accepted");
    }

    void TestNumberedPlaylist()
    {
        TempPlaylist fixture;
        std::multiset<std::string> expected;
        const auto addVisible = [&](const char *name, const char *folder = nullptr)
        {
            const auto path = fixture.Add(name, folder);
            expected.insert(path.stem().u8string());
            return path;
        };
        const fs::path representative = addVisible("test_1.mp4");
        for (const char *name : {"test_2.mp4", "test_3.mp4", "test_6.mp4", "test_7.mp4", "test_10.mp4", "test_20.mp4",
                                 "test_01.mp4", "orphan_2.mp4", "orphan_6.mp4", "ignored.txt"})
            fixture.Add(name);
        addVisible("test.mp4");
        addVisible("test.middle.mp4");
        addVisible("test_1_backup.mp4");
        addVisible("test_6.avi");
        addVisible("my_test_name.mp4");
        addVisible("20261011_145725_INF_6.avi");
        addVisible("20261011_145725_INF_1.mp4");
        fixture.Add("20261011_145725_INF_6.mp4");
        addVisible("20260230_145725_INF_6.avi");
        addVisible(u8"한글.영상_1.mp4");
        fixture.Add(u8"한글.영상_3.mp4");
        addVisible("test_1.MP4", "other");
        fixture.Add("test_3.mp4", "other");
        fixture.Add("test_2.mp4", "orphan");

        CustomFileList widget(0, nullptr);
        FileListManager list(&widget, nullptr);
        fixture.ReadInto(list);
        std::multiset<std::string> actual;
        for (const auto &item : widget.GetUiList())
        {
            actual.insert(item.filename);
            Check(item.filepath[0] != '\0' && fs::is_regular_file(fs::u8path(item.filepath)), "Listed file lost its actual path");
            Check(std::string(item.time2) == "00:00", "Playlist duration placeholder changed");
            if (std::string(item.filename).find("20261011_") == 0)
            {
                Check(item.type == static_cast<int>(FileType::INF) && std::string(item.date) == "26.10.11" &&
                      std::string(item.time1) == "14:57:25", "Recognized metadata was lost or time was truncated");
            }
            else
            {
                Check(item.type == static_cast<int>(FileType::UNKNOWN) && item.date[0] == '\0' && item.time1[0] == '\0',
                      "Unclassified file acquired fabricated metadata");
            }
        }
        Check(actual == expected, "Playlist did not contain exactly standalone files and numeric representatives");
        for (int index = 0; index < list.GetFileCount(); ++index)
        {
            Check(list.LoadSensorData(index), "Could not select a representative row");
            Check(list.GetAdjacentFileIndex(1, true) == (index + 1) % list.GetFileCount(), "Numeric navigation skipped or duplicated a group");
        }
        fs::remove(representative);
        fixture.ReadInto(list);
        Check(list.GetFileCount() == static_cast<int>(expected.size()) - 1, "Orphaned channels became independent rows after representative removal");
    }

    void TestPlaylist()
    {
        TempPlaylist fixture;
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        Check(list.GetAdjacentFileIndex(1, true) == -1, "Empty playlist returned a next file");
        AddPlaylist(fixture);
        fixture.ReadInto(list);
        Check(list.GetFileCount() == 5 && list.GetCurrentIndex() == -1, "Playlist did not reset selection");
        Check(list.GetAdjacentFileIndex(1, false) == 0, "Initial next did not select the first file");
        Check(list.GetAdjacentFileIndex(-1, true) == -1, "Initial previous invented a selection");
        Check(list.GetAdjacentFileIndex(0, true) == -1, "Invalid direction was accepted");
        Check(list.LoadSensorData(0), "Could not select first file");
        Check(list.GetAdjacentFileIndex(-1, false) == -1, "Previous crossed the first-file boundary");
        Check(list.GetAdjacentFileIndex(-1, true) == 4, "Previous wrap did not select last file");
        for (int index = 1; index <= 3; ++index)
        {
            Check(list.LoadSensorData(index), "Could not select MP4 channel file");
            Check(list.GetAdjacentFileIndex(1, false) == 4, "Next replayed another channel in the same MP4 group");
            Check(list.GetAdjacentFileIndex(-1, false) == 0, "Previous replayed another channel in the same MP4 group");
        }
        Check(list.LoadSensorData(4), "Could not select last file");
        Check(list.GetAdjacentFileIndex(1, false) == -1, "Next crossed the last-file boundary");
        Check(list.GetAdjacentFileIndex(1, true) == 0, "Next wrap did not select first file");
        Check(!list.LoadSensorData(5) && list.GetCurrentIndex() == 4, "Invalid selection changed current file");
    }

    void TestSingleRecording()
    {
        for (const bool mp4 : {false, true})
        {
            TempPlaylist fixture;
            if (mp4)
            {
                fixture.Add("20261025_120010_INF_F_N.mp4");
                fixture.Add("20261025_120010_INF_I_N.mp4");
                fixture.Add("20261025_120010_INF_R_N.mp4");
            }
            else
                fixture.Add("20261025_120000_INF_1.avi");
            FileListManager list(nullptr, nullptr);
            fixture.ReadInto(list);
            for (int index = 0; index < list.GetFileCount(); ++index)
            {
                Check(list.LoadSensorData(index), "Could not select the sole recording");
                for (const int direction : {-1, 1})
                {
                    Check(list.GetAdjacentFileIndex(direction, false) == -1, "Single recording crossed a non-looping boundary");
                    const int wrapped = list.GetAdjacentFileIndex(direction, true);
                    Check(wrapped >= 0 && wrapped < list.GetFileCount(), "Single recording could not wrap to itself");
                }
            }
            ControlSpy spy;
            ControlManager controls(&list);
            spy.Bind(controls);
            controls.FilelistClickAndPlay(0);
            controls.SetPlayState(PlayState::Playing);
            controls.OnPlaybackEnded();
            Check(spy.opened.size() == 1 && controls.GetPlayState() == PlayState::NotLoad, "Sole recording did not stop with loop off");
            controls.ToggleLooping();
            controls.FilelistClickAndPlay(0);
            controls.SetPlayState(PlayState::Playing);
            controls.OnPlaybackEnded();
            Check(spy.opened.size() == 3 && list.GetCurrentIndex() == 0, "Sole recording did not restart with loop on");
        }
    }

    void TestDeferredPlaylistUpdate()
    {
        TempPlaylist fixture;
        fixture.Add("20261025_120000_INF_1.avi");
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        Check(list.LoadSensorData(0), "Could not select the old playlist");
        std::function<void()> pending;
        list.SetUiApplyDispatcher([&](std::function<void()> task) { pending = std::move(task); });
        fixture.Add("20261025_120010_INF_1.avi");
        fixture.ReadInto(list);
        Check(pending && list.GetFileCount() == 1 && list.GetCurrentIndex() == 0, "Queued refresh changed list or selection before UI dispatch");
        pending();
        Check(list.GetFileCount() == 2 && list.GetCurrentIndex() == -1, "UI dispatch did not apply list and selection reset together");
    }

    void TestSameTimeFolderGroups()
    {
        TempPlaylist fixture;
        for (const char *name : {"20261025_120010_INF_F_N.mp4", "20261025_120010_INF_I_N.mp4", "20261025_120010_INF_R_N.mp4"})
        {
            fixture.Add(name, "B");
            fixture.Add(name, "A");
        }
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        Check(list.GetFileCount() == 6, "Two-folder MP4 fixture was not loaded");
        for (int index = 0; index < 6; ++index)
            Check(fs::u8path(list.GetVideoPath(index)).parent_path().filename() == (index < 3 ? "A" : "B"), "Same-time folder groups were interleaved or not ordered by path");
        ControlSpy spy;
        ControlManager controls(&list);
        spy.Bind(controls);
        controls.PlayNextFile();
        Check(list.GetCurrentIndex() == 0, "Next did not start with folder A");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayNextFile();
        Check(list.GetCurrentIndex() == 3, "Next did not skip from folder A to folder B");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayNextFile();
        Check(spy.opened.size() == 2 && list.GetCurrentIndex() == 3, "Next replayed a same-time folder group after the final recording");
    }

    void TestCurrentFileName()
    {
        TempPlaylist fixture;
        AddPlaylist(fixture);
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        ControlSpy spy;
        ControlManager controls(&list);
        spy.Bind(controls);

        Check(controls.GetCurrentFileName().empty(), "Startup exposed a file name");
        controls.FilelistClickAndPlay(0);
        Check(controls.GetCurrentFileName() == "20261025_120000_INF_1.avi", "Accepted load did not retain the file name with extension");
        controls.SetPlayState(PlayState::Playing);
        controls.TogglePlayback();
        Check(controls.GetCurrentFileName() == "20261025_120000_INF_1.avi", "Pause cleared the current file name");
        controls.PlayNextFile();
        Check(controls.GetCurrentFileName() == "20261025_120010_INF_F_N.mp4", "Next file did not change the title");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayPreviousFile();
        Check(controls.GetCurrentFileName() == "20261025_120000_INF_1.avi", "Previous file did not change the title");
        controls.SetPlayState(PlayState::Playing);
        fixture.ReadInto(list);
        Check(list.GetCurrentIndex() == -1 && controls.GetCurrentFileName() == "20261025_120000_INF_1.avi", "Refreshing the playlist lost the currently playing title");
        controls.StopCallBack();
        Check(controls.GetCurrentFileName().empty(), "Stop did not clear the title");

        spy.accept = false;
        controls.FilelistClickAndPlay(4);
        Check(controls.GetCurrentFileName().empty(), "Rejected load exposed a file name");
        spy.accept = true;
        controls.FilelistClickAndPlay(4);
        controls.SetPlayState(PlayState::Playing);
        controls.OnPlaybackEnded();
        Check(controls.GetCurrentFileName().empty(), "End of the playlist retained the title");
        controls.ToggleLooping();
        controls.FilelistClickAndPlay(4);
        controls.SetPlayState(PlayState::Playing);
        controls.OnPlaybackEnded();
        Check(controls.GetCurrentFileName() == "20261025_120000_INF_1.avi", "Loop restart retained the previous title");
        controls.SetPlayState(PlayState::NotLoad);
        Check(controls.GetCurrentFileName().empty(), "Asynchronous load failure retained the title");
    }

    void TestControls()
    {
        TempPlaylist fixture;
        AddPlaylist(fixture);
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        ControlSpy spy;
        ControlManager controls(&list);
        spy.Bind(controls);

        Check(!controls.IsAudioInitialized() && Near(controls.GetVolumeRatio(), 0), "Initial volume was not uninitialized zero");
        controls.TogglePlayback();
        Check(spy.pauses == 0, "Playback toggle operated without a loaded file");
        controls.FilelistClickAndPlay(0);
        Check(controls.IsAudioInitialized() && Near(controls.GetVolumeRatio(), 1), "First load did not initialize full volume");
        Check(Near(spy.volume, 100) && !spy.muted, "First load did not apply initial audio to media");
        controls.PlayNextFile();
        controls.TogglePlayback();
        Check(spy.opened.size() == 1 && spy.pauses == 0, "Loading accepted another playback request");
        controls.SetPlayState(PlayState::Playing);
        controls.TogglePlayback();
        Check(controls.GetPlayState() == PlayState::Pause && spy.pauses == 1, "Playback toggle did not pause");
        controls.TogglePlayback();
        Check(controls.GetPlayState() == PlayState::Playing && spy.pauses == 2, "Playback toggle did not resume");

        for (const auto speed : {PlaybackSpeed::Half, PlaybackSpeed::Normal, PlaybackSpeed::OnePointFive, PlaybackSpeed::Double})
        {
            controls.SetPlaybackSpeed(speed);
            Check(controls.GetPlaybackSpeed() == speed && Near(spy.speed, ToPlaybackRate(speed)), "Playback speed did not reach media");
        }
        controls.SetVolume(0.35f);
        Check(Near(spy.volume, 35) && !spy.muted, "Slider value did not reach media volume");
        controls.ToggleMute();
        Check(controls.IsMuted() && spy.muted, "Mute did not reach media");
        controls.ToggleMute();
        Check(!controls.IsMuted() && Near(controls.GetVolumeRatio(), 0.35) && Near(spy.volume, 35), "Unmute lost the previous volume");
        controls.SetVolume(0);
        Check(controls.IsMuted() && spy.muted, "Zero slider volume did not mute");
        controls.ToggleMute();
        Check(Near(controls.GetVolumeRatio(), 0.35) && !spy.muted, "Unmute from zero did not restore volume");
        controls.FilelistClickAndPlay(4);
        Check(controls.GetPlaybackSpeed() == PlaybackSpeed::Double && Near(spy.speed, 2), "Opening another file reset playback speed");
        Check(Near(controls.GetVolumeRatio(), 0.35), "Opening another file reset volume");

        spy.accept = false;
        controls.SetPlaybackSpeed(PlaybackSpeed::Half);
        controls.SetVolume(0.8f);
        Check(controls.GetPlaybackSpeed() == PlaybackSpeed::Double && Near(controls.GetVolumeRatio(), 0.35), "Rejected media command changed the displayed state");
        spy.accept = true;

        controls.SetDurationFromMedia(2.0);
        controls.SetPlayState(PlayState::Pause);
        controls.PostTimeFromWorker(0.01);
        controls.Update();
        controls.StepPlayback(-1);
        Check(Near(spy.seekTarget, 0) && controls.GetPlayState() == PlayState::Pause, "Backward seconds step did not clamp or preserve pause");
        controls.OnSeekCompleted();
        controls.SetPlayState(PlayState::Playing);
        controls.PostTimeFromWorker(1.99);
        controls.Update();
        controls.StepPlayback(1);
        Check(Near(spy.seekTarget, 2) && controls.GetPlayState() == PlayState::Playing, "Forward seconds step did not clamp or preserve playback");
        controls.OnSeekCompleted();
        controls.ToggleSeekMode();
        Check(controls.IsFrameMode(), "Frame mode did not activate");
        controls.StepPlayback(1);
        Check(spy.frameDirection == 1 && controls.GetPlayState() == PlayState::Pause, "Forward frame step did not pause playback");
        controls.StepPlayback(-1);
        Check(spy.frameDirection == -1 && controls.GetPlayState() == PlayState::Pause, "Backward frame step did not stay paused");
    }

    void TestNavigationAndLoop()
    {
        TempPlaylist fixture;
        AddPlaylist(fixture);
        FileListManager list(nullptr, nullptr);
        fixture.ReadInto(list);
        ControlSpy spy;
        ControlManager controls(&list);
        spy.Bind(controls);
        Check(!controls.IsLooping(), "Loop was enabled by default");
        controls.PlayNextFile();
        Check(list.GetCurrentIndex() == 0 && spy.opened.size() == 1, "Initial next did not open first video");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayPreviousFile();
        Check(spy.opened.size() == 1, "Previous wrapped while loop was off");
        controls.PlayNextFile();
        Check(list.GetCurrentIndex() == 1 && spy.opened.size() == 2, "Next did not open the next recording");
        controls.SetPlayState(PlayState::Playing);
        controls.OnPlaybackEnded();
        Check(list.GetCurrentIndex() == 4 && spy.opened.size() == 3, "EOF replayed another MP4 channel instead of next recording");
        controls.SetPlayState(PlayState::Playing);
        controls.OnPlaybackEnded();
        Check(spy.opened.size() == 3 && controls.GetPlayState() == PlayState::NotLoad, "Final EOF did not stop with loop off");
        controls.ToggleLooping();
        Check(controls.IsLooping(), "Loop mode did not activate");
        controls.FilelistClickAndPlay(4);
        controls.SetPlayState(PlayState::Playing);
        controls.OnPlaybackEnded();
        Check(list.GetCurrentIndex() == 0, "Final EOF did not wrap with loop on");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayPreviousFile();
        Check(list.GetCurrentIndex() == 4, "Previous did not wrap with loop on");
        controls.SetPlayState(PlayState::Playing);
        controls.PlayNextFile();
        Check(list.GetCurrentIndex() == 0, "Next did not wrap with loop on");
        controls.SetPlayState(PlayState::Playing);
        spy.accept = false;
        controls.FilelistClickAndPlay(4);
        Check(controls.GetPlayState() == PlayState::NotLoad, "Failed load left the controls stuck loading");
    }

    template <typename Predicate>
    void WaitUntil(Predicate predicate, const char *message)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!predicate())
        {
            Check(std::chrono::steady_clock::now() < deadline, message);
            std::this_thread::sleep_for(10ms);
        }
    }

    double ReadDouble(mpv_handle *handle, const char *name)
    {
        double value = std::numeric_limits<double>::quiet_NaN();
        mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &value);
        return value;
    }

    int ReadFlag(mpv_handle *handle, const char *name)
    {
        int value = -1;
        mpv_get_property(handle, name, MPV_FORMAT_FLAG, &value);
        return value;
    }

    void TestMedia(const char *filename)
    {
        Check(fs::is_regular_file(fs::u8path(filename)), "Media fixture does not exist");
        MpvPlayer player(0);
        const auto status = player.Initialize();
        if (!status)
            throw std::runtime_error(status.Message());
        std::atomic<int> eofCount{0}, stopCount{0}, commandError{0};
        std::atomic<double> observedTime{-1.0};
        MpvPlaybackCallbacks callbacks;
        callbacks.onTimePosition = [&](double value) { observedTime.store(value); };
        callbacks.onPlaybackEnded = [&](int reason, int error)
        {
            if (error < 0)
                commandError.store(error);
            if (reason == MPV_END_FILE_REASON_EOF)
                ++eofCount;
            if (reason == MPV_END_FILE_REASON_STOP)
                ++stopCount;
        };
        callbacks.onCommandError = [&](int error) { commandError.store(error); };
        MpvEventThread events(player.Handle(), std::move(callbacks));
        events.Start();
        Check(static_cast<bool>(player.SetPlaybackSpeed(1.5)), "Could not set speed before load");
        Check(static_cast<bool>(player.OpenFile(filename, 1)), "Could not queue media fixture");
        WaitUntil([&] { return ReadDouble(player.Handle(), "duration") >= 3.0 && std::isfinite(ReadDouble(player.Handle(), "time-pos")); }, "Fixture must contain at least three seconds of video");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "speed"), 1.5); }, "File load discarded the selected speed");
        player.TogglePause();
        WaitUntil([&] { return ReadFlag(player.Handle(), "pause") == 1; }, "Pause command did not reach mpv");

        for (double rate : {0.5, 1.0, 1.5, 2.0})
        {
            Check(static_cast<bool>(player.SetPlaybackSpeed(rate)), "Could not queue playback speed");
            WaitUntil([&] { return Near(ReadDouble(player.Handle(), "speed"), rate); }, "Selected speed did not reach mpv");
        }

        Check(static_cast<bool>(player.SetAudio(37, true)), "Could not queue volume/mute");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "volume"), 37) && ReadFlag(player.Handle(), "mute") == 1; }, "Volume/mute command did not reach mpv");
        Check(static_cast<bool>(player.SetAudio(73, false)), "Could not queue unmute");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "volume"), 73) && ReadFlag(player.Handle(), "mute") == 0; }, "Unmute command did not reach mpv");
        Check(static_cast<bool>(player.SeekSeconds(1.0)), "Could not queue exact seconds seek");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "time-pos"), 1.0, 0.08); }, "Exact seconds seek did not reach its target");
        Check(ReadFlag(player.Handle(), "pause") == 1, "Seconds seek resumed paused playback");
        const double beforeStep = ReadDouble(player.Handle(), "time-pos");
        Check(static_cast<bool>(player.StepFrame(1)), "Could not queue forward frame step");
        WaitUntil([&] { return ReadDouble(player.Handle(), "time-pos") > beforeStep + 0.001; }, "Forward frame step did not advance video");
        const double afterStep = ReadDouble(player.Handle(), "time-pos");
        WaitUntil([&] { return Near(observedTime.load(), afterStep); }, "Frame position was not forwarded to the UI callback");
        Check(ReadFlag(player.Handle(), "pause") == 1, "Frame step did not remain paused");
        Check(static_cast<bool>(player.StepFrame(-1)), "Could not queue backward frame step");
        WaitUntil([&] { return ReadDouble(player.Handle(), "time-pos") < afterStep - 0.001; }, "Backward frame step did not move video back");
        Check(static_cast<bool>(player.SeekSeconds(-10)), "Could not queue clamped seek");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "time-pos"), 0, 0.08); }, "Negative seconds seek did not clamp to zero");
        Check(!player.SetPlaybackSpeed(0) && !player.StepFrame(0) && !player.SetAudio(std::numeric_limits<double>::quiet_NaN(), false), "Invalid media control value was accepted");

        player.Stop();
        WaitUntil([&] { return stopCount.load() == 1; }, "Stop event was not forwarded");
        Check(eofCount.load() == 0, "Explicit stop was reported as natural EOF");
        Check(static_cast<bool>(player.SetPlaybackSpeed(2)), "Could not queue double speed");
        Check(static_cast<bool>(player.OpenFile(filename, 1)), "Could not reopen media fixture");
        WaitUntil([&] { return std::isfinite(ReadDouble(player.Handle(), "time-pos")) && ReadFlag(player.Handle(), "pause") == 0; }, "Reopened video did not resume");
        WaitUntil([&] { return Near(ReadDouble(player.Handle(), "speed"), 2); }, "Reopened video lost playback speed");
        const double duration = ReadDouble(player.Handle(), "duration");
        Check(static_cast<bool>(player.SeekSeconds(duration - 0.25)), "Could not seek near EOF");
        WaitUntil([&] { return eofCount.load() == 1; }, "Natural EOF was not forwarded");
        Check(commandError.load() == 0, "mpv reported an asynchronous media command error");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestVideoFileRules();
        TestFileListSelectionShortcuts();
        TestDeleteCheckedFiles();
        TestNumberedPlaylist();
        TestPlaylist();
        TestSingleRecording();
        TestDeferredPlaylistUpdate();
        TestSameTimeFolderGroups();
        TestCurrentFileName();
        TestControls();
        TestNavigationAndLoop();
        std::cout << "PASS: playback controls, playlist groups, navigation and loop\n";
        if (argc > 1)
        {
            TestMedia(argv[1]);
            std::cout << "PASS: real mpv speed, audio, pause, seek, frame step and EOF\n";
        }
        else
        {
            std::cout << "NOT TESTED: real mpv playback (pass a video fixture path)\n";
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
