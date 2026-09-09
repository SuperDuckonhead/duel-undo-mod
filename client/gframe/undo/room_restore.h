#pragma once
#include "protocol.h"
namespace undo {
// Prepare's assembled payload. visible is exactly one EncodePlayerRestore body;
// its embedded recipient and digest are verified by the client participant.
// Widget snapshots stay client-local and are selected by historical prompt ID.
struct RoomRestore {
    std::uint64_t prompt{};
    std::uint8_t timePlayer{2};
    ClockState clock;
    Bytes visible;
};
Bytes EncodeRoomRestore(const RoomRestore&);
RoomRestore DecodeRoomRestore(const Bytes&);
} // namespace undo
