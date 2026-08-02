#include <windows.h>

#include <string>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    const DWORD markerLength = GetEnvironmentVariableW(L"LLP_FLOW_MARKER", nullptr, 0);
    if (markerLength == 0) return 2;
    std::wstring marker(markerLength, L'\0');
    if (GetEnvironmentVariableW(L"LLP_FLOW_MARKER", marker.data(), markerLength) == 0) return 2;
    marker.resize(markerLength - 1);

    std::wstring executable(32768, L'\0');
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (executableLength == 0 || executableLength >= executable.size()) return 3;
    executable.resize(executableLength);
    executable.push_back(L'\n');

    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, executable.data(), static_cast<int>(executable.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return 4;
    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, executable.data(), static_cast<int>(executable.size()), utf8.data(), utf8Length, nullptr, nullptr);

    HANDLE file = CreateFileW(marker.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 5;
    DWORD written = 0;
    const bool succeeded = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE
        && written == utf8.size();
    FlushFileBuffers(file);
    CloseHandle(file);
    return succeeded ? 0 : 6;
}
