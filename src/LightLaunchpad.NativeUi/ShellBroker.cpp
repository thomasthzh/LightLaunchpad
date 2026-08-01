#include "ShellBroker.h"

#include "Persistence.h"

#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <deque>
#include <mutex>
#include <utility>

#ifndef SHIL_EXTRALARGE
#define SHIL_EXTRALARGE 0x2
#endif

#ifndef SHIL_JUMBO
#define SHIL_JUMBO 0x4
#endif

namespace llp::shell
{
namespace
{
const GUID NativeIID_IImageList = { 0x46eb5926, 0x582e, 0x4017, { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };

enum class RequestKind
{
    Stop,
    Catalog,
    Icon,
    Launch,
    OpenFolder,
    OpenLocation,
    Recycle,
    ImportTree,
    ImportPaths,
    PersistJson
};

struct Request
{
    RequestKind kind = RequestKind::Stop;
    uint64_t requestId = 0;
    uint64_t generation = 0;
    std::wstring dataDirectory;
    std::wstring launchpadFolder;
    std::wstring stableId;
    std::wstring path;
    std::string documentUtf8;
    std::vector<std::wstring> paths;
    int iconSize = 0;
    DWORD testDelayMilliseconds = 0;
};

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureDirectory(const std::wstring& path, HWND owner = nullptr)
{
    if (path.empty()) return false;
    const int createResult = SHCreateDirectoryExW(owner, path.c_str(), nullptr);
    if (createResult != ERROR_SUCCESS
        && createResult != ERROR_ALREADY_EXISTS
        && createResult != ERROR_FILE_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    std::wstring result = left;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') result += L'\\';
    result += right;
    return result;
}

bool EndsWithIgnoreCase(const std::wstring& value, const wchar_t* suffix)
{
    const int suffixLength = lstrlenW(suffix);
    if (static_cast<int>(value.size()) < suffixLength) return false;
    return CompareStringOrdinal(value.c_str() + value.size() - suffixLength, suffixLength, suffix, suffixLength, TRUE) == CSTR_EQUAL;
}

std::wstring FileStem(const std::wstring& path)
{
    const wchar_t* name = PathFindFileNameW(path.c_str());
    std::wstring result = name ? name : path;
    const auto dot = result.find_last_of(L'.');
    if (dot != std::wstring::npos) result.resize(dot);
    return result.empty() ? L"Imported" : result;
}

std::wstring ParentDirectory(const std::wstring& path)
{
    std::vector<wchar_t> buffer(path.begin(), path.end());
    buffer.push_back(L'\0');
    if (!PathRemoveFileSpecW(buffer.data())) return {};
    return buffer.data();
}

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ResolveShortcutTarget(const std::wstring& sourcePath, std::wstring& target)
{
    target.clear();
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) return false;
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr) && file)
    {
        hr = file->Load(sourcePath.c_str(), STGM_READ);
        if (SUCCEEDED(hr))
        {
            wchar_t path[32768] = L"";
            if (SUCCEEDED(link->GetPath(path, ARRAYSIZE(path), nullptr, SLGP_RAWPATH)) && path[0] != L'\0') target = path;
        }
        file->Release();
    }
    link->Release();
    return !target.empty();
}

std::wstring SourceIdentity(const std::wstring& sourcePath)
{
    if (EndsWithIgnoreCase(sourcePath, L".lnk"))
    {
        std::wstring target;
        if (ResolveShortcutTarget(sourcePath, target)) return L"target:" + persistence::NormalizeFolderIdentity(target);
    }
    if (EndsWithIgnoreCase(sourcePath, L".url"))
    {
        wchar_t url[32768] = L"";
        GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", url, ARRAYSIZE(url), sourcePath.c_str());
        if (url[0] != L'\0') return L"url:" + Lower(url);
    }
    if (EndsWithIgnoreCase(sourcePath, L".exe") || EndsWithIgnoreCase(sourcePath, L".com"))
    {
        return L"target:" + persistence::NormalizeFolderIdentity(sourcePath);
    }
    return L"source:" + persistence::NormalizeFolderIdentity(sourcePath);
}

void CollectDestinationIdentities(const std::wstring& folder, std::vector<std::wstring>& identities)
{
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(JoinPath(folder, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const auto path = JoinPath(folder, data.cFileName);
        if (!catalog::IsLaunchablePath(path)) continue;
        identities.push_back(SourceIdentity(path));
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

bool ContainsIdentity(const std::vector<std::wstring>& identities, const std::wstring& identity)
{
    return std::any_of(identities.begin(), identities.end(), [&](const std::wstring& value) {
        return CompareStringOrdinal(value.c_str(), -1, identity.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
}

std::wstring UniqueDestination(const std::wstring& folder, const std::wstring& stem, const std::wstring& extension)
{
    auto destination = JoinPath(folder, stem + extension);
    for (int suffix = 2; FileExists(destination); ++suffix)
    {
        destination = JoinPath(folder, stem + L" (" + std::to_wstring(suffix) + L")" + extension);
    }
    return destination;
}

bool CreateShortcutToExecutable(const std::wstring& sourcePath, const std::wstring& destination)
{
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) return false;
    hr = link->SetPath(sourcePath.c_str());
    const auto workingDirectory = ParentDirectory(sourcePath);
    if (SUCCEEDED(hr) && !workingDirectory.empty()) hr = link->SetWorkingDirectory(workingDirectory.c_str());
    IPersistFile* file = nullptr;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr) && file) hr = file->Save(destination.c_str(), TRUE);
    if (file) file->Release();
    link->Release();
    return SUCCEEDED(hr);
}

bool ImportOne(const std::wstring& sourcePath, const std::wstring& folder, std::vector<std::wstring>& identities)
{
    if (!catalog::IsLaunchablePath(sourcePath) || !FileExists(sourcePath)) return false;
    const auto identity = SourceIdentity(sourcePath);
    if (ContainsIdentity(identities, identity)) return false;
    const auto stem = FileStem(sourcePath);
    bool imported = false;
    if (EndsWithIgnoreCase(sourcePath, L".exe") || EndsWithIgnoreCase(sourcePath, L".com"))
    {
        imported = CreateShortcutToExecutable(sourcePath, UniqueDestination(folder, stem, L".lnk"));
    }
    else
    {
        const auto extension = EndsWithIgnoreCase(sourcePath, L".url") ? L".url" : L".lnk";
        imported = CopyFileW(sourcePath.c_str(), UniqueDestination(folder, stem, extension).c_str(), TRUE) != FALSE;
    }
    if (imported) identities.push_back(identity);
    return imported;
}

void ImportTree(
    const std::wstring& root,
    const std::wstring& folder,
    const std::atomic<bool>& stopping,
    std::vector<std::wstring>& identities,
    int& imported,
    int& failed,
    int& skippedReparsePoints)
{
    if (stopping.load() || root.empty()) return;
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(JoinPath(root, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do
    {
        if (stopping.load()) break;
        if (lstrcmpW(data.cFileName, L".") == 0 || lstrcmpW(data.cFileName, L"..") == 0) continue;
        const auto path = JoinPath(root, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            {
                ImportTree(path, folder, stopping, identities, imported, failed, skippedReparsePoints);
            }
            else skippedReparsePoints++;
            continue;
        }
        if (!catalog::IsLaunchablePath(path)) continue;
        if (ContainsIdentity(identities, SourceIdentity(path))) continue;
        if (ImportOne(path, folder, identities)) imported++;
        else failed++;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

HICON LoadShellImageListIcon(const std::wstring& path, int imageListSize)
{
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_LARGEICON)) return nullptr;
    IImageList* imageList = nullptr;
    HRESULT hr = SHGetImageList(imageListSize, NativeIID_IImageList, reinterpret_cast<void**>(&imageList));
    if (FAILED(hr) || !imageList) return nullptr;
    HICON icon = nullptr;
    hr = imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
    imageList->Release();
    return SUCCEEDED(hr) ? icon : nullptr;
}

HICON LoadItemIcon(const std::wstring& sourcePath, int size)
{
    std::wstring iconPath = sourcePath;
    int iconIndex = 0;
    if (EndsWithIgnoreCase(sourcePath, L".lnk"))
    {
        IShellLinkW* link = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
        if (SUCCEEDED(hr) && link)
        {
            IPersistFile* file = nullptr;
            hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
            if (SUCCEEDED(hr) && file && SUCCEEDED(file->Load(sourcePath.c_str(), STGM_READ)))
            {
                wchar_t resolved[MAX_PATH] = L"";
                if (SUCCEEDED(link->GetIconLocation(resolved, ARRAYSIZE(resolved), &iconIndex)) && resolved[0] != L'\0') iconPath = resolved;
                else
                {
                    wchar_t target[MAX_PATH] = L"";
                    if (SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH)) && target[0] != L'\0') iconPath = target;
                }
            }
            if (file) file->Release();
            link->Release();
        }
    }
    HICON icon = nullptr;
    if (PrivateExtractIconsW(iconPath.c_str(), iconIndex, size, size, &icon, nullptr, 1, 0) > 0 && icon) return icon;
    icon = LoadShellImageListIcon(sourcePath, SHIL_JUMBO);
    if (!icon) icon = LoadShellImageListIcon(sourcePath, SHIL_EXTRALARGE);
    if (icon) return icon;
    SHFILEINFOW info = {};
    return SHGetFileInfoW(sourcePath.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) ? info.hIcon : nullptr;
}

bool LaunchPath(HWND owner, const std::wstring& path)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    info.fMask = SEE_MASK_FLAG_NO_UI;
    return ShellExecuteExW(&info) != FALSE;
}

bool OpenLocation(HWND owner, const std::wstring& sourcePath)
{
    const auto arguments = L"/select,\"" + sourcePath + L"\"";
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = L"explorer.exe";
    info.lpParameters = arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    info.fMask = SEE_MASK_FLAG_NO_UI;
    return ShellExecuteExW(&info) != FALSE;
}

bool IsDirectChild(const std::wstring& path, const std::wstring& folder)
{
    const auto parent = ParentDirectory(path);
    return !parent.empty()
        && CompareStringOrdinal(
            persistence::NormalizeFolderIdentity(parent).c_str(),
            -1,
            persistence::NormalizeFolderIdentity(folder).c_str(),
            -1,
            TRUE) == CSTR_EQUAL;
}

void RecyclePaths(
    const std::wstring& folder,
    const std::vector<std::wstring>& paths,
    std::vector<std::wstring>& affectedPaths,
    int& failed)
{
    IFileOperation* operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOperation, reinterpret_cast<void**>(&operation));
    if (FAILED(hr) || !operation)
    {
        failed = static_cast<int>(paths.size());
        return;
    }
    operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);
    std::vector<std::wstring> queuedPaths;
    for (const auto& path : paths)
    {
        if (!IsDirectChild(path, folder) || !FileExists(path))
        {
            failed++;
            continue;
        }
        IShellItem* item = nullptr;
        hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&item));
        if (SUCCEEDED(hr) && item)
        {
            hr = operation->DeleteItem(item, nullptr);
            item->Release();
        }
        if (SUCCEEDED(hr)) queuedPaths.push_back(path);
        else failed++;
    }
    if (!queuedPaths.empty())
    {
        operation->PerformOperations();
        BOOL aborted = FALSE;
        operation->GetAnyOperationsAborted(&aborted);
        for (const auto& path : queuedPaths)
        {
            if (!FileExists(path)) affectedPaths.push_back(path);
            else failed++;
        }
        if (aborted && affectedPaths.empty() && failed == 0) failed = static_cast<int>(queuedPaths.size());
    }
    operation->Release();
}
}

struct ShellBroker::Impl
{
    HWND targetWindow = nullptr;
    UINT completionMessage = 0;
    HANDLE workEvent = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stopping{ false };
    std::atomic<bool> cancelRequested{ false };
    std::atomic<uint64_t> nextRequestId{ 1 };
    std::atomic<DWORD> workerThreadId{ 0 };
    mutable std::mutex mutex;
    mutable std::mutex activeRequestMutex;
    RequestKind activeRequestKind = RequestKind::Stop;
    std::deque<Request> queue;

    uint64_t Enqueue(Request request)
    {
        if (!thread || stopping.load() || cancelRequested.load()) return 0;
        request.requestId = nextRequestId.fetch_add(1);
        const uint64_t id = request.requestId;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (request.kind == RequestKind::Catalog)
            {
                queue.erase(
                    std::remove_if(queue.begin(), queue.end(), [](const Request& pending) {
                        return pending.kind == RequestKind::Catalog;
                    }),
                    queue.end());
            }
            queue.push_back(std::move(request));
        }
        SetEvent(workEvent);
        return id;
    }

    bool TryPop(Request& request)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        request = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    void Publish(Result* result)
    {
        if (!PostMessageW(targetWindow, completionMessage, 0, reinterpret_cast<LPARAM>(result)))
        {
            if (result->icon) DestroyIcon(result->icon);
            delete result;
        }
    }

    void Process(const Request& request, DWORD threadId, const std::wstring& apartment)
    {
        const ULONGLONG started = GetTickCount64();
        auto* result = new Result();
        result->requestId = request.requestId;
        result->brokerThreadId = threadId;
        result->apartment = apartment;
        switch (request.kind)
        {
        case RequestKind::Catalog:
            result->kind = ResultKind::Catalog;
            for (DWORD delayed = 0; delayed < request.testDelayMilliseconds && !cancelRequested.load(); delayed += 20)
            {
                Sleep(std::min<DWORD>(20, request.testDelayMilliseconds - delayed));
            }
            if (cancelRequested.load())
            {
                delete result;
                return;
            }
            result->snapshot = std::make_shared<const catalog::Snapshot>(
                catalog::LoadSnapshot(request.dataDirectory, request.launchpadFolder, request.generation));
            result->succeeded = true;
            break;
        case RequestKind::Icon:
            result->kind = ResultKind::Icon;
            result->stableId = request.stableId;
            result->iconSize = request.iconSize;
            result->icon = LoadItemIcon(request.path, request.iconSize);
            result->succeeded = result->icon != nullptr;
            if (!result->succeeded) result->error = L"Shell could not load the item icon.";
            break;
        case RequestKind::Launch:
            result->kind = ResultKind::Launch;
            result->stableId = request.stableId;
            result->succeeded = LaunchPath(targetWindow, request.path);
            if (!result->succeeded) result->error = L"Shell launch failed.";
            break;
        case RequestKind::OpenFolder:
        {
            result->kind = ResultKind::OpenFolder;
            result->succeeded = EnsureDirectory(request.path, targetWindow)
                && LaunchPath(targetWindow, request.path);
            if (!result->succeeded) result->error = L"Shell could not create or open the launchpad folder.";
            break;
        }
        case RequestKind::OpenLocation:
            result->kind = ResultKind::OpenLocation;
            result->succeeded = OpenLocation(targetWindow, request.path);
            if (!result->succeeded) result->error = L"Shell could not open the item location.";
            break;
        case RequestKind::Recycle:
            result->kind = ResultKind::Recycle;
            RecyclePaths(request.launchpadFolder, request.paths, result->affectedPaths, result->failed);
            result->succeeded = result->failed == 0;
            if (!result->succeeded) result->error = L"One or more launchpad items could not be moved to the Recycle Bin.";
            break;
        case RequestKind::ImportTree:
        {
            result->kind = ResultKind::ImportTree;
            if (!EnsureDirectory(request.launchpadFolder))
            {
                result->failed = 1;
                result->succeeded = false;
                result->error = L"The launchpad folder could not be created for import.";
                break;
            }
            std::vector<std::wstring> identities;
            CollectDestinationIdentities(request.launchpadFolder, identities);
            ImportTree(
                request.path,
                request.launchpadFolder,
                cancelRequested,
                identities,
                result->imported,
                result->failed,
                result->skippedReparsePoints);
            result->succeeded = result->failed == 0;
            if (!result->succeeded) result->error = L"One or more launchable files could not be imported.";
            break;
        }
        case RequestKind::ImportPaths:
        {
            result->kind = ResultKind::ImportTree;
            if (!EnsureDirectory(request.launchpadFolder))
            {
                result->failed = 1;
                result->succeeded = false;
                result->error = L"The launchpad folder could not be created for import.";
                break;
            }
            std::vector<std::wstring> identities;
            CollectDestinationIdentities(request.launchpadFolder, identities);
            for (const auto& path : request.paths)
            {
                if (cancelRequested.load()) break;
                if (ContainsIdentity(identities, SourceIdentity(path))) continue;
                if (ImportOne(path, request.launchpadFolder, identities)) result->imported++;
                else result->failed++;
            }
            result->succeeded = result->failed == 0;
            if (!result->succeeded) result->error = L"One or more launchable files could not be imported.";
            break;
        }
        case RequestKind::PersistJson:
        {
            result->kind = ResultKind::Persistence;
            try
            {
                const auto document = nlohmann::json::parse(request.documentUtf8);
                result->succeeded = persistence::WriteJsonAtomicWithBackup(request.path, document, result->error);
            }
            catch (const nlohmann::json::exception& exception)
            {
                result->error = L"Queued layout JSON was invalid: " + persistence::Utf8ToWide(exception.what());
                result->succeeded = false;
            }
            break;
        }
        case RequestKind::Stop:
            delete result;
            return;
        }
        result->elapsedMilliseconds = GetTickCount64() - started;
        Publish(result);
    }

    static DWORD WINAPI ThreadProc(void* context)
    {
        auto* self = static_cast<Impl*>(context);
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const std::wstring apartment = SUCCEEDED(initialized) ? L"STA" : L"STA initialization failed";
        const DWORD threadId = GetCurrentThreadId();
        self->workerThreadId.store(threadId);
        if (SUCCEEDED(initialized)) CoEnableCallCancellation(nullptr);
        MSG message = {};
        while (!self->stopping.load())
        {
            const DWORD wait = MsgWaitForMultipleObjects(1, &self->workEvent, FALSE, INFINITE, QS_ALLINPUT);
            if (self->stopping.load()) break;
            if (wait == WAIT_OBJECT_0)
            {
                Request request;
                while (self->TryPop(request))
                {
                    if (request.kind == RequestKind::Stop)
                    {
                        self->stopping.store(true);
                        break;
                    }
                    if (self->cancelRequested.load() && request.kind != RequestKind::PersistJson) continue;
                    {
                        std::lock_guard<std::mutex> lock(self->activeRequestMutex);
                        self->activeRequestKind = request.kind;
                    }
                    self->Process(request, threadId, apartment);
                    {
                        std::lock_guard<std::mutex> lock(self->activeRequestMutex);
                        self->activeRequestKind = RequestKind::Stop;
                    }
                    if (self->stopping.load()) break;
                }
            }
            else if (wait == WAIT_OBJECT_0 + 1)
            {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            else
            {
                break;
            }
        }
        if (SUCCEEDED(initialized))
        {
            CoDisableCallCancellation(nullptr);
            CoUninitialize();
        }
        self->workerThreadId.store(0);
        return 0;
    }
};

ShellBroker::ShellBroker() : impl_(new Impl()) {}

ShellBroker::~ShellBroker()
{
    Stop();
    delete impl_;
}

bool ShellBroker::Start(HWND targetWindow, UINT completionMessage)
{
    if (impl_->thread) return true;
    impl_->targetWindow = targetWindow;
    impl_->completionMessage = completionMessage;
    impl_->stopping.store(false);
    impl_->cancelRequested.store(false);
    impl_->workEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->workEvent) return false;
    impl_->thread = CreateThread(nullptr, 0, Impl::ThreadProc, impl_, 0, nullptr);
    if (!impl_->thread)
    {
        CloseHandle(impl_->workEvent);
        impl_->workEvent = nullptr;
        return false;
    }
    return true;
}

void ShellBroker::Stop()
{
    if (!impl_ || !impl_->thread) return;
    impl_->cancelRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->queue.erase(
            std::remove_if(impl_->queue.begin(), impl_->queue.end(), [](const Request& pending) {
                return pending.kind != RequestKind::PersistJson;
            }),
            impl_->queue.end());
        Request stopRequest;
        stopRequest.kind = RequestKind::Stop;
        impl_->queue.push_back(std::move(stopRequest));
    }
    const DWORD workerThreadId = impl_->workerThreadId.load();
    if (workerThreadId != 0) CoCancelCall(workerThreadId, 0);
    {
        std::lock_guard<std::mutex> lock(impl_->activeRequestMutex);
        if (impl_->activeRequestKind != RequestKind::Stop
            && impl_->activeRequestKind != RequestKind::PersistJson)
        {
            CancelSynchronousIo(impl_->thread);
        }
    }
    SetEvent(impl_->workEvent);
    WaitForSingleObject(impl_->thread, INFINITE);
    CloseHandle(impl_->thread);
    CloseHandle(impl_->workEvent);
    impl_->thread = nullptr;
    impl_->workEvent = nullptr;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queue.clear();
}

bool ShellBroker::IsRunning() const
{
    return impl_ && impl_->thread != nullptr
        && !impl_->stopping.load()
        && !impl_->cancelRequested.load();
}

uint64_t ShellBroker::RequestCatalog(const std::wstring& dataDirectory, const std::wstring& launchpadFolder, uint64_t generation, DWORD testDelayMilliseconds)
{
    Request request;
    request.kind = RequestKind::Catalog;
    request.dataDirectory = dataDirectory;
    request.launchpadFolder = launchpadFolder;
    request.generation = generation;
    request.testDelayMilliseconds = testDelayMilliseconds;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestIcon(const std::wstring& stableId, const std::wstring& sourcePath, int size)
{
    Request request;
    request.kind = RequestKind::Icon;
    request.stableId = stableId;
    request.path = sourcePath;
    request.iconSize = size;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestLaunch(const std::wstring& sourcePath)
{
    Request request;
    request.kind = RequestKind::Launch;
    request.stableId = catalog::StableItemId(sourcePath);
    request.path = sourcePath;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestOpenFolder(const std::wstring& folderPath)
{
    Request request;
    request.kind = RequestKind::OpenFolder;
    request.path = folderPath;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestOpenLocation(const std::wstring& sourcePath)
{
    Request request;
    request.kind = RequestKind::OpenLocation;
    request.path = sourcePath;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestRecycle(const std::wstring& launchpadFolder, const std::vector<std::wstring>& sourcePaths)
{
    Request request;
    request.kind = RequestKind::Recycle;
    request.launchpadFolder = launchpadFolder;
    request.paths = sourcePaths;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestImportTree(const std::wstring& root, const std::wstring& launchpadFolder)
{
    Request request;
    request.kind = RequestKind::ImportTree;
    request.path = root;
    request.launchpadFolder = launchpadFolder;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestImportPaths(const std::vector<std::wstring>& sourcePaths, const std::wstring& launchpadFolder)
{
    Request request;
    request.kind = RequestKind::ImportPaths;
    request.paths = sourcePaths;
    request.launchpadFolder = launchpadFolder;
    return impl_->Enqueue(std::move(request));
}

uint64_t ShellBroker::RequestPersistJson(const std::wstring& path, const std::string& documentUtf8)
{
    Request request;
    request.kind = RequestKind::PersistJson;
    request.path = path;
    request.documentUtf8 = documentUtf8;
    return impl_->Enqueue(std::move(request));
}
}
