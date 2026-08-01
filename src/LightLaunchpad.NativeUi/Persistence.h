#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace llp::persistence
{
constexpr int CurrentSchemaVersion = 2;

struct LoadResult
{
    nlohmann::json document;
    bool found = false;
    bool recoveredFromBackup = false;
    bool quarantinedCorruptPrimary = false;
    std::wstring quarantinePath;
    std::wstring error;
};

std::wstring NormalizeFolderIdentity(const std::wstring& path);
std::wstring ScopedLayoutPath(const std::wstring& dataDirectory, const std::wstring& launchpadFolder);
LoadResult LoadJsonWithRecovery(const std::wstring& path);
bool WriteJsonAtomicWithBackup(const std::wstring& path, const nlohmann::json& document, std::wstring& error);
std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);
}
