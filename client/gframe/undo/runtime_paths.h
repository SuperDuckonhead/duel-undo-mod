#pragma once
#include <filesystem>
#include <vector>
#include <string>
namespace undo {
std::filesystem::path ExecutableRoot();
std::filesystem::path ResourcePath(const std::filesystem::path& root, const std::filesystem::path& relative);
std::vector<std::wstring> BotArguments(const std::wstring& command, unsigned short port, bool hand, const std::filesystem::path& root);
void AnchorRuntime(const std::filesystem::path& root);
}
