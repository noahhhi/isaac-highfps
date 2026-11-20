#pragma once

#include <windows.h>

#include <string>

namespace highfps::update {

constexpr wchar_t kUpdater[] = L"isaac-highfps-update.exe";
constexpr wchar_t kIni[] = L"isaac-highfps.ini";
constexpr wchar_t kDll[] = L"winmm.dll";
constexpr wchar_t kGame[] = L"isaac-ng.exe";

inline std::wstring ModulePath(HMODULE module) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

inline std::wstring DirectoryOf(const std::wstring& path) {
    const auto slash = path.rfind(L'\\');
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1);
}

inline bool EqualPath(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline unsigned long long Ticks(const FILETIME& time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

inline bool Enabled(const std::wstring& directory) {
    const auto ini = directory + kIni;
    return GetPrivateProfileIntW(L"highfps", L"Enabled", 1, ini.c_str()) != 0 &&
           GetPrivateProfileIntW(L"highfps", L"AutoUpdate", 1, ini.c_str()) != 0;
}

}  // namespace highfps::update
