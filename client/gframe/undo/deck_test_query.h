#pragma once
#include "deck_test_model.h"
#include "core_driver.h"
#include <future>
struct lua_State;

namespace undo {

enum class PoolQueryStage : uint8_t { TargetCheck, ResolutionSelection };

// Pointer-free identity for the narrowly registered D01 adapter. The Lua
// closure and effect are reobtained in each core, never exported to a worker.
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
};

struct PoolQueryDiscovery {
 DeckTestContext context;
 Digest acceptedPrefix{};
 Bytes diagnostic;
 std::vector<PoolQueryRequest> requests;
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
private:
 friend class CoreDriver;
 static void ResponseSubmitted(CoreDriver&,const Bytes&,Origin);
 static void ResponseRejected(CoreDriver&);
 static void ResponseAccepted(CoreDriver&);
 static std::unique_ptr<CoreDriver> Replay(const InitialState&,std::shared_ptr<const ResourceView>,
   const std::vector<ResponseRecord>&,std::shared_ptr<PoolQueryState>);
 static bool Query(CoreDriver&,lua_State*,bool selection,bool actual);
};
}
