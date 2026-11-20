#define wmain updater_entry
#include "../src/update.cpp"
#undef wmain

#include <cassert>

using namespace highfps::update;

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"child") == 0) {
        Sleep(20000);
        return 0;
    }
    assert(argc == 2);
    const std::wstring directory = std::wstring(argv[1]) + L"\\";
    const std::string abcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string dllHash = "5994471abb01112afcc18159f6cc74b4f511b99806da59b3caf5a9c173cacfc5";
    const auto manifestText = std::string(kManifestHeader) + "\n2\n" + abcHash + "\n" + dllHash +
                              "\n" + abcHash + "\n" + abcHash + "\n";
    Manifest manifest;
    assert(ParseManifest(manifestText, manifest) && manifest.revision == 2 && manifest.dllHash == dllHash);
    assert(!ParseManifest(manifestText + "\n", manifest));
    assert(!ParseManifest("isaac-highfps-j510-v1\n0\n" + manifestText.substr(sizeof kManifestHeader + 2), manifest));
    assert(!ParseManifest("isaac-highfps-j510-v1\n999999999999\n" + manifestText.substr(sizeof kManifestHeader + 2), manifest));
    std::string crlf;
    for (const auto character : manifestText) {
        if (character == '\n') crlf += '\r';
        crlf += character;
    }
    assert(ParseManifest(crlf, manifest));
    assert(CheckDue(0, 123) && CheckDue(123, 123 + kCheckInterval));
    assert(!CheckDue(123, 122) && !CheckDue(123, 122 + kCheckInterval));

    const auto ini = directory + kIni;
    const auto game = directory + kGame;
    const auto dll = directory + kDll;
    const auto fixtureGame = directory + L"fixture-game.exe";
    const auto stage = directory + L"staged.dll";
    const auto backup = dll + L".highfps-previous";
    assert(StageDll(game, "abc") && StageDll(dll, "abc") && StageDll(stage, "12345"));
    assert(HashFile(game) == abcHash && HashFile(stage) == dllHash);
    assert(WritePrivateProfileStringW(L"highfps", L"AutoUpdate", L"0", ini.c_str()));
    assert(!Enabled(directory));
    assert(!Apply(directory, stage, manifest, abcHash) && HashFile(dll) == abcHash);
    assert(WritePrivateProfileStringW(L"highfps", L"AutoUpdate", L"1", ini.c_str()));
    assert(Enabled(directory));
    {
        InstallLock lock(directory);
        assert(lock.held && !Apply(directory, stage, manifest, abcHash));
    }
    assert(!Apply(directory, stage, manifest, dllHash) && HashFile(dll) == abcHash);
    auto wrong = manifest;
    wrong.gameHash = dllHash;
    assert(!Apply(directory, stage, wrong, abcHash) && HashFile(dll) == abcHash);
    wrong = manifest;
    wrong.dllHash = abcHash;
    assert(!Apply(directory, stage, wrong, abcHash) && HashFile(dll) == abcHash);
    assert(Apply(directory, stage, manifest, abcHash));
    assert(HashFile(dll) == dllHash && HashFile(backup) == abcHash && HashFile(stage).empty());

    const auto path = directory + kTimestamp;
    {
        Handle state{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        assert(state.Valid() && ReserveCheck(state.value));
        Handle concurrent{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        assert(!concurrent.Valid());
    }
    {
        Handle state{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        assert(state.Valid() && !ReserveCheck(state.value));
    }

    assert(CopyFileW(ModulePath(nullptr).c_str(), fixtureGame.c_str(), TRUE));
    auto command = L"\"" + fixtureGame + L"\" child";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    assert(CreateProcessW(fixtureGame.c_str(), command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process));
    Handle child{process.hProcess};
    Handle childThread{process.hThread};
    FILETIME created, exited, kernel, user;
    assert(GetProcessTimes(child.value, &created, &exited, &kernel, &user));
    assert(SameGameProcess(child.value, Ticks(created), fixtureGame));
    assert(!SameGameProcess(child.value, Ticks(created) + 1, fixtureGame));
    assert(!SameGameProcess(child.value, Ticks(created), game));

    if (GetEnvironmentVariableW(L"HIGHFPS_TEST_GITHUB", nullptr, 0)) {
        HttpHandle session{WinHttpOpen(L"isaac-highfps-test", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        HttpHandle connection{WinHttpConnect(session.value, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
        std::string payload;
        const bool fetched = session.value && connection.value &&
            WinHttpSetTimeouts(session.value, 3000, 4000, 5000, 5000) &&
            Fetch(connection.value,
                  L"/hunchcldev/isaac-highfps/releases/download/v0.13.0/isaac-highfps-0.13.0.zip",
                  kMaxDllSize, payload, child.value);
        printf("GitHub release asset: fetched=%d, bytes=%zu, error=%lu\n",
               fetched, payload.size(), GetLastError());
        assert(fetched && payload.compare(0, 2, "PK") == 0);
    }

    assert(TerminateProcess(child.value, 0));
    assert(WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0);
    puts("Updater manifest, throttle, process identity and transactional replacement passed.");
    return 0;
}
