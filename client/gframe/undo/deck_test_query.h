#pragma once
#include "deck_test_model.h"
#include "core_driver.h"
#include <future>
struct lua_State;
class card;
enum class native_query_api : uint8_t;

namespace undo {

enum class PoolQueryStage : uint8_t { TargetCheck, ResolutionSelection };
enum class PoolQueryApi : uint8_t { ExistingMatching, MatchingGroup, SelectMatching, FusionMaterials, CheckFusion, FusionProcedure, SelectFusion, SelectedFusion };
enum class PoolQuerySemantic : uint8_t { SingleTarget, FusionTarget, MaterialUniverse, ExtraMaterial, WholeFusion, MaterialSelection };

// Pointer-free identity for registered semantic adapters. The Lua closure and
// effect are reobtained in each core, never exported to a worker.
struct PoolQueryRequest {
 DeckTestContext context;
 Digest acceptedPrefix{}, script{}, parameters{}, callState{};
 StableInstanceId handler;
 uint64_t effectRegistration{}, invocation{};
 uint32_t effectCode{}, callsite{}, selfLocation{}, opponentLocation{}, minimum{}, maximum{};
 uint8_t player{};
 PoolQueryStage stage{PoolQueryStage::TargetCheck};
 SelectionRole role{SelectionRole::ResolutionTarget};
 CardSource source{CardSource::OwnMainDeck};
 PoolQueryApi api{PoolQueryApi::ExistingMatching};
 PoolQuerySemantic semantic{PoolQuerySemantic::SingleTarget};
 Digest helper{}, scope{}, parentScope{};
 StableInstanceId target;
 uint64_t procedureRegistration{};
};

// Actual-only observations from the original closures. Stable membership and
// results can cross a core boundary; Lua closures/Groups never leave their core.
struct PoolQueryObservation {
 PoolQueryRequest request;
 std::vector<CardReference> input, members;
 StableInstanceId reasonHandler;
 uint64_t reasonRegistration{};
 // request.callState is before native consumption; afterCallState is separate
 // because executing an original predicate may change Lua or engine state.
 Digest additionalCheck{}, additionalGoal{}, targetScript{}, afterCallState{};
 bool additionalCheckPresent{}, additionalGoalPresent{}, completed{};
 int32_t result{-1};
 uint32_t chainMaterialEffects{}, extraMaterialEffects{};
};

struct PoolQueryDiscovery {
 DeckTestContext context;
 Digest acceptedPrefix{};
 Bytes diagnostic;
 std::vector<PoolQueryRequest> requests;
 std::vector<PoolQueryObservation> observations;
};

// Only the worker can construct accepted evidence. A caller cannot turn an
// arbitrary card number into a positive answer to the engine's precheck.
class PoolQueryEvidence {
public:
 const std::vector<uint32_t>& Candidates() const {return candidates_;}
 const std::vector<std::string>& Errors() const {return errors_;}
 const PoolQueryRequest& Request() const {return request_;}
private:
 friend class PoolQueryGate;
 PoolQueryRequest request_;
 std::vector<uint32_t> candidates_;
 std::vector<std::string> errors_;
};

// Architecture gate only: no UI, wire protocol or automatic catalog scan.
// The candidate span is explicitly partial. Each exact check owns a disposable
// core; production indexed/sandboxed search replaces this deliberately costly
// path in task 5.3.
class PoolQueryGate {
public:
 static PoolQueryDiscovery Discover(const CoreDriver&,const std::vector<ResponseRecord>&,const DeckTestContext&);
 static std::future<PoolQueryEvidence> SearchAsync(const InitialState&,std::shared_ptr<const ResourceView>,
   std::vector<ResponseRecord>,PoolQueryRequest,std::vector<uint32_t> candidateCodes);
 static void Install(std::unique_ptr<CoreDriver>&,const std::vector<ResponseRecord>&,const DeckTestContext& current,
   const PoolQueryDiscovery&,const PoolQueryEvidence&);
 static void RecomputeActual(std::unique_ptr<CoreDriver>&,const std::vector<ResponseRecord>&,const DeckTestContext& current);
 static std::vector<PoolQueryRequest> PendingQueries(const CoreDriver&);
 static Digest PrefixDigest(const std::vector<ResponseRecord>&);
 // Recreate an actual control boundary privately and retain observation for
 // subsequent native responses. No evidence installation or entity creation.
 static std::unique_ptr<CoreDriver> Recreate(const CoreDriver&,const std::vector<ResponseRecord>&,const DeckTestContext&);
 static std::vector<PoolQueryObservation> Observations(const CoreDriver&);
private:
 friend class CoreDriver;
 static void ResponseSubmitted(CoreDriver&,const Bytes&,Origin);
 static void ResponseRejected(CoreDriver&);
 static void ResponseAccepted(CoreDriver&);
 static std::unique_ptr<CoreDriver> Replay(const InitialState&,std::shared_ptr<const ResourceView>,
   const std::vector<ResponseRecord>&,std::shared_ptr<PoolQueryState>);
 static bool Query(CoreDriver&,lua_State*,bool selection,bool actual);
 static void Observe(CoreDriver&,lua_State*,native_query_api,bool,const std::vector<card*>&,card*,int32_t);
};
}
