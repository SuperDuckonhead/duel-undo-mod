#include "measurement_process.h"
#include <iostream>
#include <string>
#include <stdexcept>
#define require(value) do { if(!(value)) throw std::runtime_error("process regression failed at " + std::to_string(__LINE__) + ", error " + std::to_string(GetLastError())); } while(false)
int main(int argc, char** argv) {
 try {
  if(argc == 3 && std::string(argv[1]) == "leaf") {
   auto release = reinterpret_cast<HANDLE>(std::stoull(argv[2]));
   return WaitForSingleObject(release, 30000) == WAIT_OBJECT_0 ? 0 : 2;
  }
  if(argc == 4 && std::string(argv[1]) == "child") {
   auto ready = reinterpret_cast<HANDLE>(std::stoull(argv[2]));
   auto release = reinterpret_cast<HANDLE>(std::stoull(argv[3]));
   std::string command = '"' + std::string(argv[0]) + "\" leaf " + argv[3];
   STARTUPINFOA start{}; start.cb = sizeof(start); PROCESS_INFORMATION process{};
   require(CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, DETACHED_PROCESS, nullptr, nullptr, &start, &process));
   measurement::Handle child(process.hProcess), thread(process.hThread);
   require(SetEvent(ready));
   require(WaitForSingleObject(release, 30000) == WAIT_OBJECT_0);
   require(WaitForSingleObject(child.get(), 30000) == WAIT_OBJECT_0);
   return 0;
  }
  bool rejected = false;
  try { measurement::ReadProcesses(INVALID_HANDLE_VALUE); } catch(const std::runtime_error&) { rejected = true; }
  require(rejected);
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  measurement::Handle ready(CreateEventA(&security, TRUE, FALSE, nullptr));
  measurement::Handle release(CreateEventA(&security, TRUE, FALSE, nullptr));
  require(ready.get() && release.get());
  std::string command = '"' + std::string(argv[0]) + "\" child " + std::to_string(reinterpret_cast<std::uintptr_t>(ready.get())) + " " + std::to_string(reinterpret_cast<std::uintptr_t>(release.get()));
  STARTUPINFOA start{}; start.cb = sizeof(start); PROCESS_INFORMATION process{};
  require(CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, DETACHED_PROCESS, nullptr, nullptr, &start, &process));
  measurement::Handle child(process.hProcess), thread(process.hThread);
  require(WaitForSingleObject(ready.get(), 30000) == WAIT_OBJECT_0);
  const auto descendants = measurement::CountDescendants();
  require(SetEvent(release.get()));
  require(WaitForSingleObject(child.get(), 30000) == WAIT_OBJECT_0);
  std::cout << "Observed descendants: " << descendants << '\n'; require(descendants == 2);
  DWORD exit{}; require(GetExitCodeProcess(child.get(), &exit) && exit == 0);
  require(measurement::CountDescendants() == 0);
  std::cout << "Toolhelp failure rejected; actual child and grandchild counted; all exited.\n";
  return 0;
 } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
