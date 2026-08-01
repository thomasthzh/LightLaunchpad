#include "Persistence.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace llp::persistence
{
namespace
{
constexpr LONGLONG MaxJsonBytes = 16LL * 1024 * 1024;

bool EnsureParentDirectory(const std::wstring& path)
{
    if (path.empty()) return false;
    std::vector<wchar_t> directory(path.begin(), path.end());
    directory.push_back(L'\0');
    if (!PathRemoveFileSpecW(directory.data()) || directory[0] == L'\0') return true;
    const int result = SHCreateDirectoryExW(nullptr, directory.data(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool ReadBytes(const std::wstring& path, std::string& content)
{
    content.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > MaxJsonBytes)
    {
        CloseHandle(file);
        return false;
    }
    content.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr) != FALSE
        && read == content.size();
    CloseHandle(file);
    if (!ok) content.clear();
    return ok;
}

bool ParseObject(const std::string& content, nlohmann::json& document, std::wstring& error)
{
    try
    {
        document = nlohmann::json::parse(content);
        if (!document.is_object())
        {
            error = L"JSON root must be an object.";
            return false;
        }
        return true;
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = Utf8ToWide(exception.what());
        return false;
    }
}

bool LoadObject(const std::wstring& path, nlohmann::json& document, std::wstring& error)
{
    std::string content;
    if (!ReadBytes(path, content))
    {
        error = L"File is missing, empty, unreadable, or too large.";
        return false;
    }
    return ParseObject(content, document, error);
}

std::wstring TimestampSuffix()
{
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    wchar_t buffer[48] = L"";
    std::swprintf(
        buffer,
        sizeof(buffer) / sizeof(buffer[0]),
        L"%04u%02u%02u-%02u%02u%02u-%03u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return buffer;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring QuarantineCorruptFile(const std::wstring& path)
{
    if (!FileExists(path)) return {};
    const std::wstring quarantinePath = path + L".corrupt-" + TimestampSuffix();
    return MoveFileExW(path.c_str(), quarantinePath.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE
        ? quarantinePath
        : std::wstring{};
}

bool WriteBytesFlushed(const std::wstring& path, const std::string& content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) != FALSE
        && written == content.size()
        && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok;
}

std::string SerializeDocument(const nlohmann::json& document)
{
    return document.dump(2, ' ', false, nlohmann::json::error_handler_t::strict) + "\n";
}

bool ReplaceWithFlushedBytes(const std::wstring& path, const std::string& content, const wchar_t* temporarySuffix)
{
    const std::wstring temporaryPath = path + temporarySuffix;
    if (!WriteBytesFlushed(temporaryPath, content))
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    return true;
}
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring NormalizeFolderIdentity(const std::wstring& path)
{
    if (path.empty()) return {};
    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    std::wstring normalized;
    if (required > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1, L'\0');
        if (GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr) > 0)
        {
            normalized = buffer.data();
        }
    }
    if (normalized.empty()) normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    while (normalized.size() > 3 && normalized.back() == L'\\') normalized.pop_back();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return normalized;
}

std::wstring ScopedLayoutPath(const std::wstring& dataDirectory, const std::wstring& launchpadFolder)
{
    const std::wstring identity = NormalizeFolderIdentity(launchpadFolder);
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t ch : identity)
    {
        const uint16_t value = static_cast<uint16_t>(ch);
        hash ^= static_cast<uint8_t>(value & 0xFF);
        hash *= 1099511628211ULL;
        hash ^= static_cast<uint8_t>((value >> 8) & 0xFF);
        hash *= 1099511628211ULL;
    }
    std::wostringstream name;
    name << L"layout-" << std::hex << std::setfill(L'0') << std::setw(16) << hash << L".json";
    std::wstring directory = dataDirectory;
    if (!directory.empty() && directory.back() != L'\\') directory += L'\\';
    directory += L"layouts\\";
    return directory + name.str();
}

LoadResult LoadJsonWithRecovery(const std::wstring& path)
{
    LoadResult result;
    if (path.empty()) return result;
    result.found = FileExists(path);
    const std::wstring backupPath = path + L".bak";
    const bool backupFound = FileExists(backupPath);
    if (!result.found && !backupFound) return result;
    if (result.found && LoadObject(path, result.document, result.error)) return result;

    const std::wstring primaryError = result.error;
    nlohmann::json backupDocument;
    std::wstring backupError;
    if (LoadObject(backupPath, backupDocument, backupError))
    {
        if (result.found)
        {
            result.quarantinePath = QuarantineCorruptFile(path);
            result.quarantinedCorruptPrimary = !result.quarantinePath.empty();
        }
        const bool restoredPrimary = EnsureParentDirectory(path)
            && ReplaceWithFlushedBytes(path, SerializeDocument(backupDocument), L".recovery.tmp");
        result.document = std::move(backupDocument);
        result.recoveredFromBackup = true;
        result.error = primaryError;
        if (!restoredPrimary)
        {
            if (!result.error.empty()) result.error += L" ";
            result.error += L"The backup was loaded, but the primary file could not be restored atomically.";
        }
        return result;
    }

    if (result.found)
    {
        result.quarantinePath = QuarantineCorruptFile(path);
        result.quarantinedCorruptPrimary = !result.quarantinePath.empty();
    }
    if (!result.found || result.error.empty()) result.error = backupError;
    return result;
}

bool WriteJsonAtomicWithBackup(const std::wstring& path, const nlohmann::json& document, std::wstring& error)
{
    error.clear();
    if (path.empty() || !document.is_object())
    {
        error = L"Persistence path is empty or JSON root is not an object.";
        return false;
    }
    if (!EnsureParentDirectory(path))
    {
        error = L"Unable to create the persistence directory.";
        return false;
    }

    const std::string content = SerializeDocument(document);
    const std::wstring temporaryPath = path + L".tmp";
    if (!WriteBytesFlushed(temporaryPath, content))
    {
        DeleteFileW(temporaryPath.c_str());
        error = L"Unable to write and flush the temporary persistence file.";
        return false;
    }

    nlohmann::json verifiedDocument;
    std::wstring verifyError;
    if (!LoadObject(temporaryPath, verifiedDocument, verifyError))
    {
        DeleteFileW(temporaryPath.c_str());
        error = L"Temporary persistence verification failed: " + verifyError;
        return false;
    }

    nlohmann::json existingDocument;
    std::wstring existingError;
    const std::wstring backupPath = path + L".bak";
    const bool existingPrimaryIsValid = LoadObject(path, existingDocument, existingError);
    const nlohmann::json& backupDocument = existingPrimaryIsValid ? existingDocument : verifiedDocument;
    if (!ReplaceWithFlushedBytes(backupPath, SerializeDocument(backupDocument), L".tmp"))
    {
        DeleteFileW(temporaryPath.c_str());
        error = L"Unable to atomically preserve a valid persistence backup.";
        return false;
    }

    if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporaryPath.c_str());
        error = L"Unable to atomically replace the persistence file.";
        return false;
    }
    return true;
}
}
