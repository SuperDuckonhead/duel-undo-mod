#include "deck_test_query.h"
#include "../../ocgcore/duel.h"
#include "../../ocgcore/card.h"
#include "../../ocgcore/effect.h"
#include "../../ocgcore/field.h"
#include "../../ocgcore/interpreter.h"
#include "../../ocgcore/ocgapi.h"
#include <algorithm>
#include <stdexcept>

namespace undo {
struct PoolQueryState {
 DeckTestContext context;
 Digest prefix{};
 std::vector<PoolQueryRequest> requests;
 std::optional<PoolQueryRequest> match;
 uint32_t witness{};
 bool evidence{},applied{},witnessAccepted{};
 uint64_t invocation{};
 std::vector<ResponseRecord> records;
 std::shared_ptr<PoolQueryState> rollback;
};
namespace {
void word(Bytes& out,uint64_t n) {for(unsigned i=0;i<8;++i)out.push_back(uint8_t(n>>(8*i)));}
void bytes(Bytes& out,const Bytes& in) {word(out,in.size());out.insert(out.end(),in.begin(),in.end());}
void digest(Bytes& out,const Digest& in) {out.insert(out.end(),in.begin(),in.end());}
bool position(const Checkpoint& a,const Checkpoint& b) {
 return a.player==b.player && a.prompt==b.prompt && a.canonicalState==b.canonicalState && a.transcriptDigest==b.transcriptDigest;
}
bool context(const DeckTestContext& a,const DeckTestContext& b) {
 return a.session.value==b.session.value && a.branch.value==b.branch.value && a.historyCursor==b.historyCursor &&
  a.resourceVersion==b.resourceVersion && a.modeGeneration==b.modeGeneration && position(a.checkpoint,b.checkpoint) &&
  a.checkpoint.clock.remainingMs==b.checkpoint.clock.remainingMs && a.checkpoint.aiLogCursor==b.checkpoint.aiLogCursor;
}
bool same(const PoolQueryRequest& a,const PoolQueryRequest& b) {
 return context(a.context,b.context) && a.acceptedPrefix==b.acceptedPrefix && a.script==b.script &&
  a.parameters==b.parameters && a.callState==b.callState && a.handler==b.handler &&
  a.effectRegistration==b.effectRegistration && a.invocation==b.invocation && a.effectCode==b.effectCode &&
  a.callsite==b.callsite && a.selfLocation==b.selfLocation && a.opponentLocation==b.opponentLocation &&
  a.minimum==b.minimum && a.maximum==b.maximum && a.player==b.player && a.stage==b.stage && a.role==b.role && a.source==b.source;
}
Digest registeredScript() {
 const char* hex="14432383c12ce67c8b171ff9e96326130714f676f505b3472b4c898969218141";
 Digest out{};auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
 for(size_t i=0;i<out.size();++i)out[i]=uint8_t(nibble(hex[2*i])*16+nibble(hex[2*i+1]));return out;
}
void validate(const CoreDriver& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& token) {
 if(Validate(token)!=DeckTestError::None || !token.modeGeneration || token.historyCursor!=prefix.size() ||
    token.resourceVersion!=live.Resources()->Fingerprint() || live.Current().kind!=BoundaryKind::AwaitResponse ||
    !position(token.checkpoint,live.Current().checkpoint))throw std::runtime_error("Stale pool query context");
}
bool nativePrompt(const Boundary& boundary) {
 if(boundary.kind!=BoundaryKind::AwaitResponse || boundary.rejectedResponse || boundary.checkpoint.prompt.empty())return false;
 const auto& p=boundary.checkpoint.prompt;
 auto code=[&](size_t at){return at+4<=p.size() && (uint32_t(p[at])|(uint32_t(p[at+1])<<8)|(uint32_t(p[at+2])<<16)|(uint32_t(p[at+3])<<24))==62962630;};
 if(p[0]==MSG_SELECT_EFFECTYN)return code(2);
 if(p[0]==MSG_SELECT_CHAIN && p.size()>=12)
  for(size_t i=0;i<p[2];++i)if(code(14+i*14))return true;
 return false;
}
bool functionEquals(lua_State* L,int index,const char* name) {
 const int top=lua_gettop(L);lua_getglobal(L,"c62962630");
 if(!lua_istable(L,-1)){lua_settop(L,top);return false;}
 lua_getfield(L,-1,name);bool same=lua_rawequal(L,index,-1);lua_settop(L,top);return same;
}
}
Digest PoolQueryGate::PrefixDigest(const std::vector<ResponseRecord>& prefix) {
 Bytes out;word(out,prefix.size());
 for(const auto& r:prefix) {
  word(out,r.player);word(out,uint8_t(r.origin));bytes(out,r.response);word(out,r.before.player);
  bytes(out,r.before.prompt);bytes(out,r.before.canonicalState);digest(out,r.before.transcriptDigest);
  for(auto t:r.before.clock.remainingMs)word(out,t);word(out,r.before.aiLogCursor);
 }
 return Sha256(out);
}

bool PoolQueryGate::Query(CoreDriver& driver,lua_State* L,bool selection,bool actual) {
 auto& state=*driver.poolQuery_;auto* pd=reinterpret_cast<duel*>(driver.handle_);auto* f=pd->game_field;
 effect* e=selection?f->core.reason_effect:pd->pool_target_check;
 if(!e || !e->handler || e->handler->data.code!=62962630 || e->handler->current.controler!=0 ||
    e->type!=(EFFECT_TYPE_SINGLE|EFFECT_TYPE_TRIGGER_O|EFFECT_TYPE_ACTIONS) ||
    (e->code!=EVENT_SUMMON_SUCCESS && e->code!=EVENT_SPSUMMON_SUCCESS))return actual;
 const int filter=selection?2:1;const int total=selection?8:6;
 if(lua_gettop(L)!=total || !lua_isnil(L,total) || !functionEquals(L,filter,"thfilter"))return actual;
 // Exact supported parameter shape; unsupported arguments retain real semantics.
 const int selfIndex=filter+1;
 for(int index=selfIndex;index<total;++index)if(!lua_isinteger(L,index))return actual;
 if(lua_tointeger(L,selfIndex)!=0 || (selection && lua_tointeger(L,1)!=0) ||
    lua_tointeger(L,selfIndex+1)!=LOCATION_DECK || lua_tointeger(L,selfIndex+2)!=0 ||
    lua_tointeger(L,selfIndex+3)!=1 || (selection && lua_tointeger(L,7)!=1))return actual;
 // Verify the immediate Lua caller is the registered original target/operation.
 lua_Debug ar{};if(!lua_getstack(L,1,&ar) || !lua_getinfo(L,"fl",&ar))return actual;
 bool caller=functionEquals(L,lua_gettop(L),selection?"thop":"thtg");lua_pop(L,1);
 if(!caller)return actual;
 PoolQueryRequest request;request.context=state.context;request.acceptedPrefix=state.prefix;
 request.script=registeredScript();request.handler={e->handler->cardid};request.effectRegistration=e->registration_ordinal;
 request.effectCode=e->code;request.invocation=++state.invocation;request.callsite=ar.currentline;
 request.player=0;request.selfLocation=LOCATION_DECK;request.minimum=1;request.maximum=1;
 request.stage=selection?PoolQueryStage::ResolutionSelection:PoolQueryStage::TargetCheck;
 Bytes parameters;word(parameters,total);word(parameters,selection);for(int index=selfIndex;index<total;++index)word(parameters,lua_tointeger(L,index));
 request.parameters=Sha256(parameters);request.callState=Sha256(driver.DiagnosticState());
 if(actual)return actual;
 state.requests.push_back(request);
 if(selection) {pd->pool_selection_pending=true;return actual;}
 if(!state.match || !same(request,*state.match))return actual;
 if(state.witness) {
  // Disposable worker only. Run initial_effect and the original filter on a
  // real source instance. Nothing here is ever installed into the main core.
  // Use the real source lifecycle, including enable_field_effect and
  // adjust_instant. A deck-range restriction must apply in the worker too.
  ::new_card(driver.handle_,state.witness,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE);
  bool accepted=f->filter_matching_card(L,1,0,LOCATION_DECK,0,nullptr,nullptr,nullptr,0,nullptr,1);
  // initial_effect may install player restrictions as well as card-local
  // properties. Recheck the full activation using the still-live event, not
  // merely the filter whose call we intercepted before creating the witness.
  if(accepted)accepted=pd->pool_target_event && e->is_activateable(0,*pd->pool_target_event);
  driver.CheckFailure();state.witnessAccepted=accepted;state.applied=true;return accepted;
 }
 if(state.evidence){state.applied=true;return true;}
 return actual;
}

std::unique_ptr<CoreDriver> PoolQueryGate::Replay(const InitialState& initial,std::shared_ptr<const ResourceView> resources,
 const std::vector<ResponseRecord>& prefix,std::shared_ptr<PoolQueryState> state) {
 if(state && Sha256(resources->Read("script/c62962630.lua"))!=registeredScript())
  throw std::runtime_error("Unsupported c62962630 script hash");
 auto candidate=CoreDriver::Create(initial,std::move(resources));
 if(state)state->records=prefix;
 auto attach=[&] {
  if(!state)return;
  std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());candidate->poolQuery_=state;
  reinterpret_cast<duel*>(candidate->handle_)->pool_query=[driver=candidate.get()](lua_State* L,bool selection,bool actual) {
   try{return Query(*driver,L,selection,actual);}catch(const std::exception& error){
    driver->callbackFailure_=error.what();return actual;
   }
  };
 };
 if(prefix.empty())attach();auto boundary=candidate->Advance();
 for(size_t i=0;i<prefix.size();++i) {
  const auto& record=prefix[i];
  if(record.player!=record.before.player || record.player>1 || uint8_t(record.origin)>uint8_t(Origin::Bot) ||
     boundary.kind!=BoundaryKind::AwaitResponse || !position(boundary.checkpoint,record.before))
   throw std::runtime_error("Pool replay prefix diverged at "+std::to_string(i));
  candidate->Submit(record.response,record.origin);if(i+1==prefix.size())attach();boundary=candidate->Advance();
  if(boundary.rejectedResponse)throw std::runtime_error("Pool replay response rejected");
 }
 if(boundary.kind==BoundaryKind::Failed || boundary.kind==BoundaryKind::Finished)
  throw std::runtime_error("Pool replay failed: "+boundary.failure);
 return candidate;
}

PoolQueryDiscovery PoolQueryGate::Discover(const CoreDriver& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& current) {
 validate(live,prefix,current);PoolQueryDiscovery result;result.context=current;result.acceptedPrefix=PrefixDigest(prefix);result.diagnostic=live.DiagnosticState();
 auto state=std::make_shared<PoolQueryState>();state->context=current;state->prefix=result.acceptedPrefix;
 auto candidate=Replay(live.Initial(),live.Resources(),prefix,state);
 if(!position(candidate->Current().checkpoint,current.checkpoint) || candidate->DiagnosticState()!=result.diagnostic)
  throw std::runtime_error("Query discovery changed actual-only state");
 result.requests=state->requests;return result;
}

std::future<PoolQueryEvidence> PoolQueryGate::SearchAsync(const InitialState& initial,std::shared_ptr<const ResourceView> resources,
 std::vector<ResponseRecord> prefix,PoolQueryRequest request,std::vector<uint32_t> candidateCodes) {
 return std::async(std::launch::async,[initial,resources=std::move(resources),prefix=std::move(prefix),request,candidateCodes=std::move(candidateCodes)] {
  PoolQueryEvidence result;result.request_=request;
  if(!resources || request.context.resourceVersion!=resources->Fingerprint() || request.acceptedPrefix!=PrefixDigest(prefix) ||
     request.context.historyCursor!=prefix.size() || request.stage!=PoolQueryStage::TargetCheck)
   throw std::runtime_error("Stale pool search input");
  for(uint32_t code:candidateCodes) {
   try {
    resources->Card(code);
    auto state=std::make_shared<PoolQueryState>();state->context=request.context;state->prefix=request.acceptedPrefix;state->match=request;state->witness=code;
    auto candidate=Replay(initial,resources,prefix,state);
    if(state->applied && state->witnessAccepted && nativePrompt(candidate->Current()))result.candidates_.push_back(code);
   }catch(const std::exception& error){result.errors_.push_back(std::to_string(code)+": "+error.what());}
  }
  return result;
 });
}

void PoolQueryGate::Install(std::unique_ptr<CoreDriver>& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& current,
 const PoolQueryDiscovery& discovery,const PoolQueryEvidence& evidence) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 validate(*live,prefix,current);
 if(!context(current,discovery.context) || discovery.acceptedPrefix!=PrefixDigest(prefix) || live->DiagnosticState()!=discovery.diagnostic ||
    evidence.candidates_.empty() || !context(current,evidence.request_.context) ||
    !std::any_of(discovery.requests.begin(),discovery.requests.end(),[&](const auto& request){return same(request,evidence.request_);}))
  throw std::runtime_error("Stale or unproven pool evidence");
 auto state=std::make_shared<PoolQueryState>();state->context=current;state->prefix=discovery.acceptedPrefix;state->match=evidence.request_;state->evidence=true;
 auto candidate=Replay(live->Initial(),live->Resources(),prefix,state);
 if(!state->applied || !nativePrompt(candidate->Current()))throw std::runtime_error("Evidence did not produce a native action prompt");
 // Recheck immediately before publishing. All candidate outputs stayed private.
 validate(*live,prefix,current);
 if(live->DiagnosticState()!=discovery.diagnostic)throw std::runtime_error("Pool installation state changed");
 live.swap(candidate);
}
void PoolQueryGate::RecomputeActual(std::unique_ptr<CoreDriver>& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& current) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 validate(*live,prefix,current);auto diagnostic=live->DiagnosticState();
 auto candidate=Replay(live->Initial(),live->Resources(),prefix,nullptr);
 validate(*live,prefix,current);if(live->DiagnosticState()!=diagnostic)throw std::runtime_error("Actual-only installation state changed");
 live.swap(candidate);
}
std::vector<PoolQueryRequest> PoolQueryGate::PendingQueries(const CoreDriver& driver) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());return driver.poolQuery_?driver.poolQuery_->requests:std::vector<PoolQueryRequest>{};
}
void PoolQueryGate::ResponseSubmitted(CoreDriver& driver,const Bytes& response,Origin origin) {
 if(!driver.poolQuery_)return;
 auto& state=*driver.poolQuery_;state.rollback=std::make_shared<PoolQueryState>(state);
 state.records.push_back({driver.boundary_.checkpoint.player,origin,response,driver.boundary_.checkpoint});
 state.context.checkpoint=driver.boundary_.checkpoint;state.context.historyCursor=state.records.size();
 state.prefix=PrefixDigest(state.records);state.requests.clear();state.invocation=0;
 state.match.reset();state.evidence=false;
}
void PoolQueryGate::ResponseRejected(CoreDriver& driver) {
 if(driver.poolQuery_ && driver.poolQuery_->rollback)driver.poolQuery_=driver.poolQuery_->rollback;
}
void PoolQueryGate::ResponseAccepted(CoreDriver& driver) {
 if(driver.poolQuery_)driver.poolQuery_->rollback.reset();
}
}
