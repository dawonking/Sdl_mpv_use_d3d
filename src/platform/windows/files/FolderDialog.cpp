#include "FolderDialog.h"

#include <Windows.h>
#include <shobjidl.h>

namespace sdlviewer
{
    namespace
    {
        std::string ToUtf8(const std::wstring &wide)
        {
            if (wide.empty())
            {
                return {};
            }

            const int sizeNeeded = WideCharToMultiByte(
                CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            std::string result(sizeNeeded, '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), sizeNeeded, nullptr, nullptr);
            return result;
        }
    } // namespace

    std::string OpenFolderDialog()
    {
        const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initHr))
        {
            return {};
        }

        std::string result;

        IFileOpenDialog *fileDialog = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
            IID_IFileOpenDialog, reinterpret_cast<void **>(&fileDialog));

        if (SUCCEEDED(hr))
        {
            DWORD options = 0;
            fileDialog->GetOptions(&options);
            fileDialog->SetOptions(options | FOS_PICKFOLDERS);

            hr = fileDialog->Show(GetActiveWindow());
            if (SUCCEEDED(hr))
            {
                IShellItem *item = nullptr;
                hr = fileDialog->GetResult(&item);
                if (SUCCEEDED(hr))
                {
                    PWSTR path = nullptr;
                    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (SUCCEEDED(hr))
                    {
                        result = ToUtf8(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            fileDialog->Release();
        }

        CoUninitialize();
        return result;
    }
} // namespace sdlviewer
