#include "update_manifest.h"
#include "update_release.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <array>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace highfps::update {
namespace {

constexpr wchar_t kManifestPath[] = L"/noahhhi/isaac-highfps/releases/latest/download/j510-update.txt";
constexpr wchar_t kAssetBase[] = L"/noahhhi/isaac-highfps/releases/download/j510-r";
constexpr wchar_t kTimestamp[] = L".isaac-highfps-update.state";
constexpr wchar_t kInstallLock[] = L".isaac-highfps-install.lock";
constexpr size_t kMaxDllSize = 8 * 1024 * 1024;

struct Handle {
    HANDLE value;
    bool Valid() const { return value && value != INVALID_HANDLE_VALUE; }
    ~Handle() { if (Valid()) CloseHandle(value); }
};

struct HttpHandle {
    HINTERNET value;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

struct TempFile {
    std::wstring path;
    ~TempFile() { DeleteFileW(path.c_str()); }
};

struct InstallLock {
    std::wstring path;
    bool held;
    explicit InstallLock(const std::wstring& directory)
        : path(directory + kInstallLock), held(CreateDirectoryW(path.c_str(), nullptr) != 0) {}
    ~InstallLock() { if (held) RemoveDirectoryW(path.c_str()); }
};

struct LogFile {
    FILE* file;
    explicit LogFile(const std::wstring& directory)
        : file(_wfopen((directory + L"isaac-highfps-update.log").c_str(), L"w")) {}
    ~LogFile() { if (file) fclose(file); }
    void Write(const char* message) const {
        if (!file) return;
        fputs(message, file);
        fputc('\n', file);
        fflush(file);
    }
};

unsigned long long Number(const wchar_t* text) {
    if (!*text || wcslen(text) > 19) return 0;
    for (const wchar_t* digit = text; *digit; ++digit)
        if (*digit < L'0' || *digit > L'9') return 0;
    return _wcstoui64(text, nullptr, 10);
}

bool SameGameProcess(HANDLE process, unsigned long long created, const std::wstring& expectedImage) {
    FILETIME started, exited, kernel, user;
    if (!GetProcessTimes(process, &started, &exited, &kernel, &user) || Ticks(started) != created)
        return false;
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return false;
    path.resize(length);
    return EqualPath(path, expectedImage);
}

bool ReserveCheck(HANDLE state) {
    unsigned long long previous = 0;
    DWORD count = 0;
    if (!ReadFile(state, &previous, sizeof previous, &count, nullptr)) return false;
    if (count != 0 && count != sizeof previous) previous = 0;
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    const auto now = Ticks(time);
    if (!CheckDue(previous, now)) return false;
    if (SetFilePointer(state, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
    return WriteFile(state, &now, sizeof now, &count, nullptr) &&
           count == sizeof now && SetEndOfFile(state) && FlushFileBuffers(state);
}

bool TrustedResponse(HINTERNET request) {
    wchar_t finalUrl[8192];
    DWORD length = sizeof finalUrl;
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, finalUrl, &length)) return false;
    const std::wstring_view url(finalUrl);
    for (const std::wstring_view prefix : {L"https://github.com/", L"https://release-assets.githubusercontent.com/",
                                          L"https://objects.githubusercontent.com/"})
        if (url.substr(0, prefix.size()) == prefix) return true;
    return false;
}

bool Fetch(HINTERNET connection, const wchar_t* path, size_t limit, std::string& output, HANDLE game) {
    const auto deadline = GetTickCount64() + 20000;
    HttpHandle request{WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD redirects = 3;
    if (!request.value ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                          &redirects, sizeof redirects) ||
        !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr) || !TrustedResponse(request.value))
        return false;
    DWORD status = 0;
    DWORD statusSize = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) return false;

    std::array<char, 16384> bytes;
    output.clear();
    for (;;) {
        DWORD count = 0;
        if (GetTickCount64() >= deadline || WaitForSingleObject(game, 0) != WAIT_TIMEOUT ||
            !WinHttpReadData(request.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count))
            return false;
        if (count == 0) return !output.empty();
        if (count > limit - output.size()) return false;
        output.append(bytes.data(), count);
    }
}

class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
        DWORD size = 0, written = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                              sizeof size, &written, 0) < 0 || written != sizeof size) return;
        object_.resize(size);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), size, nullptr, 0, 0) < 0)
            hash_ = nullptr;
    }

    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    bool Add(const char* bytes, size_t size) {
        return hash_ && BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)),
                                       static_cast<ULONG>(size), 0) >= 0;
    }

    std::string Finish() {
        unsigned char bytes[32];
        if (!hash_ || BCryptFinishHash(hash_, bytes, sizeof bytes, 0) < 0) return {};
        constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (const auto byte : bytes) {
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15]);
        }
        return hex;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_HASH_HANDLE hash_{};
    std::vector<unsigned char> object_;
};

std::string HashFile(const std::wstring& path) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file.Valid()) return {};
    Sha256 hash;
    std::array<char, 65536> bytes;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
            return {};
        if (count == 0) return hash.Finish();
        if (!hash.Add(bytes.data(), count)) return {};
    }
}

bool StageDll(const std::wstring& path, const std::string& data) {
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD written = 0;
    return file.Valid() && WriteFile(file.value, data.data(), static_cast<DWORD>(data.size()),
                                     &written, nullptr) &&
           written == data.size() && FlushFileBuffers(file.value);
}

bool Apply(const std::wstring& directory, const std::wstring& staged, const Manifest& manifest,
           std::string_view installedHash) {
    InstallLock lock(directory);
    if (!lock.held || !Enabled(directory) || HashFile(directory + kGame) != manifest.gameHash)
        return false;
    const auto dll = directory + kDll;
    if (HashFile(dll) != installedHash || HashFile(staged) != manifest.dllHash) return false;
    const auto backup = dll + L".highfps-previous";
    const auto pendingBackup = backup + L".pending-" + std::to_wstring(GetCurrentProcessId()) +
                               L"-" + std::to_wstring(GetTickCount64());
    if (!CopyFileW(dll.c_str(), pendingBackup.c_str(), TRUE)) return false;
    constexpr DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (!MoveFileExW(staged.c_str(), dll.c_str(), flags)) {
        DeleteFileW(pendingBackup.c_str());
        return false;
    }
    // If the old backup is in use, leave the just-created backup under its unique name.
    MoveFileExW(pendingBackup.c_str(), backup.c_str(), flags);
    return true;
}

void Check(const std::wstring& directory, HANDLE game, unsigned installedRevision) {
    const auto statePath = directory + kTimestamp;
    Handle state{CreateFileW(statePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!state.Valid() || !ReserveCheck(state.value)) return;
    LogFile log(directory);
    HttpHandle session{WinHttpOpen(L"isaac-highfps-j510", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 3000, 4000, 5000, 5000)) {
        log.Write("Update check unavailable. Keeping the installed DLL.");
        return;
    }
    HttpHandle connection{WinHttpConnect(session.value, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    std::string manifestText;
    Manifest manifest;
    if (!connection.value || !Fetch(connection.value, kManifestPath, 1024, manifestText, game) ||
        !ParseManifest(manifestText, manifest)) {
        log.Write("Update manifest unavailable or invalid. Keeping the installed DLL.");
        return;
    }
    if (manifest.revision <= installedRevision) {
        log.Write("The installed revision is current.");
        return;
    }
    if (HashFile(directory + kGame) != manifest.gameHash) {
        log.Write("No update for this game executable.");
        return;
    }
    const auto installedHash = HashFile(directory + kDll);
    if (installedHash.empty() || installedHash == manifest.dllHash) return;

    const auto asset = kAssetBase + std::to_wstring(manifest.revision) + L"/winmm.dll";
    std::string payload;
    if (!Fetch(connection.value, asset.c_str(), kMaxDllSize, payload, game)) {
        log.Write("Update download failed. Keeping the installed DLL.");
        return;
    }
    Sha256 hash;
    if (!hash.Add(payload.data(), payload.size()) || hash.Finish() != manifest.dllHash) {
        log.Write("Update checksum mismatch. Keeping the installed DLL.");
        return;
    }
    TempFile staged{directory + L".isaac-highfps-download-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(GetTickCount64())};
    if (!StageDll(staged.path, payload)) {
        log.Write("Could not stage the verified update. Keeping the installed DLL.");
        return;
    }
    std::string().swap(payload);
    log.Write("Verified update downloaded. Waiting for the game to exit.");
    if (WaitForSingleObject(game, INFINITE) == WAIT_OBJECT_0 &&
        Apply(directory, staged.path, manifest, installedHash))
        log.Write("Update installed with a DLL backup.");
    else
        log.Write("Update not applied. The installed DLL was kept.");
}

}  // namespace
}  // namespace highfps::update

int wmain(int argc, wchar_t** argv) {
    using namespace highfps::update;
    if (argc != 4) return 1;
    const auto pid = Number(argv[1]);
    const auto created = Number(argv[2]);
    const auto revision = Number(argv[3]);
    if (!pid || pid > MAXDWORD || !created || !revision || revision > 999999999) return 1;
    const auto directory = DirectoryOf(ModulePath(nullptr));
    if (directory.empty() || !Enabled(directory)) return 0;
    Handle game{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid))};
    if (!game.Valid() || !SameGameProcess(game.value, created, directory + kGame) ||
        WaitForSingleObject(game.value, 0) != WAIT_TIMEOUT) return 1;
    Check(directory, game.value, static_cast<unsigned>(revision));
    return 0;
}
