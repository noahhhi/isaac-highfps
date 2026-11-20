#pragma once

#include "update_release.h"
#include "version.h"

namespace highfps::update {

inline DWORD WINAPI LaunchAfterStartup(LPVOID module) {
    Sleep(30000);
    const auto dllPath = ModulePath(static_cast<HMODULE>(module));
    const auto directory = DirectoryOf(dllPath);
    if (directory.empty() || !EqualPath(dllPath, directory + kDll) || !Enabled(directory)) return 0;
    const auto updater = directory + kUpdater;
    if (GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES) return 0;

    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    auto command = L"\"" + updater + L"\" " + std::to_wstring(GetCurrentProcessId()) + L" " +
                   std::to_wstring(Ticks(created)) + L" " + std::to_wstring(HIGHFPS_J510_REVISION);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(updater.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, directory.c_str(),
                       &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return 0;
}

}  // namespace highfps::update
