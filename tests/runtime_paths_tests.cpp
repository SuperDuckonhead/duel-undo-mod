#include "test_support.h"
#include "undo/runtime_paths.h"
#include <windows.h>
#include <fstream>
namespace fs = std::filesystem;
int main() {
    wchar_t exe[32768];
    CHECK(GetModuleFileNameW(nullptr, exe, 32768));
    const auto expected = fs::path(exe).parent_path();
    fs::current_path(expected.root_path());
    CHECK(undo::ExecutableRoot() == expected);
    const auto root = expected / L"测试目录";
    CHECK(undo::ResourcePath(root, L"pics/100.jpg") == root / L"pics/100.jpg");
    for(const auto* invalid : {L"../outside", L"pics/../../outside", L"C:/outside", L"/outside", L"C:outside"}) {
        bool rejected = false;
        try { undo::ResourcePath(root, invalid); } catch(const std::exception&) { rejected = true; }
        CHECK(rejected);
    }
    const auto args = undo::BotArguments(L"Deck=ChainBurn Name='two words' DeckFile='F:/卡组/test.ydk'", 7912, true, root);
    CHECK(args.size() == 7);
    CHECK(args[0] == L"Deck=ChainBurn"); CHECK(args[1] == L"Name=two words");
    CHECK(args[2] == L"DeckFile=F:/卡组/test.ydk");
    CHECK(args[3] == L"Host=127.0.0.1"); CHECK(args[4] == L"Port=7912");
    CHECK(args[5] == L"Hand=1"); CHECK(args[6] == L"RuntimeRoot=" + root.wstring());
    CHECK(undo::BotArguments(L"Deck=ChainBurn", 7913, false, root)[3] == L"Hand=0");
    undo::AnchorRuntime(expected);
    CHECK(fs::current_path() == expected);
}
