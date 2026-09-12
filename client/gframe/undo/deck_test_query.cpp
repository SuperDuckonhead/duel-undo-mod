#include "deck_test_query.h"
#include "../../ocgcore/duel.h"
#include "../../ocgcore/card.h"
#include "../../ocgcore/effect.h"
#include "../../ocgcore/field.h"
#include "../../ocgcore/interpreter.h"
#include "../../ocgcore/ocgapi.h"
#include "../../ocgcore/group.h"
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
 bool observe{};
 std::vector<PoolQueryObservation> observations;
 std::vector<size_t> scopes;
 std::map<Digest,uint64_t> scopeInvocations;
 std::map<std::string,Digest> hashes;
 std::vector<PoolQueryRequest> milestones;
 std::optional<PoolQueryRequest> selectionQuery;
 std::optional<PoolIntroductionRecord> preparation;
 std::vector<PoolIntroductionRecord> introductions;
 bool creating{},sourceCreated{},selectionBound{};
};
namespace {
void word(Bytes& out,uint64_t n) {for(unsigned i=0;i<8;++i)out.push_back(uint8_t(n>>(8*i)));}
void bytes(Bytes& out,const Bytes& in) {word(out,in.size());out.insert(out.end(),in.begin(),in.end());}
void digest(Bytes& out,const Digest& in) {out.insert(out.end(),in.begin(),in.end());}
bool position(const Checkpoint& a,const Checkpoint& b) {
 return a.player==b.player && a.prompt==b.prompt && a.canonicalState==b.canonicalState && a.transcriptDigest==b.transcriptDigest;
}
bool sameGeneration(const DeckTestContext& a,const DeckTestContext& b) {
 return a.session.value==b.session.value && a.branch.value==b.branch.value &&
  a.resourceVersion==b.resourceVersion && a.modeGeneration==b.modeGeneration;
}
bool context(const DeckTestContext& a,const DeckTestContext& b) {
 return sameGeneration(a,b) && a.historyCursor==b.historyCursor && position(a.checkpoint,b.checkpoint) &&
  a.checkpoint.clock.remainingMs==b.checkpoint.clock.remainingMs && a.checkpoint.aiLogCursor==b.checkpoint.aiLogCursor;
}
bool same(const PoolQueryRequest& a,const PoolQueryRequest& b) {
 return context(a.context,b.context) && a.acceptedPrefix==b.acceptedPrefix && a.script==b.script &&
  a.parameters==b.parameters && a.callState==b.callState && a.handler==b.handler &&
  a.effectRegistration==b.effectRegistration && a.invocation==b.invocation && a.effectCode==b.effectCode &&
  a.callsite==b.callsite && a.selfLocation==b.selfLocation && a.opponentLocation==b.opponentLocation &&
  a.minimum==b.minimum && a.maximum==b.maximum && a.player==b.player && a.stage==b.stage && a.role==b.role && a.source==b.source &&
  a.api==b.api && a.semantic==b.semantic && a.helper==b.helper && a.scope==b.scope && a.parentScope==b.parentScope &&
  a.target==b.target && a.procedureRegistration==b.procedureRegistration;
}
Digest registeredScript() {
 const char* hex="14432383c12ce67c8b171ff9e96326130714f676f505b3472b4c898969218141";
 Digest out{};auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
 for(size_t i=0;i<out.size();++i)out[i]=uint8_t(nibble(hex[2*i])*16+nibble(hex[2*i+1]));return out;
}
void validate(const CoreDriver& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& token,bool privateBoundary=false) {
 if(Validate(token)!=DeckTestError::None || !token.modeGeneration || token.historyCursor!=prefix.size() ||
    token.resourceVersion!=live.Resources()->Fingerprint() ||
    (live.Current().kind!=BoundaryKind::AwaitResponse && !(privateBoundary && live.Current().kind==BoundaryKind::AwaitPoolQuery)) ||
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
Digest fromHex(const char* hex) {
 Digest out{};auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
 for(size_t i=0;i<out.size();++i)out[i]=uint8_t(nibble(hex[2*i])*16+nibble(hex[2*i+1]));return out;
}
bool tableFunction(lua_State* L,int index,const char* table,const char* name) {
 index=lua_absindex(L,index);const int top=lua_gettop(L);lua_getglobal(L,table);
 if(!lua_istable(L,-1)){lua_settop(L,top);return false;}
 lua_getfield(L,-1,name);bool equal=lua_rawequal(L,index,-1);lua_settop(L,top);return equal;
}
Digest functionIdentity(lua_State* L,int index) {
 if(!lua_isfunction(L,index))return {};
 lua_pushvalue(L,index);lua_Debug info{};lua_getinfo(L,">S",&info);
 Bytes b;const std::string source=info.source?info.source:"";bytes(b,Bytes(source.begin(),source.end()));word(b,info.linedefined);word(b,info.lastlinedefined);
 return Sha256(b);
}
std::vector<CardReference> references(const std::vector<card*>& cards) {
 std::vector<CardReference> out;
 for(auto* c:cards)out.push_back({CardReferenceKind::Existing,{c->cardid},c->data.code,c->owner,
  c->current.controler==0 && c->current.location==LOCATION_DECK?CardSource::OwnMainDeck:
  c->current.controler==0 && c->current.location==LOCATION_EXTRA && c->is_position(POS_FACEDOWN)?CardSource::OwnFacedownExtraDeck:CardSource::ExistingState,
  CardLocation{c->current.controler,c->current.location,c->current.sequence,c->current.position}});
 std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.instance.value<b.instance.value;});return out;
}
void membership(Bytes& b,const std::vector<CardReference>& cards) {
 word(b,cards.size());for(auto& c:cards){word(b,c.instance.value);word(b,c.code);word(b,c.owner);word(b,uint8_t(c.source));word(b,c.location->controller);word(b,c.location->zone);word(b,c.location->sequence);word(b,c.location->position);}
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

void PoolQueryGate::Observe(CoreDriver& driver,lua_State* L,native_query_api nativeApi,bool enter,
 const std::vector<card*>& cards,card* target,int32_t result) {
 auto& state=*driver.poolQuery_;const size_t absent=size_t(-1);
 if(!enter) {
  if(state.scopes.empty())throw std::runtime_error("Unbalanced native query scope");
  const auto index=state.scopes.back();state.scopes.pop_back();if(index==absent)return;
  auto& observation=state.observations.at(index);observation.members=references(cards);observation.result=result;
  observation.afterCallState=Sha256(driver.DiagnosticState());observation.completed=true;return;
 }
 size_t parent=absent;for(auto it=state.scopes.rbegin();it!=state.scopes.rend();++it)if(*it!=absent){parent=*it;break;}
 state.scopes.push_back(absent);
 auto* pd=reinterpret_cast<duel*>(driver.handle_);auto& core=pd->game_field->core;
 effect* e=pd->pool_target_check?pd->pool_target_check:core.reason_effect;
 if(parent!=absent) {
  const auto& identity=state.observations[parent].request;
  e=nullptr;for(auto* candidate:pd->effects)if(candidate->handler && candidate->handler->cardid==identity.handler.value && candidate->registration_ordinal==identity.effectRegistration){e=candidate;break;}
 }
 if(!e || !e->handler || e->handler->current.controler!=0 || !(e->type&EFFECT_TYPE_ACTIVATE) || e->code!=EVENT_FREE_CHAIN)return;
 uint32_t code=e->handler->data.code;if(code!=44362883 && code!=75500286)return;
 auto resourceHash=[&](const std::string& path) {auto found=state.hashes.find(path);if(found!=state.hashes.end())return found->second;return state.hashes.emplace(path,Sha256(driver.Resources()->Read(path))).first->second;};
 const auto script=resourceHash("script/c"+std::to_string(code)+".lua");
 if(script!=fromHex(code==44362883?"df65c2875fe485cab0ba106f2f9a8926af85e5616f2c3aa9d20125becbf85469":"45789f7d9fea6da47b798b2d08ac1612521c0ad9d603e9120a8babcb2b867e04"))return;
 Digest helper{};if(code==44362883){helper=resourceHash("script/procedure.lua");if(helper!=fromHex("df887c18619374f825fc14f5a5f05f6a090c910adbb52933bd8462c773973baa"))return;}
 const auto api=PoolQueryApi(nativeApi);const int top=lua_gettop(L);
 lua_Debug caller{};if(!lua_getstack(L,1,&caller) || !lua_getinfo(L,"flS",&caller))return;
 const int callerIndex=lua_gettop(L);
 const bool targetCaller=([&]{lua_rawgeti(L,LUA_REGISTRYINDEX,e->target);bool equal=lua_rawequal(L,callerIndex,-1);lua_pop(L,1);return equal;})();
 const bool operationCaller=([&]{lua_rawgeti(L,LUA_REGISTRYINDEX,e->operation);bool equal=lua_rawequal(L,callerIndex,-1);lua_pop(L,1);return equal;})();
 const bool materialsCaller=tableFunction(L,callerIndex,"FusionSpell","GetFusionMaterial");
 const bool checkCaller=tableFunction(L,callerIndex,"FusionSpell","SummonTargetFilter");
 lua_pop(L,1);
 auto integerAt=[&](int n,int64_t value){return lua_isinteger(L,n) && lua_tointeger(L,n)==value;};
 bool registered=false;PoolQuerySemantic semantic=PoolQuerySemantic::SingleTarget;
 uint32_t self=0,opponent=0,min=0,max=0;
 if(code==75500286) {
  if(api==PoolQueryApi::ExistingMatching)registered=targetCaller && top==6 && tableFunction(L,1,"Card","IsAbleToRemove") && integerAt(2,0) && integerAt(3,LOCATION_DECK) && integerAt(4,0) && integerAt(5,1) && lua_isnil(L,6);
  if(api==PoolQueryApi::SelectMatching)registered=operationCaller && top==8 && tableFunction(L,2,"Card","IsAbleToRemove") && integerAt(1,0) && integerAt(3,0) && integerAt(4,LOCATION_DECK) && integerAt(5,0) && integerAt(6,1) && integerAt(7,1) && lua_isnil(L,8);
  self=LOCATION_DECK;min=max=1;
 } else {
  switch(api) {
   case PoolQueryApi::ExistingMatching:
    registered=targetCaller && top==6 && lua_isfunction(L,1) && integerAt(2,0) && integerAt(3,LOCATION_EXTRA) && integerAt(4,0) && integerAt(5,1) && lua_isnil(L,6);
    semantic=PoolQuerySemantic::FusionTarget;self=LOCATION_EXTRA;min=max=1;break;
   case PoolQueryApi::MatchingGroup:
    if(operationCaller && top==5 && lua_isfunction(L,1) && integerAt(2,0) && integerAt(3,LOCATION_EXTRA) && integerAt(4,0) && lua_isnil(L,5)) {registered=true;semantic=PoolQuerySemantic::FusionTarget;self=LOCATION_EXTRA;}
    else if(materialsCaller && top==6 && tableFunction(L,1,"Card","IsHasEffect") && integerAt(2,0) && lua_isnil(L,5) && integerAt(6,EFFECT_EXTRA_FUSION_MATERIAL) &&
     ((integerAt(3,LOCATION_EXTRA)&&integerAt(4,0)) || (integerAt(3,0)&&integerAt(4,LOCATION_ONFIELD)))) {
     registered=true;semantic=PoolQuerySemantic::ExtraMaterial;self=uint32_t(lua_tointeger(L,3));opponent=uint32_t(lua_tointeger(L,4));
    }break;
   case PoolQueryApi::FusionMaterials:
    registered=materialsCaller && top==2 && integerAt(1,0) && lua_isinteger(L,2);
    semantic=PoolQuerySemantic::MaterialUniverse;self=uint32_t(lua_tointeger(L,2));break;
   case PoolQueryApi::CheckFusion:
    registered=checkCaller && top==4 && target && lua_isnil(L,3) && integerAt(4,0);
    semantic=PoolQuerySemantic::WholeFusion;break;
   case PoolQueryApi::FusionProcedure:
    registered=parent!=absent && state.observations[parent].request.api==PoolQueryApi::CheckFusion && target;
    semantic=PoolQuerySemantic::WholeFusion;break;
   case PoolQueryApi::SelectFusion:case PoolQueryApi::SelectedFusion:
    registered=operationCaller && top==5 && integerAt(1,0) && target && lua_isnil(L,4) && integerAt(5,0);
    semantic=PoolQuerySemantic::MaterialSelection;break;
   default:break;
  }
 }
 if(!registered)return;
 if(code==44362883 && materialsCaller) {
  // Read the typed target argument from the exact pinned helper's live frame.
  // This runs separately in each Lua state. No registry ref, upvalue, closure,
  // or userdata is retained, and sibling targets never depend on call order.
  for(int depth=1;depth<32;++depth) {
   lua_Debug frame{};if(!lua_getstack(L,depth,&frame))break;
   if(!lua_getinfo(L,"f",&frame))break;
   const bool helperFrame=tableFunction(L,-1,"FusionSpell","GetMaterialsGroupForTargetCard");lua_pop(L,1);
   if(!helperFrame)continue;
   const char* name=lua_getlocal(L,&frame,1);
   if(name){if(std::string(name)=="tc" && lua_isuserdata(L,-1)){auto* c=*static_cast<card**>(lua_touserdata(L,-1));if(pd->cards.count(c))target=c;}lua_pop(L,1);}
   break;
  }
  if(!target)return;
 }
 PoolQueryObservation observation;auto& request=observation.request;
 request.context=state.context;request.acceptedPrefix=state.prefix;request.script=script;request.helper=helper;
 request.handler={e->handler->cardid};request.effectRegistration=e->registration_ordinal;request.effectCode=e->code;
 request.api=api;request.semantic=semantic;request.callsite=caller.currentline;request.selfLocation=self;request.opponentLocation=opponent;request.minimum=min;request.maximum=max;
 request.stage=pd->pool_target_check?PoolQueryStage::TargetCheck:PoolQueryStage::ResolutionSelection;
 if(parent!=absent){request.parentScope=state.observations[parent].request.scope;request.stage=state.observations[parent].request.stage;}
 request.role=semantic==PoolQuerySemantic::SingleTarget || semantic==PoolQuerySemantic::FusionTarget?
  (request.stage==PoolQueryStage::TargetCheck?SelectionRole::ActivationTarget:SelectionRole::ResolutionTarget):SelectionRole::ResolutionMaterial;
 request.source=semantic==PoolQuerySemantic::FusionTarget?CardSource::OwnFacedownExtraDeck:
  semantic==PoolQuerySemantic::ExtraMaterial?CardSource::ExistingState:CardSource::OwnMainDeck;
 if(target){request.target={target->cardid};observation.targetScript=resourceHash("script/c"+std::to_string(target->data.code)+".lua");auto found=target->single_effect.find(EFFECT_FUSION_MATERIAL);if(found!=target->single_effect.end())request.procedureRegistration=found->second->registration_ordinal;}
 if(core.reason_effect && core.reason_effect->handler){observation.reasonHandler={core.reason_effect->handler->cardid};observation.reasonRegistration=core.reason_effect->registration_ordinal;}
 observation.input=references(cards);
 Bytes parameters;word(parameters,uint8_t(api));word(parameters,top);word(parameters,self);word(parameters,opponent);word(parameters,min);word(parameters,max);
 // Only the exact registered shapes above reach normalization. Userdata are
 // represented by typed card/group identities, never addresses or registry IDs.
 if(api!=PoolQueryApi::FusionProcedure)for(int i=1;i<=top;++i) {
  word(parameters,lua_type(L,i));if(lua_isinteger(L,i))word(parameters,lua_tointeger(L,i));
  else if(lua_isfunction(L,i))digest(parameters,functionIdentity(L,i));
 }
 word(parameters,request.target.value);word(parameters,request.procedureRegistration);membership(parameters,observation.input);
 request.parameters=Sha256(parameters);request.callState=Sha256(driver.DiagnosticState());
 Bytes scope;digest(scope,request.parentScope);word(scope,uint8_t(api));word(scope,uint8_t(semantic));word(scope,request.callsite);word(scope,request.handler.value);word(scope,request.effectRegistration);word(scope,request.target.value);request.scope=Sha256(scope);
 request.invocation=++state.scopeInvocations[request.scope];
 const int saved=lua_gettop(L);lua_getglobal(L,"aux");
 if(lua_istable(L,-1)){
  lua_getfield(L,-1,"FCheckAdditional");observation.additionalCheckPresent=lua_isfunction(L,-1);observation.additionalCheck=functionIdentity(L,-1);lua_pop(L,1);
  lua_getfield(L,-1,"FGoalCheckAdditional");observation.additionalGoalPresent=lua_isfunction(L,-1);observation.additionalGoal=functionIdentity(L,-1);
 }lua_settop(L,saved);
 // Record the fixture's alternative-route context without invoking effect
 // filters a second time. Frozen state also remains in callState.
 for(auto* candidate:pd->effects)if(candidate->code==EFFECT_CHAIN_MATERIAL)++observation.chainMaterialEffects;
 else if(candidate->code==EFFECT_EXTRA_FUSION_MATERIAL)++observation.extraMaterialEffects;
 state.requests.push_back(request);state.scopes.back()=state.observations.size();state.observations.push_back(std::move(observation));
}

std::optional<PoolQueryRequest> PoolQueryGate::IdentifyD01(CoreDriver& driver,lua_State* L,bool selection) {
 auto& state=*driver.poolQuery_;
 auto* pd=reinterpret_cast<duel*>(driver.handle_);
 auto* f=pd->game_field;
 effect* e=selection?f->core.reason_effect:pd->pool_target_check;
 if(!e || !e->handler || e->handler->data.code!=62962630 || e->handler->current.controler!=0 ||
    e->type!=(EFFECT_TYPE_SINGLE|EFFECT_TYPE_TRIGGER_O|EFFECT_TYPE_ACTIONS) ||
    (e->code!=EVENT_SUMMON_SUCCESS && e->code!=EVENT_SPSUMMON_SUCCESS))return std::nullopt;
 const int filter=selection?2:1;
 const int total=selection?8:6;
 if(lua_gettop(L)!=total || !lua_isnil(L,total) || !functionEquals(L,filter,"thfilter"))return std::nullopt;
 // Exact supported parameter shape; unsupported arguments retain real semantics.
 const int selfIndex=filter+1;
 for(int index=selfIndex;index<total;++index)if(!lua_isinteger(L,index))return std::nullopt;
 if(lua_tointeger(L,selfIndex)!=0 || (selection && lua_tointeger(L,1)!=0) ||
    lua_tointeger(L,selfIndex+1)!=LOCATION_DECK || lua_tointeger(L,selfIndex+2)!=0 ||
    lua_tointeger(L,selfIndex+3)!=1 || (selection && lua_tointeger(L,7)!=1))return std::nullopt;
 // Verify the immediate Lua caller is the registered original target/operation.
 lua_Debug ar{};
 if(!lua_getstack(L,1,&ar) || !lua_getinfo(L,"fl",&ar))return std::nullopt;
 bool caller=functionEquals(L,lua_gettop(L),selection?"thop":"thtg");
 lua_pop(L,1);
 if(!caller)return std::nullopt;
 PoolQueryRequest request;
 request.context=state.context;
 request.acceptedPrefix=state.prefix;
 request.script=registeredScript();
 request.handler={e->handler->cardid};
 request.effectRegistration=e->registration_ordinal;
 request.effectCode=e->code;
 request.invocation=++state.invocation;
 request.callsite=ar.currentline;
 request.player=0;
 request.selfLocation=LOCATION_DECK;
 request.minimum=request.maximum=1;
 request.stage=selection?PoolQueryStage::ResolutionSelection:PoolQueryStage::TargetCheck;
 request.api=selection?PoolQueryApi::SelectMatching:PoolQueryApi::ExistingMatching;
 Bytes parameters;
 word(parameters,total);
 word(parameters,selection);
 for(int index=selfIndex;index<total;++index)word(parameters,lua_tointeger(L,index));
 request.parameters=Sha256(parameters);
 request.callState=Sha256(driver.DiagnosticState());
 return request;
}

bool PoolQueryGate::Query(CoreDriver& driver,lua_State* L,bool selection,bool actual) {
 auto& state=*driver.poolQuery_;
 if(state.creating)return actual;
 auto identity=selection?state.selectionQuery:IdentifyD01(driver,L,false);
 if(selection)state.selectionQuery.reset();
 if(!identity)return actual;
 const auto& request=*identity;
 auto* pd=reinterpret_cast<duel*>(driver.handle_);
 auto* f=pd->game_field;
 auto* e=selection?f->core.reason_effect:pd->pool_target_check;
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

void PoolQueryGate::Select(CoreDriver& driver,lua_State* L,group* filtered) {
 auto& state=*driver.poolQuery_;
 if(state.creating)return;
 if(!filtered)state.selectionQuery=IdentifyD01(driver,L,true);
 if(!state.selectionQuery || !state.preparation || !same(*state.selectionQuery,state.preparation->query))return;
 auto& record=*state.preparation;
 auto* pd=reinterpret_cast<duel*>(driver.handle_);
 if(!filtered) {
  if(state.sourceCreated)throw std::runtime_error("Source plan already consumed");
  // Initialization can execute nested Lua queries. They retain native rules
  // and cannot recursively consume the outer plan or overwrite its identity.
  struct Creating {
   bool& flag;
   explicit Creating(bool& value):flag(value){flag=true;}
   ~Creating(){flag=false;}
  } creating(state.creating);
  const auto& selected=record.plan.cards.at(0);
  auto* created=new_card_result(driver.handle_,selected.code,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE);
  driver.CheckFailure();
  if(!created || !pd->cards.count(created) || created->owner!=0 || created->current.controler!=0 ||
     created->current.location!=LOCATION_DECK || created->current.position!=POS_FACEDOWN_DEFENSE)
   throw std::runtime_error("Source initialization did not produce own main-deck card");
  record.nativeInstance={created->cardid};
  record.sourceLocation={0,LOCATION_DECK,created->current.sequence,POS_FACEDOWN_DEFENSE};
  state.sourceCreated=true;
 } else {
  auto found=std::find_if(filtered->container.begin(),filtered->container.end(),[&](card* c){return c->cardid==record.nativeInstance.value;});
  if(!state.sourceCreated || found==filtered->container.end())
   throw std::runtime_error("Confirmed source rejected by original selection filter");
  auto* selected=*found;
  // The original closure checked the whole actual group. Only the finite,
  // confirmed subset enters the bounded native response index space.
  filtered->container.clear();
  filtered->container.insert(selected);
  state.selectionBound=true;
 }
}

void PoolQueryGate::Attach(CoreDriver& candidate,std::shared_ptr<PoolQueryState> state) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 candidate.poolQuery_=std::move(state);
 auto* pd=reinterpret_cast<duel*>(candidate.handle_);
 pd->pool_query={};
 pd->pool_observe={};
 pd->pool_select={};
 if(!candidate.poolQuery_)return;
 pd->pool_query=[driver=&candidate](lua_State* L,bool selection,bool actual) {
   if(!driver->callbackFailure_.empty())return actual;
   try{return Query(*driver,L,selection,actual);}catch(const std::exception& error){
    if(driver->callbackFailure_.empty())driver->callbackFailure_=error.what();
    return actual;
   }
  };
 pd->pool_select=[driver=&candidate](lua_State* L,group* filtered) {
  if(!driver->callbackFailure_.empty())return;
  try {Select(*driver,L,filtered);}
  catch(const std::exception& error){if(driver->callbackFailure_.empty())driver->callbackFailure_=error.what();}
 };
 if(candidate.poolQuery_->observe)pd->pool_observe=[driver=&candidate](lua_State* L,native_query_api api,bool enter,const card_set* members,card* target,int32_t result) {
   try {Observe(*driver,L,api,enter,members?std::vector<card*>(members->begin(),members->end()):std::vector<card*>{},target,result);}
   catch(const std::exception& error){if(driver->callbackFailure_.empty())driver->callbackFailure_=error.what();}
  };
}

std::unique_ptr<CoreDriver> PoolQueryGate::Replay(const InitialState& initial,std::shared_ptr<const ResourceView> resources,
 const std::vector<ResponseRecord>& prefix,std::shared_ptr<PoolQueryState> state,const std::vector<PoolQueryRequest>& milestones) {
 if(state && Sha256(resources->Read("script/c62962630.lua"))!=registeredScript())
  throw std::runtime_error("Unsupported c62962630 script hash");
 size_t previous=0;
 for(size_t i=0;i<milestones.size();++i) {
  const auto& m=milestones[i];
  const auto cursor=m.context.historyCursor;
  if(cursor>prefix.size() || (i && cursor<=previous) || m.stage!=PoolQueryStage::TargetCheck ||
     m.context.resourceVersion!=resources->Fingerprint() ||
     !state || !sameGeneration(m.context,state->context) ||
     m.acceptedPrefix!=PrefixDigest(std::vector<ResponseRecord>(prefix.begin(),prefix.begin()+cursor)))
   throw std::runtime_error("Invalid evidence milestone history");
  previous=cursor;
 }
 auto candidate=CoreDriver::Create(initial,std::move(resources));
 size_t milestoneIndex=0;
 auto advance=[&](size_t cursor) {
  std::shared_ptr<PoolQueryState> current=cursor==prefix.size()?state:nullptr;
  const bool milestone=milestoneIndex<milestones.size() && milestones[milestoneIndex].context.historyCursor==cursor;
  if(milestone) {
   if(!current)current=std::make_shared<PoolQueryState>();
   const auto& m=milestones[milestoneIndex++];
   current->context=m.context;
   current->prefix=m.acceptedPrefix;
   current->match=m;
   current->evidence=true;
  }
  if(current) {
   current->records.assign(prefix.begin(),prefix.begin()+cursor);
   current->milestones=milestones;
  }
  Attach(*candidate,current);
  auto boundary=candidate->Advance();
  if(milestone && (!current->applied || !nativePrompt(boundary)))
   throw std::runtime_error("Evidence milestone did not recreate native activation");
  return boundary;
 };
 auto boundary=advance(0);
 for(size_t i=0;i<prefix.size();++i) {
  const auto& record=prefix[i];
  if(record.player!=record.before.player || record.player>1 || uint8_t(record.origin)>uint8_t(Origin::Bot) ||
     boundary.kind!=BoundaryKind::AwaitResponse || !position(boundary.checkpoint,record.before))
   throw std::runtime_error("Pool replay prefix diverged at "+std::to_string(i));
  Attach(*candidate,nullptr);
  candidate->Submit(record.response,record.origin);
  boundary=advance(i+1);
  if(boundary.rejectedResponse)throw std::runtime_error("Pool replay response rejected");
 }
 if(boundary.kind==BoundaryKind::Failed || boundary.kind==BoundaryKind::Finished)
  throw std::runtime_error("Pool replay failed: "+boundary.failure);
 return candidate;
}

PoolQueryDiscovery PoolQueryGate::Discover(const CoreDriver& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& current) {
 validate(live,prefix,current);PoolQueryDiscovery result;result.context=current;result.acceptedPrefix=PrefixDigest(prefix);result.diagnostic=live.DiagnosticState();
 auto state=std::make_shared<PoolQueryState>();state->context=current;state->prefix=result.acceptedPrefix;state->observe=true;
 auto candidate=Replay(live.Initial(),live.Resources(),prefix,state);
 if(!position(candidate->Current().checkpoint,current.checkpoint) || candidate->DiagnosticState()!=result.diagnostic)
  throw std::runtime_error("Query discovery changed actual-only state");
 result.requests=state->requests;result.observations=state->observations;return result;
}

std::unique_ptr<CoreDriver> PoolQueryGate::Recreate(const CoreDriver& live,const std::vector<ResponseRecord>& prefix,const DeckTestContext& current) {
 validate(live,prefix,current,true);
 auto state=std::make_shared<PoolQueryState>();
 state->context=current;
 state->prefix=PrefixDigest(prefix);
 state->observe=true;
 std::vector<PoolQueryRequest> milestones;
 if(live.poolQuery_) {
  if(PrefixDigest(live.poolQuery_->records)!=state->prefix)throw std::runtime_error("Accepted pool prefix mismatch");
  state->context=live.poolQuery_->context;
  milestones=live.poolQuery_->milestones;
 }
 auto candidate=Replay(live.Initial(),live.Resources(),prefix,state,milestones);
 if(!position(candidate->Current().checkpoint,current.checkpoint) || candidate->DiagnosticState()!=live.DiagnosticState())
  throw std::runtime_error("Actual query recreation diverged");
 return candidate;
}
std::vector<PoolQueryObservation> PoolQueryGate::Observations(const CoreDriver& driver) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());return driver.poolQuery_?driver.poolQuery_->observations:std::vector<PoolQueryObservation>{};
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
 state->milestones.push_back(evidence.request_);
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
void PoolQueryGate::Introduce(std::unique_ptr<CoreDriver>& live,const DeckTestContext& current,
 const PoolQueryRequest& request,const SelectionPlan& plan) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 if(!live || !live->poolQuery_ || live->Current().kind!=BoundaryKind::AwaitPoolQuery)
  throw std::runtime_error("Source introduction requires a pending private selection");
 const auto& source=*live->poolQuery_;
 const auto prefix=source.records;
 validate(*live,prefix,current,true);
 auto expected=source.context;
 expected.checkpoint=live->Current().checkpoint;
 if(!context(current,expected) || !context(plan.context,current) || Validate(plan)!=DeckTestError::None ||
    plan.cards.size()!=1 || plan.role!=SelectionRole::ResolutionTarget || plan.bindingStage!=BindingStage::Resolution ||
    request.stage!=PoolQueryStage::ResolutionSelection || request.role!=plan.role || request.api!=PoolQueryApi::SelectMatching ||
    request.source!=CardSource::OwnMainDeck || request.acceptedPrefix!=PrefixDigest(prefix) ||
    !std::any_of(source.requests.begin(),source.requests.end(),[&](const auto& q){return same(q,request);}) ||
    !source.introductions.empty())
  throw std::runtime_error("Stale or unsupported single-source selection plan");
 const auto& selected=plan.cards.front();
 const auto& data=live->Resources()->Card(selected.code);
 if(selected.kind!=CardReferenceKind::NewCopy || selected.source!=CardSource::OwnMainDeck || selected.owner!=0 ||
    !(data.type&(TYPE_MONSTER|TYPE_SPELL|TYPE_TRAP)) || (data.type&(TYPE_TOKEN|TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK)))
  throw std::runtime_error("Selected card is not an own main-deck source");
 const auto before=live->DiagnosticState();
 const auto transcript=live->Transcript();
 const auto logs=live->Logs();
 PoolIntroductionRecord record;
 record.query=request;
 record.plan=plan;
 record.prefix=prefix;
 record.evidenceMilestones=source.milestones;
 record.introduced={current,selected,plan.role,plan.bindingStage};
 auto state=std::make_shared<PoolQueryState>();
 state->context=request.context;
 state->prefix=request.acceptedPrefix;
 state->preparation=record;
 auto candidate=Replay(live->Initial(),live->Resources(),prefix,state,record.evidenceMilestones);
 const auto boundary=candidate->Current();
 if(!state->sourceCreated || !state->selectionBound || boundary.kind!=BoundaryKind::AwaitResponse)
  throw std::runtime_error("Source plan did not reach its native selection");
 record=*state->preparation;
 auto* pd=reinterpret_cast<duel*>(candidate->handle_);
 const auto& cards=pd->game_field->core.select_cards;
 const auto& prompt=boundary.checkpoint.prompt;
 // Map actual identity after the native processor's sorting, never by code
 // or hidden deck sequence. This gate's finite plan has exactly one member.
 auto member=std::find_if(cards.begin(),cards.end(),[&](card* c){return c->cardid==record.nativeInstance.value;});
 if(cards.size()!=1 || member==cards.end() || prompt.size()!=14 || prompt[0]!=MSG_SELECT_CARD ||
    prompt[1]!=0 || prompt[3]!=1 || prompt[4]!=1 || prompt[5]!=1)
  throw std::runtime_error("Finite native selection identity mismatch");
 const auto slot=uint8_t(std::distance(cards.begin(),member));
 record.selection={0,Origin::Manual,{1,slot},boundary.checkpoint};
 state->preparation.reset();
 candidate->Submit(record.selection.response,record.selection.origin);
 auto after=candidate->Advance(); // All output remains in the private core.
 if(after.kind==BoundaryKind::Failed || after.rejectedResponse)
  throw std::runtime_error("Source selection continuation failed: "+after.failure);
 record.after=after.checkpoint;
 record.diagnostic=Sha256(candidate->DiagnosticState());
 state->introductions.push_back(std::move(record));
 // Prepare every allocation and event before the single no-throw publication.
 validate(*live,prefix,current,true);
 if(live->DiagnosticState()!=before || live->Transcript()!=transcript || live->Logs()!=logs)
  throw std::runtime_error("Live core changed during source preparation");
 live.swap(candidate);
}
std::vector<PoolIntroductionRecord> PoolQueryGate::Introductions(const CoreDriver& driver) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 return driver.poolQuery_?driver.poolQuery_->introductions:std::vector<PoolIntroductionRecord>{};
}
std::vector<ResponseRecord> PoolQueryGate::AcceptedResponses(const CoreDriver& driver) {
 std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());
 return driver.poolQuery_?driver.poolQuery_->records:std::vector<ResponseRecord>{};
}
std::unique_ptr<CoreDriver> PoolQueryGate::ReplayIntroduction(const InitialState& initial,
 std::shared_ptr<const ResourceView> resources,const PoolIntroductionRecord& record) {
 if(record.version!=1 || record.insertionSequence!=SEQ_DECKTOP || Validate(record.introduced)!=DeckTestError::None ||
    record.plan.cards.size()!=1 || !context(record.introduced.context,record.plan.context) ||
    record.introduced.role!=record.plan.role || record.introduced.bindingStage!=record.plan.bindingStage)
  throw std::runtime_error("Invalid introduction record");
 const auto& card=record.plan.cards.front();
 const auto& event=record.introduced.card;
 if(event.instance!=card.instance || event.code!=card.code || event.owner!=card.owner || event.source!=card.source)
  throw std::runtime_error("Introduction event does not match selected identity");
 auto state=std::make_shared<PoolQueryState>();
 state->context=record.query.context;
 state->prefix=record.query.acceptedPrefix;
 auto candidate=Replay(initial,std::move(resources),record.prefix,state,record.evidenceMilestones);
 Introduce(candidate,record.plan.context,record.query,record.plan);
 const auto& recreated=candidate->poolQuery_->introductions.at(0);
 if(recreated.nativeInstance!=record.nativeInstance || recreated.sourceLocation.controller!=record.sourceLocation.controller ||
    recreated.sourceLocation.zone!=record.sourceLocation.zone || recreated.sourceLocation.sequence!=record.sourceLocation.sequence ||
    recreated.sourceLocation.position!=record.sourceLocation.position ||
    PrefixDigest({recreated.selection})!=PrefixDigest({record.selection}) ||
    !position(recreated.after,record.after) || recreated.after.clock.remainingMs!=record.after.clock.remainingMs ||
    recreated.after.aiLogCursor!=record.after.aiLogCursor || recreated.diagnostic!=record.diagnostic)
  throw std::runtime_error("Introduction replay diverged");
 return candidate;
}
void PoolQueryGate::ResponseSubmitted(CoreDriver& driver,const Bytes& response,Origin origin) {
 if(!driver.poolQuery_)return;
 auto& state=*driver.poolQuery_;state.rollback=std::make_shared<PoolQueryState>(state);
 state.records.push_back({driver.boundary_.checkpoint.player,origin,response,driver.boundary_.checkpoint});
 state.context.checkpoint=driver.boundary_.checkpoint;state.context.historyCursor=state.records.size();
 state.prefix=PrefixDigest(state.records);state.requests.clear();state.invocation=0;
 state.scopeInvocations.clear();
 state.match.reset();state.evidence=false;
}
void PoolQueryGate::ResponseRejected(CoreDriver& driver) {
 if(driver.poolQuery_ && driver.poolQuery_->rollback)driver.poolQuery_=driver.poolQuery_->rollback;
}
void PoolQueryGate::ResponseAccepted(CoreDriver& driver) {
 if(driver.poolQuery_)driver.poolQuery_->rollback.reset();
}
}
