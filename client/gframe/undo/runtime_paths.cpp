#include "runtime_paths.h"
#include <stdexcept>
#include <system_error>
#include <vector>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
namespace undo {
std::filesystem::path ExecutableRoot() {
    std::vector<wchar_t> buffer(256);
    for(;;) {
        const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if(!size) throw std::system_error(GetLastError(), std::system_category(), "GetModuleFileNameW");
        if(size < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}
std::filesystem::path ResourcePath(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if(relative.has_root_name() || relative.has_root_directory()) throw std::invalid_argument("Rooted resource suffix");
    for(const auto& part : relative) if(part == "..") throw std::invalid_argument("Resource escapes root");
    return (root / relative).lexically_normal();
}
std::vector<std::wstring> BotArguments(const std::wstring& command, unsigned short port, bool hand, const std::filesystem::path& root) {
    auto quoted = L"WindBot " + command;
    std::replace(quoted.begin(), quoted.end(), L'\'', L'"');
    int count = 0;
    auto parsed = CommandLineToArgvW(quoted.c_str(), &count);
    if(!parsed) throw std::runtime_error("Cannot parse bot command");
    std::vector<std::wstring> arguments(parsed + 1, parsed + count);
    LocalFree(parsed);
    arguments.push_back(L"Host=127.0.0.1");
    arguments.push_back(L"Port=" + std::to_wstring(port));
    arguments.push_back(hand ? L"Hand=1" : L"Hand=0");
    arguments.push_back(L"RuntimeRoot=" + std::filesystem::absolute(root).wstring());
    return arguments;
}
void AnchorRuntime(const std::filesystem::path& root) { std::filesystem::current_path(root); }
}
