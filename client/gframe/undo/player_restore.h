#pragma once
#include "../client_field.h"
#include "protocol.h"
#include <memory>
namespace undo {
// Input provenance is the caller's trust boundary: only retained, recipient-filtered
// legacy game-message bytes belong here. Never pass CoreDriver canonical/replay data.
struct PlayerRestore {
    std::uint8_t player{};
    std::vector<Bytes> frames;
    Bytes prompt;
    Digest visibleDigest{};
};
PlayerRestore BuildPlayerRestore(std::uint8_t player, const std::vector<Bytes> &playerVisibleHistory,
                                 const Bytes &targetPrompt);
Digest HashVisibleRestore(const PlayerRestore &);
Bytes EncodePlayerRestore(const PlayerRestore &);
PlayerRestore DecodePlayerRestore(const Bytes &);
class PlayerRestoreAssembler {
  public:
    PlayerRestoreAssembler(const TxKey &key, std::uint8_t recipient);
    bool Add(const TxKey &, std::uint8_t recipient, const Bytes &fragment);
    PlayerRestore Finish() const;

  private:
    TxKey key_;
    std::uint8_t recipient_;
    FragmentAssembler fragments_;
    bool failed_{};
};
struct PlayerViewState {
    std::int32_t lp[2]{};
    std::uint32_t turn{};
    std::uint16_t phase{};
    std::uint8_t turnPlayer{}, duelRule{4};
    Bytes prompt;
};
// Single-threaded controller. Caller holds the render/input mutex across Commit
// and updates its prepared widget state before Resume. Candidate replay never
// touches mainGame, GUI, animation queues, audio, networking, replay, or clocks.
class ClientRestore {
  public:
    ClientRestore(ygo::ClientField &live, PlayerViewState &state, std::uint8_t recipient,
                  const SessionId &session, std::uint64_t epoch);
    bool Prepare(const TxKey &, const PlayerRestore &, const Bytes &expectedPrompt);
    bool Commit(const TxKey &, std::uint64_t installedEpoch) noexcept;
    bool Resume(const TxKey &) noexcept;
    void Abort(const TxKey &) noexcept;
    bool AcceptsGameplay(const SessionId &, std::uint64_t epoch) const noexcept;
    bool Paused() const noexcept { return paused_; }
    const ygo::ClientField *PreparedField() const noexcept { return candidate_.get(); }
    const PlayerViewState *PreparedState() const noexcept { return candidate_ ? &preparedState_ : nullptr; }
    const std::string &Error() const noexcept { return error_; }

  private:
    ygo::ClientField &live_;
    PlayerViewState &state_;
    std::uint8_t recipient_;
    SessionId session_;
    std::uint64_t epoch_;
    std::optional<TxKey> active_;
    std::unique_ptr<ygo::ClientField> candidate_;
    PlayerViewState preparedState_;
    bool paused_{}, committed_{};
    std::string error_;
};
} // namespace undo
