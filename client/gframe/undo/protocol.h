#pragma once
#include "duel_history.h"
#include <optional>
#include <string>

namespace undo {
using SessionId = std::array<std::uint8_t,16>;
struct TxKey {
 SessionId session{};
 std::uint64_t epoch{}, request{}, targetIndex{};
 Digest targetDigest{};
};
enum class WireKind : std::uint8_t {
 Hello=1, Response=2, Request=3, Consent=4, Prepare=5, Ready=6,
 Commit=7, CommitAck=8, Resume=9, Abort=10, AbortAck=11
};
struct Envelope { WireKind kind; TxKey key; Bytes payload; };
constexpr std::size_t WireHeaderSize = 79;
constexpr std::size_t MaxPayload = 48 * 1024;
constexpr std::size_t MaxRestoreBytes = 16 * 1024 * 1024;
Bytes Encode(const Envelope&);
Envelope Decode(const Bytes&);
bool SameKey(const TxKey&, const TxKey&);
bool IsCurrent(const TxKey&, const SessionId&, std::uint64_t epoch);
SessionId NewSessionId();
enum class RoomMode : std::uint8_t { ConsentLan=1, LoopbackFree=2 };
struct Hello {
 std::uint16_t version{1};
 Digest engine{}, rules{}, resources{};
 RoomMode mode{RoomMode::ConsentLan};
};
Bytes EncodeHello(const Hello&);
Hello DecodeHello(const Bytes&);
// Empty means compatible; absence of peer Hello is explicitly incompatible.
std::string Compatibility(const Hello&, const std::optional<Hello>&);
// Caller supplies ONLY bytes already filtered for this recipient. A separate
// assembler is bound to one TxKey/recipient; it never handles host replay data.
std::vector<Bytes> Fragment(const Bytes& playerVisibleStream);
class FragmentAssembler {
public:
 void Add(const Bytes& fragment);
 Bytes Finish() const;
private:
 std::uint32_t count_{}, next_{}, total_{};
 Bytes data_;
 bool failed_{};
};
} // namespace undo
