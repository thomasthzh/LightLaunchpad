#include "Catalog.h"

#include "Persistence.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace llp::catalog
{
namespace
{
using Json = nlohmann::json;

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring JoinPath(const std::wstring& left, const wchar_t* right)
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
    return CompareStringOrdinal(
        value.c_str() + value.size() - suffixLength,
        suffixLength,
        suffix,
        suffixLength,
        TRUE) == CSTR_EQUAL;
}

std::wstring FileStem(const std::wstring& path)
{
    const wchar_t* name = PathFindFileNameW(path.c_str());
    std::wstring stem = name ? name : path;
    const auto dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem.resize(dot);
    return stem;
}

bool SameIdentity(const std::wstring& left, const std::wstring& right)
{
    const auto normalizedLeft = persistence::NormalizeFolderIdentity(left);
    const auto normalizedRight = persistence::NormalizeFolderIdentity(right);
    return CompareStringOrdinal(normalizedLeft.c_str(), -1, normalizedRight.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool IsDirectChild(const std::wstring& sourcePath, const std::wstring& folder)
{
    if (sourcePath.empty() || folder.empty()) return false;
    std::vector<wchar_t> parent(sourcePath.begin(), sourcePath.end());
    parent.push_back(L'\0');
    if (!PathRemoveFileSpecW(parent.data())) return false;
    return SameIdentity(parent.data(), folder);
}

void AppendError(std::wstring& target, const std::wstring& message)
{
    if (message.empty()) return;
    if (target.empty()) target = message;
    else if (target.find(message) == std::wstring::npos) target += L" " + message;
}

bool TryGetInt(const Json& value, int& result)
{
    if (value.is_number_unsigned())
    {
        const auto number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(std::numeric_limits<int>::max())) return false;
        result = static_cast<int>(number);
        return true;
    }
    if (!value.is_number_integer()) return false;
    const auto number = value.get<int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
    result = static_cast<int>(number);
    return true;
}

bool RegionExists(const Snapshot& snapshot, const std::wstring& id)
{
    return std::any_of(snapshot.regions.begin(), snapshot.regions.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, id.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
}

void AddDefaultRegion(Snapshot& snapshot)
{
    if (!RegionExists(snapshot, L"uncategorized"))
    {
        snapshot.regions.push_back({ L"uncategorized", L"Uncategorized", 0 });
    }
}

bool ContainsStableId(const Snapshot& snapshot, const std::wstring& stableId)
{
    const auto contains = [&](const std::vector<Item>& items) {
        return std::any_of(items.begin(), items.end(), [&](const Item& item) {
            return item.stableId == stableId;
        });
    };
    return contains(snapshot.items) || contains(snapshot.missingItems);
}

Json SnapshotDocument(const Snapshot& snapshot)
{
    Json document = {
        { "SchemaVersion", persistence::CurrentSchemaVersion },
        { "FolderIdentity", persistence::WideToUtf8(snapshot.folderIdentity) },
        { "FolderPath", persistence::WideToUtf8(snapshot.folderPath) },
        { "ViewMode", persistence::WideToUtf8(snapshot.viewMode) },
        { "Regions", Json::array() },
        { "Items", Json::array() }
    };
    for (const auto& region : snapshot.regions)
    {
        document["Regions"].push_back({
            { "Id", persistence::WideToUtf8(region.id) },
            { "Name", persistence::WideToUtf8(region.name) },
            { "Order", region.order }
        });
    }
    auto appendItem = [&](const Item& item) {
        document["Items"].push_back({
            { "SourcePath", persistence::WideToUtf8(item.sourcePath) },
            { "DisplayName", persistence::WideToUtf8(item.displayName) },
            { "RegionId", persistence::WideToUtf8(item.regionId) },
            { "Order", item.order }
        });
    };
    for (const auto& item : snapshot.items) appendItem(item);
    for (const auto& item : snapshot.missingItems) appendItem(item);
    return document;
}

void ReadMetadata(const Json& document, Snapshot& snapshot, bool migratedLegacy)
{
    if (document.contains("ViewMode") && document["ViewMode"].is_string())
    {
        const auto viewMode = persistence::Utf8ToWide(document["ViewMode"].get<std::string>());
        if (!viewMode.empty()) snapshot.viewMode = viewMode;
    }

    if (document.contains("Regions") && !document["Regions"].is_array())
    {
        AppendError(snapshot.error, L"Layout field Regions must be an array.");
    }
    else if (document.contains("Regions"))
    {
        for (const auto& entry : document["Regions"])
        {
            if (!entry.is_object() || snapshot.regions.size() >= 1000) continue;
            const auto id = entry.contains("Id") && entry["Id"].is_string()
                ? persistence::Utf8ToWide(entry["Id"].get<std::string>()) : std::wstring{};
            const auto name = entry.contains("Name") && entry["Name"].is_string()
                ? persistence::Utf8ToWide(entry["Name"].get<std::string>()) : std::wstring{};
            int order = static_cast<int>(snapshot.regions.size());
            if (entry.contains("Order") && !TryGetInt(entry["Order"], order))
            {
                AppendError(snapshot.error, L"Layout region Order must be an in-range integer.");
                order = static_cast<int>(snapshot.regions.size());
            }
            if (!id.empty() && !name.empty() && !RegionExists(snapshot, id)) snapshot.regions.push_back({ id, name, order });
        }
    }
    AddDefaultRegion(snapshot);

    if (document.contains("Items") && !document["Items"].is_array())
    {
        AppendError(snapshot.error, L"Layout field Items must be an array.");
        return;
    }
    if (!document.contains("Items")) return;

    for (const auto& entry : document["Items"])
    {
        if (!entry.is_object() || snapshot.items.size() + snapshot.missingItems.size() >= 10000) continue;
        auto sourcePath = entry.contains("SourcePath") && entry["SourcePath"].is_string()
            ? persistence::Utf8ToWide(entry["SourcePath"].get<std::string>()) : std::wstring{};
        if (sourcePath.empty() || !IsDirectChild(sourcePath, snapshot.folderPath)) continue;
        const auto stableId = StableItemId(sourcePath);
        if (ContainsStableId(snapshot, stableId))
        {
            AppendError(snapshot.error, L"Layout contains a duplicate stable source path; the later item was ignored.");
            continue;
        }
        auto displayName = entry.contains("DisplayName") && entry["DisplayName"].is_string()
            ? persistence::Utf8ToWide(entry["DisplayName"].get<std::string>()) : std::wstring{};
        auto regionId = entry.contains("RegionId") && entry["RegionId"].is_string()
            ? persistence::Utf8ToWide(entry["RegionId"].get<std::string>()) : std::wstring{};
        int order = static_cast<int>(snapshot.items.size() + snapshot.missingItems.size());
        if (entry.contains("Order") && !TryGetInt(entry["Order"], order))
        {
            AppendError(snapshot.error, L"Layout item Order must be an in-range integer.");
            order = static_cast<int>(snapshot.items.size() + snapshot.missingItems.size());
        }
        if (displayName.empty()) displayName = FileStem(sourcePath);
        if (regionId.empty() || !RegionExists(snapshot, regionId)) regionId = L"uncategorized";
        Item item;
        item.stableId = stableId;
        item.sourcePath = sourcePath;
        item.displayName = displayName;
        item.regionId = regionId;
        item.order = order;
        if (FileExists(sourcePath)) snapshot.items.push_back(std::move(item));
        else snapshot.missingItems.push_back(std::move(item));
    }
    (void)migratedLegacy;
}

void ScanFolder(Snapshot& snapshot)
{
    if (snapshot.folderPath.empty()) return;
    const int createResult = SHCreateDirectoryExW(nullptr, snapshot.folderPath.c_str(), nullptr);
    if (createResult != ERROR_SUCCESS
        && createResult != ERROR_ALREADY_EXISTS
        && createResult != ERROR_FILE_EXISTS) return;
    const DWORD folderAttributes = GetFileAttributesW(snapshot.folderPath.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES
        || (folderAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return;
    WIN32_FIND_DATAW data = {};
    const auto search = JoinPath(snapshot.folderPath, L"*");
    HANDLE find = FindFirstFileW(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const auto path = JoinPath(snapshot.folderPath, data.cFileName);
        if (!IsLaunchablePath(path)) continue;
        const auto stableId = StableItemId(path);
        if (ContainsStableId(snapshot, stableId)) continue;
        Item item;
        item.stableId = stableId;
        item.sourcePath = path;
        item.displayName = FileStem(path);
        item.regionId = L"uncategorized";
        item.order = static_cast<int>(snapshot.items.size());
        snapshot.items.push_back(std::move(item));
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

void SortSnapshot(Snapshot& snapshot)
{
    std::sort(snapshot.regions.begin(), snapshot.regions.end(), [](const Region& left, const Region& right) {
        if (left.order != right.order) return left.order < right.order;
        return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    auto regionOrder = [&](const std::wstring& id) {
        auto found = std::find_if(snapshot.regions.begin(), snapshot.regions.end(), [&](const Region& region) {
            return CompareStringOrdinal(region.id.c_str(), -1, id.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        return found == snapshot.regions.end() ? 9999 : found->order;
    };
    std::sort(snapshot.items.begin(), snapshot.items.end(), [&](const Item& left, const Item& right) {
        const int leftRegion = regionOrder(left.regionId);
        const int rightRegion = regionOrder(right.regionId);
        if (leftRegion != rightRegion) return leftRegion < rightRegion;
        if (left.order != right.order) return left.order < right.order;
        return CompareStringOrdinal(left.displayName.c_str(), -1, right.displayName.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
}
}

bool IsLaunchablePath(const std::wstring& path)
{
    return EndsWithIgnoreCase(path, L".lnk")
        || EndsWithIgnoreCase(path, L".url")
        || EndsWithIgnoreCase(path, L".exe")
        || EndsWithIgnoreCase(path, L".com");
}

std::wstring StableItemId(const std::wstring& sourcePath)
{
    const auto identity = persistence::NormalizeFolderIdentity(sourcePath);
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t ch : identity)
    {
        const uint16_t value = static_cast<uint16_t>(ch);
        hash ^= static_cast<uint8_t>(value & 0xFF);
        hash *= 1099511628211ULL;
        hash ^= static_cast<uint8_t>((value >> 8) & 0xFF);
        hash *= 1099511628211ULL;
    }
    std::wostringstream result;
    result << L"item-" << std::hex << std::setfill(L'0') << std::setw(16) << hash;
    return result.str();
}

Snapshot LoadSnapshot(const std::wstring& dataDirectory, const std::wstring& launchpadFolder, uint64_t generation)
{
    const ULONGLONG started = GetTickCount64();
    Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.folderPath = launchpadFolder;
    snapshot.folderIdentity = persistence::NormalizeFolderIdentity(launchpadFolder);
    if (dataDirectory.empty())
    {
        AddDefaultRegion(snapshot);
        ScanFolder(snapshot);
        snapshot.loadMilliseconds = GetTickCount64() - started;
        return snapshot;
    }

    const auto scopedPath = persistence::ScopedLayoutPath(dataDirectory, launchpadFolder);
    const auto backupPath = scopedPath + L".bak";
    const bool scopedStateExists = FileExists(scopedPath) || FileExists(backupPath);
    auto loadResult = persistence::LoadJsonWithRecovery(scopedPath);
    bool migratedLegacy = false;
    const auto legacyPath = JoinPath(dataDirectory, L"layout.json");
    if (!loadResult.document.is_object() && !scopedStateExists)
    {
        auto legacyResult = persistence::LoadJsonWithRecovery(legacyPath);
        if (legacyResult.document.is_object())
        {
            loadResult = std::move(legacyResult);
            migratedLegacy = true;
        }
    }
    snapshot.recoveredFromBackup = loadResult.recoveredFromBackup;
    snapshot.quarantinePath = loadResult.quarantinePath;
    if (loadResult.found || loadResult.recoveredFromBackup) snapshot.error = loadResult.error;

    bool documentAccepted = false;
    int schemaVersion = 1;
    if (loadResult.document.is_object())
    {
        const auto& document = loadResult.document;
        if (document.contains("SchemaVersion") && !TryGetInt(document["SchemaVersion"], schemaVersion))
        {
            snapshot.error = L"Layout SchemaVersion must be a supported integer.";
        }
        else if (schemaVersion < 1 || schemaVersion > persistence::CurrentSchemaVersion)
        {
            snapshot.error = L"Unsupported layout schema version " + std::to_wstring(schemaVersion) + L".";
        }
        else if (!migratedLegacy && schemaVersion >= 2
            && (!document.contains("FolderIdentity") || !document["FolderIdentity"].is_string()))
        {
            snapshot.error = L"Schema v2 layout requires a string FolderIdentity.";
        }
        else
        {
            bool identityMatches = true;
            if (!migratedLegacy && document.contains("FolderIdentity") && document["FolderIdentity"].is_string())
            {
                const auto storedIdentity = persistence::Utf8ToWide(document["FolderIdentity"].get<std::string>());
                identityMatches = SameIdentity(storedIdentity, snapshot.folderIdentity);
            }
            if (!identityMatches)
            {
                snapshot.error = L"Scoped layout folder identity does not match the active launchpad folder.";
            }
            else
            {
                ReadMetadata(document, snapshot, migratedLegacy);
                documentAccepted = true;
            }
        }
    }
    if (!documentAccepted) AddDefaultRegion(snapshot);
    ScanFolder(snapshot);
    SortSnapshot(snapshot);

    snapshot.migratedLegacy = documentAccepted
        && (migratedLegacy || schemaVersion < persistence::CurrentSchemaVersion);
    if (snapshot.migratedLegacy)
    {
        std::wstring saveError;
        if (persistence::WriteJsonAtomicWithBackup(scopedPath, SnapshotDocument(snapshot), saveError))
        {
            if (migratedLegacy)
            {
                std::wstring archive = legacyPath + L".migrated-v1";
                if (FileExists(archive)) archive += L"-" + std::to_wstring(GetTickCount64());
                if (!MoveFileExW(legacyPath.c_str(), archive.c_str(), MOVEFILE_WRITE_THROUGH))
                {
                    AppendError(snapshot.error, L"Scoped layout was saved, but the legacy layout could not be archived.");
                }
            }
        }
        else
        {
            AppendError(snapshot.error, saveError);
        }
    }
    snapshot.loadMilliseconds = GetTickCount64() - started;
    return snapshot;
}
}
