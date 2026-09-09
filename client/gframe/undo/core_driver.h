#pragma once
#include "resource_view.h"
namespace undo {
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
enum class BoundaryKind { AwaitResponse, Finished, Failed };
struct Boundary {
 BoundaryKind kind{BoundaryKind::Failed}; Checkpoint checkpoint{};
 bool rejectedResponse{}; std::string failure;
};
class CoreDriver {
public:
 static std::unique_ptr<CoreDriver> Create(const InitialState&,std::shared_ptr<const ResourceView>);
 ~CoreDriver();
 CoreDriver(const CoreDriver&)=delete;
 CoreDriver& operator=(const CoreDriver&)=delete;
 Boundary Advance();
 Boundary Current() const;
 void Submit(const Bytes&);
 const InitialState& Initial() const { return initial_; }
 std::shared_ptr<const ResourceView> Resources() const { return resources_; }
 Bytes Transcript() const;
 std::vector<std::string> Logs() const;
private:
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
};
}