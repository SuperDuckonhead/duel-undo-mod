#pragma once
#include "coordinator.h"
#include <memory>

namespace undo {
// The legacy TCP length prefix carries this one reserved outer opcode.
constexpr std::uint8_t RoomOuterOpcode = 0x7e;
constexpr std::size_t MaxGamePacket = 1024 * 1024;
struct GamePacket {
    Bytes packet; // Recipient-filtered original STOC opcode followed by its body.
    std::uint64_t prompt{}, sequence{};
};
std::vector<Envelope> EncodeGamePacket(const SessionId&, std::uint64_t epoch,
    std::uint64_t prompt, std::uint64_t sequence, const Bytes& packet);

// One recipient, one epoch, one TCP ordered stream. Reset only after an
// authenticated room initialization or a committed epoch change.
class GamePacketStream {
public:
    GamePacketStream(SessionId session, std::uint64_t epoch);
    void Reset(SessionId session, std::uint64_t epoch);
    std::optional<GamePacket> Add(const Envelope&);
    std::uint64_t LastSequence() const { return lastSequence_; }
private:
    SessionId session_{};
    std::uint64_t epoch_{}, lastSequence_{};
    std::optional<TxKey> active_;
    std::unique_ptr<FragmentAssembler> fragments_;
    bool failed_{};
};

struct NetworkResponse { TxKey key; Origin origin; Bytes response; };
// Bot responses enter through the authenticated private participant, never CTOS.
Envelope EncodeResponse(const TxKey&, Origin, const Bytes&);
NetworkResponse DecodeResponse(const Envelope&);
struct RoomStatus {
    TxState state{TxState::Running};
    std::uint8_t eligibleMask{}, promptPlayer{2}, timePlayer{2};
    std::uint64_t nextRequest{1}, prompt{};
    ClockState clock{};
};
Bytes EncodeRoomStatus(const RoomStatus&);
RoomStatus DecodeRoomStatus(const Bytes&);
} // namespace undo
