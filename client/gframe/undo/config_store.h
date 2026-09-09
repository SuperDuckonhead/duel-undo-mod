#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace undo {
using ConfigValues = std::map<std::string, std::string>;
class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path root): root_(std::move(root)) {}
    ConfigValues Load() const;
    bool Save(const ConfigValues& changedKeys) const;
    static ConfigValues Parse(const std::string& text);
private:
    std::filesystem::path root_;
};
}
