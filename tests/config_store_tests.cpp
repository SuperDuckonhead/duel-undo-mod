#include "test_support.h"
#include "undo/config_store.h"
#include "undo/runtime_paths.h"
#include <windows.h>
#include <fstream>
#include <thread>
#include <chrono>
namespace fs = std::filesystem;
using undo::ConfigStore;
static std::string bytes(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void write(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; CHECK(f.good()); }
static void awaitFile(const fs::path& p) { for(int i=0; i<1000 && !fs::exists(p); ++i) Sleep(10); CHECK(fs::exists(p)); }
int wmain(int argc, wchar_t** argv) {
    if(argc == 4) {
        const fs::path root = argv[1];
        ConfigStore store(root);
        const auto baseline = store.Load();
        CHECK(baseline.at("nickname") == "original name");
        const std::string key = fs::path(argv[2]).string();
        write(root / (key + ".ready"), "ready");
        awaitFile(root / "go");
        CHECK(store.Save({{key, fs::path(argv[3]).string()}}));
        return 0;
    }
    const auto root = undo::ExecutableRoot() / (L"配置测试-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root);
    const std::string original = "# original unchanged\r\nnickname = original name\r\nsound_volume = 20\r\nmusic_volume = 30\r\n";
    write(root / "system.conf", original);
    write(root / "load-once.conf", "nickname = one-time\n");
    ConfigStore store(root);
    CHECK(store.Load().at("nickname") == "original name");
    CHECK(!fs::exists(root / "system-undo.conf"));
    CHECK(store.Save({}));
    PROCESS_INFORMATION children[2]{};
    const wchar_t* args[] = {L"sound_volume 61", L"music_volume 72"};
    for(int i=0; i<2; ++i) {
        std::wstring command = L"\"" + (undo::ExecutableRoot() / L"config_store_tests.exe").wstring() + L"\" \"" + root.wstring() + L"\" " + args[i];
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &children[i]));
    }
    awaitFile(root / "sound_volume.ready"); awaitFile(root / "music_volume.ready"); write(root / "go", "go");
    for(auto& child : children) { CHECK(WaitForSingleObject(child.hProcess, 15000) == WAIT_OBJECT_0); DWORD code=1; CHECK(GetExitCodeProcess(child.hProcess, &code)); CHECK(code == 0); CloseHandle(child.hThread); CloseHandle(child.hProcess); }
    auto values = store.Load();
    CHECK(values.at("sound_volume") == "61"); CHECK(values.at("music_volume") == "72");
    CHECK(values.at("nickname") == "original name");
    const auto before = bytes(root / "system-undo.conf");
    HANDLE held = CreateFileW((root / "system-undo.conf").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    CHECK(!store.Save({{"nickname", "must not persist"}}));
    CloseHandle(held);
    CHECK(bytes(root / "system-undo.conf") == before);
    for(const auto& p : fs::directory_iterator(root)) CHECK(p.path().filename().wstring().find(L".tmp") == std::wstring::npos);
    CHECK(!store.Save({{"nickname", "bad\ninjection = value"}}));
    CHECK(bytes(root / "system-undo.conf") == before);
    CHECK(bytes(root / "system.conf") == original);
    CHECK(bytes(root / "load-once.conf") == "nickname = one-time\n");
    CHECK(store.Save({{"nickname", "new name"}}));
    CHECK(store.Load().at("nickname") == "new name");
}
