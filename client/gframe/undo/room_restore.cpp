#include "room_restore.h"
#include <stdexcept>
#include <limits>
namespace undo {
namespace {
constexpr std::size_t MaxSize=16*1024*1024, HeaderSize=29;
void require(bool ok) {if(!ok)throw std::invalid_argument("Invalid room restore descriptor");}
void put(Bytes& out,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i) out.push_back(static_cast<std::uint8_t>(value>>(8*i)));
}
std::uint64_t get(const Bytes& in,std::size_t& at,unsigned count) {
    require(at<=in.size() && count<=in.size()-at);
    std::uint64_t result{};for(unsigned i=0;i<count;++i)result|=std::uint64_t(in[at++])<<(8*i);
    return result;
}
void validate(const RoomRestore& r) {
    require(r.prompt && r.timePlayer<=2 && r.clock.remainingMs[0]>=0 && r.clock.remainingMs[1]>=0);
    require(!r.visible.empty() && r.visible.size()<=MaxSize-HeaderSize);
}
}
Bytes EncodeRoomRestore(const RoomRestore& r) {
    validate(r);Bytes out;out.reserve(HeaderSize+r.visible.size());
    put(out,r.prompt,8);put(out,r.timePlayer,1);
    for(auto clock:r.clock.remainingMs)put(out,static_cast<std::uint64_t>(clock),8);
    put(out,r.visible.size(),4);out.insert(out.end(),r.visible.begin(),r.visible.end());return out;
}
RoomRestore DecodeRoomRestore(const Bytes& bytes) {
    require(bytes.size()>=HeaderSize && bytes.size()<=MaxSize);
    std::size_t at{};RoomRestore out;out.prompt=get(bytes,at,8);out.timePlayer=static_cast<std::uint8_t>(get(bytes,at,1));
    for(auto& clock:out.clock.remainingMs) {
        auto value=get(bytes,at,8);require(value<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
        clock=static_cast<std::int64_t>(value);
    }
    auto size=get(bytes,at,4);require(size==bytes.size()-at);
    out.visible.assign(bytes.begin()+at,bytes.end());validate(out);return out;
}
} // namespace undo
