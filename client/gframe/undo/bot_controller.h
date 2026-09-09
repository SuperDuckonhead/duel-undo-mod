#pragma once
#include "protocol.h"
#include "resource_view.h"
#include <memory>
#include <functional>
namespace ygo { class DataManager; }
namespace undo {
enum class BotState : std::uint8_t { Running, Frozen, Ready, Committed, Failed };
struct BotLaunchData {
 std::string runtimeRoot, executor, deckFile, dialog;
 std::int32_t seed{};
 bool chat{true}, usePreErrataEffects{};
 Digest engine{}, resources{};
 Bytes cardView;
};
// Capture synchronously beside CoreDriver resource capture, before admitting game
// inputs. All 16 setcodes/core fields, ot and all texts are copied; no later DB I/O.
Bytes CaptureBotCardView(const ResourceView&, const ygo::DataManager&, const Digest& engine);
struct BotOutput {
 SessionId session{};
 std::uint64_t epoch{}, prompt{};
 Origin origin{Origin::Bot};
 std::uint32_t producerPid{};
 Bytes packet;
};
class BotController {
public:
 BotController(const std::wstring& executable, const BotLaunchData&, SessionId, std::uint64_t epoch);
 ~BotController();
 BotController(const BotController&)=delete;
 BotController& operator=(const BotController&)=delete;
 std::vector<BotOutput> Dispatch(SessionId, std::uint64_t epoch, std::uint64_t prompt, const Bytes& visibleSTOC);
 // Host-thread synchronous dispatch means Cursor always denotes a completed,
 // drained callback. No network control message is passed to GameBehavior.
 std::size_t Cursor() const;
 // Only a trusted coordinator-bound ActiveKey is authorized. These methods do
 // not derive targets or grant consent/owner privileges from a received request.
 bool Prepare(const TxKey&, std::size_t aiCursor);
 bool Commit(const TxKey&);
 void Abort(const TxKey&);
 bool Resume(const TxKey&, std::uint64_t installedEpoch);
 void Pause();
 // Call at the LAST response/chat delivery boundary, with current prompt identity.
 bool Deliver(const BotOutput&, std::uint64_t currentPrompt, const std::function<void(const Bytes&)>& sink) const;
 BotState State() const;
 std::uint64_t Epoch() const;
 std::uint32_t ActivePid() const;
 std::uint32_t CandidatePid() const;
 std::uint32_t RetainedPid() const;
 std::uint32_t CommitCount() const;
 const std::string& Failure() const;
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}