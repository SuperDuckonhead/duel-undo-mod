#pragma once
#include "protocol.h"
#include "resource_view.h"
#include <memory>
#include <functional>
#include <atomic>
#include <optional>
namespace ygo { class DataManager; }
namespace undo {
enum class BotState : std::uint8_t { Running, Frozen, Ready, Committed, Failed };
struct BotSelectionInfo {
 std::string name{"WindBot-undo"}, executor, deckFile, dialog{"default"}, customDeckSource;
 std::int32_t hand{};
 bool chat{true}, usePreErrataEffects{};
};
struct BotFrozenConfig {
 std::string source;
 Bytes content;
 Digest sha256{};
};
// Discovery parses command/catalog only, never chooses Random or reads a file.
// Freeze every returned source under its exact menu-relative/source label.
std::vector<std::string> DiscoverBotConfigSources(const std::string& command, const Bytes& catalog);
struct BotLaunchData {
 std::string runtimeRoot, executor, deckFile, dialog{"default"};
 std::int32_t seed{};
 bool chat{true}, usePreErrataEffects{};
 Digest engine{}, resources{};
 Bytes cardView;
 std::string name{"WindBot-undo"}, selectionCommand, customDeckSource;
 std::int32_t hand{};
 Bytes selectionCatalog, customDeck;
 bool hasCustomDeck{};
 std::vector<BotFrozenConfig> selectionConfigs;
 std::optional<BotFrozenConfig> appSettings;
};
Bytes CaptureBotCardView(const ResourceView&, const ygo::DataManager&, const Digest& engine);
struct BotOutput {
 SessionId session{};
 std::uint64_t epoch{}, prompt{};
 Origin origin{Origin::Bot};
 std::uint32_t producerPid{};
 Bytes packet;
};
struct BotIdentity {
 SessionId session{};
 std::uint64_t epoch{};
 BotState state{BotState::Frozen};
 std::uint32_t activePid{};
};
// Pure final-boundary check. Caller also checks coordinator admission/generation.
bool AcceptsBotOutput(const BotOutput&, const BotIdentity&, std::uint64_t currentPrompt) noexcept;
using BotCancellation = std::shared_ptr<std::atomic<bool>>;
class BotController {
public:
 BotController(const std::wstring& executable, const BotLaunchData&, SessionId, std::uint64_t epoch, BotCancellation cancel = {});
 ~BotController();
 BotController(const BotController&)=delete;
 BotController& operator=(const BotController&)=delete;
 std::vector<BotOutput> Dispatch(SessionId, std::uint64_t epoch, std::uint64_t prompt, const Bytes& visibleSTOC);
 std::size_t Cursor() const;
 bool Prepare(const TxKey&, std::size_t aiCursor);
 bool Commit(const TxKey&);
 void Abort(const TxKey&);
 bool Resume(const TxKey&, std::uint64_t installedEpoch);
 void Pause();
 bool Deliver(const BotOutput&, std::uint64_t currentPrompt, const std::function<void(const Bytes&)>& sink) const;
 BotState State() const;
 std::uint64_t Epoch() const;
 std::uint32_t ActivePid() const;
 std::uint32_t CandidatePid() const;
 std::uint32_t RetainedPid() const;
 std::uint32_t CommitCount() const;
 const std::string& Failure() const;
 const BotSelectionInfo& Selection() const;
 BotIdentity Identity() const;
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}