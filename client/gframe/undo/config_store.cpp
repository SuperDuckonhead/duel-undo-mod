#include "config_store.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cwctype>
#include <windows.h>
#include <bcrypt.h>
namespace undo {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Lock {
    Handle handle;
    bool held = false;
    explicit Lock(const std::filesystem::path& root) {
        auto normalized = std::filesystem::weakly_canonical(std::filesystem::absolute(root)).wstring();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) { return std::towlower(c); });
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA256 provider failed");
        unsigned char digest[32];
        BCRYPT_HASH_HANDLE hash = nullptr;
        auto result = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        if(result >= 0) result = BCryptHashData(hash, reinterpret_cast<PUCHAR>(normalized.data()), static_cast<ULONG>(normalized.size() * sizeof(wchar_t)), 0);
        if(result >= 0) result = BCryptFinishHash(hash, digest, sizeof(digest), 0);
        if(hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if(result < 0) throw std::runtime_error("SHA256 failed");
        std::wstring name = L"Local\\YGOProUndoConfig-";
        for(auto byte : digest) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
        handle.value = CreateMutexW(nullptr, FALSE, name.c_str());
        if(!handle.value) throw std::runtime_error("Configuration mutex failed");
        const auto wait = WaitForSingleObject(handle.value, 30000);
        held = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if(!held) throw std::runtime_error("Configuration mutex wait failed");
    }
    ~Lock() { if(held) ReleaseMutex(handle.value); }
};
std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if(begin == std::string::npos) return {};
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}
ConfigValues read(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path)) return {};
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read configuration: " + path.u8string());
    std::string contents{std::istreambuf_iterator<char>(input), {}};
    if(input.bad()) throw std::runtime_error("Cannot read configuration: " + path.u8string());
    return ConfigStore::Parse(contents);
}
}
ConfigValues ConfigStore::Load() const {
    const auto current = root_ / "system-undo.conf";
    return read(std::filesystem::exists(current) ? current : root_ / "system.conf");
}
ConfigValues ConfigStore::Parse(const std::string& text) {
    ConfigValues values;
    std::istringstream input(text);
    std::string line;
    while(std::getline(input, line)) {
        line = trim(line);
        if(line.empty() || line.front() == '#') continue;
        const auto separator = line.find('=');
        if(separator == std::string::npos) continue;
        auto key = trim(line.substr(0, separator));
        if(!key.empty() && key.find_first_of(" \t\r\n") == std::string::npos) values[key] = trim(line.substr(separator + 1));
    }
    return values;
}
bool ConfigStore::Save(const ConfigValues& changedKeys) const {
    std::filesystem::path temporary;
    try {
        for(const auto& [key, value] : changedKeys)
            if(key.empty() || key.find_first_of("=\r\n\t ") != std::string::npos || key.front() == '#' || value.find_first_of("\r\n") != std::string::npos) return false;
        Lock lock(root_);
        auto latest = Load();
        for(const auto& [key, value] : changedKeys) latest[key] = value;
        std::string bytes = "# YGOPro undo configuration\r\n";
        for(const auto& [key, value] : latest) bytes += key + " = " + value + "\r\n";
        unsigned char random[16];
        if(BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
        std::wstring name = L"system-undo.conf.";
        for(auto byte : random) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
        temporary = root_ / (name + L".tmp");
        {
            Handle file;
            file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if(file.value == INVALID_HANDLE_VALUE) { temporary.clear(); return false; }
            DWORD written = 0;
            if(bytes.size() > MAXDWORD || !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size() || !FlushFileBuffers(file.value)) throw std::runtime_error("Configuration write failed");
        }
        const auto target = root_ / "system-undo.conf";
        const bool replaced = std::filesystem::exists(target)
            ? ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)
            : MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        if(!replaced) throw std::runtime_error("Configuration replacement failed");
        return true;
    } catch(...) {
        if(!temporary.empty()) DeleteFileW(temporary.c_str());
        return false;
    }
}
}
