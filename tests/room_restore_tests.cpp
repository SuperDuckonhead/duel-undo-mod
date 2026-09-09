#include "test_support.h"
#include "undo/room_restore.h"
#include <iostream>
template<class F> bool rejects(F f) {try { f(); } catch(const std::exception&) {return true;} return false;}
int main() {try {
    using namespace undo;
    RoomRestore original{27,1,{{1234,5678}},Bytes{0,1,2,3}};
    auto bytes=EncodeRoomRestore(original);auto value=DecodeRoomRestore(bytes);
    CHECK(value.prompt==27 && value.timePlayer==1 && value.clock.remainingMs==original.clock.remainingMs && value.visible==original.visible);
    for(std::size_t n=0;n<bytes.size();++n) CHECK(rejects([&]{DecodeRoomRestore(Bytes(bytes.begin(),bytes.begin()+n));}));
    auto trailing=bytes;trailing.push_back(0);CHECK(rejects([&]{DecodeRoomRestore(trailing);}));
    auto wrong=original;wrong.prompt=0;CHECK(rejects([&]{EncodeRoomRestore(wrong);}));
    wrong=original;wrong.timePlayer=3;CHECK(rejects([&]{EncodeRoomRestore(wrong);}));
    wrong=original;wrong.clock.remainingMs[1]=-1;CHECK(rejects([&]{EncodeRoomRestore(wrong);}));
    wrong=original;wrong.visible.clear();CHECK(rejects([&]{EncodeRoomRestore(wrong);}));
    wrong=original;wrong.visible.resize(16*1024*1024);CHECK(rejects([&]{EncodeRoomRestore(wrong);}));
    std::cout << "restore target prompt, precise clock, bounded visible body and all truncations passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
