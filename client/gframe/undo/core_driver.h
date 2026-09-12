#pragma once
#include "resource_view.h"
#include "test_state_patch.h"
#include <functional>
namespace undo {
class PoolQueryGate;
struct PoolQueryState;
struct InitialCard {
 uint32_t code{}; uint8_t owner{},controller{},location{},sequence{},position{};
};
struct PlayerInit { int32_t lp{8000},startCount{},drawCount{}; };
struct InitialState {
 std::vector<uint32_t> seed;
 uint32_t duelOptions{};
 bool noCheckDeck{},noShuffleDeck{};
 std::array<PlayerInit,2> players{};
 std::vector<InitialCard> cards;
 std::string scenarioName;
 // Opaque bytes exposed as the Lua string UNDO_SCENARIO_PARAMETERS before preload.
 Bytes scenarioParameters;
 Digest resourceDigest{};
};
enum class BoundaryKind { AwaitResponse, Finished, Failed, AwaitPoolQuery };
struct Boundary {
 BoundaryKind kind{BoundaryKind::Failed}; Checkpoint checkpoint{};
 bool rejectedResponse{}; std::string failure;
};
// Native, host-private ordered output. Every message still requires the normal
// host recipient filter before it can become a restore frame or AI input.
struct CoreOutput {
 Bytes message;
 std::optional<TestStatePatch> sourceBirth; // recipient is assigned by the host
};
class CoreDriver {
public:
 static std::unique_ptr<CoreDriver> Create(const InitialState&,std::shared_ptr<const ResourceView>);
 ~CoreDriver();
 CoreDriver(const CoreDriver&)=delete;
 CoreDriver& operator=(const CoreDriver&)=delete;
 // Live-only output is emitted message by message outside the core API binding.
 // Awaiting-response prompts are withheld until the session records acceptance.
 // Rebuild leaves this callback empty and has no external output capability.
 using LiveOutput=std::function<void(const Bytes&)>;
 // Candidate-only collection into a private host builder. As with LiveOutput,
 // raw messages must pass the normal host visibility filter before delivery.
 using PrivateOutput=std::function<void(const CoreDriver&,const CoreOutput&)>;
 Boundary Advance(const LiveOutput& output={});
 Bytes QueryInfo() const;
 Bytes QueryField(uint8_t player,uint8_t location,uint32_t flags) const;
 Bytes QueryCard(uint8_t player,uint8_t location,uint8_t sequence,uint32_t flags) const;
 Boundary Current() const;
 void Submit(const Bytes&);
 void Submit(const Bytes&,Origin);
 const InitialState& Initial() const { return initial_; }
 std::shared_ptr<const ResourceView> Resources() const { return resources_; }
 Bytes Transcript() const;
 std::vector<std::string> Logs() const;
 // Private host diagnostic, deliberately separate from legacy checkpoints.
 Bytes DiagnosticState() const;
 // Captured only for an attached pool context or explicit private collector.
 std::vector<CoreOutput> OrderedOutput() const;
private:
 friend class PoolQueryGate;
 class Binding;
 CoreDriver(const InitialState&,std::shared_ptr<const ResourceView>);
 static unsigned char* Script(const char*,int*);
 static uint32_t Card(uint32_t,card_data*);
 static uint32_t Log(intptr_t,uint32_t);
 Bytes Canonical();
 void CheckFailure() const;
 intptr_t handle_{};
 InitialState initial_;
 std::shared_ptr<const ResourceView> resources_;
 Boundary boundary_{};
 bool started_{},waiting_{},submitted_{};
 Bytes transcript_;
 std::vector<std::string> logs_;
 std::string callbackFailure_;
 std::shared_ptr<PoolQueryState> poolQuery_;
 std::vector<CoreOutput> orderedOutput_;
 std::vector<std::pair<size_t,TestStatePatch>> pendingBirths_;
 PrivateOutput privateOutput_;
};
}
