#pragma once
#include "single_duel.h"
#include "undo/room_config.h"
#include "undo/room_wire.h"
#include <functional>
#include <memory>
namespace ygo {
// Authoritative room owner. All methods execute on the NetServer event thread;
// candidate workers receive immutable copies and have no endpoint/UI callbacks.
class UndoDuel final : public SingleDuel {
public:
    using Send = std::function<bool(DuelPlayer*, const undo::Envelope&)>;
    UndoDuel(bool match, std::shared_ptr<const undo::RoomConfig>, undo::SessionId, Send = {});
    ~UndoDuel() override;
    bool SupportsUndo() const override { return true; }
    bool HasActiveDuel() const override;
    void JoinGame(DuelPlayer*, unsigned char*, bool) override;
    void TPResult(DuelPlayer*, unsigned char) override;
    void Process() override;
    void GetResponse(DuelPlayer*, unsigned char*, unsigned int) override;
    void ReceiveUndo(DuelPlayer*, const undo::Envelope&) override;
    void PollUndo() override;
    bool RoutePacket(DuelPlayer*, uint8_t, const unsigned char*, size_t) override;
    void WaitforResponse(int) override;
    void TimeConfirm(DuelPlayer*) override;
    void TimerTick() override;
    void EndDuel() override;
    void Surrender(DuelPlayer*) override;
    void LeaveGame(DuelPlayer*) override;
    void OnPlayerDisconnected(DuelPlayer*) override;
    void ToObserver(DuelPlayer*) override;
    const undo::InitialState& Initial() const;
    const undo::DuelHistory& History() const;
    undo::Boundary CurrentBoundary() const;
    undo::RoomStatus Status() const;
    const undo::TxKey& ActiveKey() const;
    std::uint64_t InstalledEpoch() const;
protected:
    undo::Bytes QueryFieldBytes(int, int, unsigned int, int) override;
    undo::Bytes QueryCardBytes(int, int, int, unsigned int) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ygo
