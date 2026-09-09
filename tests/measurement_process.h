#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace measurement {
class Handle {
public:
 explicit Handle(HANDLE handle) : handle_(handle) {}
 ~Handle() { if(handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
 Handle(const Handle&) = delete;
 Handle& operator=(const Handle&) = delete;
 HANDLE get() const { return handle_; }
private:
 HANDLE handle_;
};
inline std::multimap<DWORD, DWORD> ReadProcesses(HANDLE snapshot) {
 std::multimap<DWORD, DWORD> children;
 PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
 if(!Process32FirstW(snapshot, &entry))
  throw std::runtime_error("Process32First failed: " + std::to_string(GetLastError()));
 do { children.emplace(entry.th32ParentProcessID, entry.th32ProcessID); }
 while(Process32NextW(snapshot, &entry));
 const auto error = GetLastError();
 if(error != ERROR_NO_MORE_FILES)
  throw std::runtime_error("Process32Next failed: " + std::to_string(error));
 return children;
}
// One OS snapshot: count all descendant levels, excluding the measured process.
inline DWORD CountDescendants() {
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
 if(snapshot.get() == INVALID_HANDLE_VALUE)
  throw std::runtime_error("Process snapshot failed: " + std::to_string(GetLastError()));
 const auto children = ReadProcesses(snapshot.get());
 std::set<DWORD> seen{GetCurrentProcessId()};
 std::vector<DWORD> pending{GetCurrentProcessId()};
 while(!pending.empty()) {
  const auto parent = pending.back(); pending.pop_back();
  const auto range = children.equal_range(parent);
  for(auto child = range.first; child != range.second; ++child)
   if(seen.insert(child->second).second) pending.push_back(child->second);
 }
 return static_cast<DWORD>(seen.size() - 1);
}
}
