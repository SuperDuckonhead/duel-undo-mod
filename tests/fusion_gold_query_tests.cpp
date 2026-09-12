#include "core_fixture.h"
#include "undo/deck_test_query.h"
#include "common.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
using namespace undo;
static Bytes integer(uint32_t n) {return {uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};}
static uint32_t u32(const Bytes& p,size_t n) {CHECK(n+4<=p.size());return uint32_t(p[n])|(uint32_t(p[n+1])<<8)|(uint32_t(p[n+2])<<16)|(uint32_t(p[n+3])<<24);}
static std::string hex(const Digest& d) {std::ostringstream o;for(auto b:d)o<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return o.str();}
static void waiting(const Boundary& b) {if(b.kind!=BoundaryKind::AwaitResponse)throw std::runtime_error(b.failure);CHECK(!b.rejectedResponse);}
static Bytes automatic(const Checkpoint& c) {
 const auto& p=c.prompt;if(p[0]==MSG_SELECT_CHAIN)return integer(-1);
 if(p[0]==MSG_SELECT_PLACE){auto mask=u32(p,3);for(unsigned bit=0;bit<32;++bit)if(!(mask&(1u<<bit)))return {uint8_t(p[1]^(bit>=16)),uint8_t((bit%16)<8?LOCATION_MZONE:LOCATION_SZONE),uint8_t(bit%8)};}
 if(p[0]==MSG_SELECT_POSITION)return integer(POS_FACEUP_ATTACK);
 if(p[0]==MSG_SELECT_EFFECTYN || p[0]==MSG_SELECT_YESNO)return integer(0);
 throw std::runtime_error("Unexpected automatic prompt "+std::to_string(p[0]));
}
struct Run {
 std::unique_ptr<CoreDriver> core;std::vector<ResponseRecord> prefix;Boundary now;
 Run(const InitialState& s,std::shared_ptr<const ResourceView> r):core(CoreDriver::Create(s,r)),now(core->Advance()){waiting(now);}
 Run(std::unique_ptr<CoreDriver> c,std::vector<ResponseRecord> p):core(std::move(c)),prefix(std::move(p)),now(core->Current()){waiting(now);}
 void submit(Bytes b) {auto before=now.checkpoint;core->Submit(b);now=core->Advance();waiting(now);prefix.push_back({before.player,Origin::Manual,b,before});}
 void idle() {for(unsigned n=0;now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n){CHECK(n<20);CHECK(now.checkpoint.prompt[0]==MSG_SELECT_CHAIN);submit(integer(-1));}}
 int activation(uint32_t code) const {auto& p=now.checkpoint.prompt;CHECK(p[0]==MSG_SELECT_IDLECMD);size_t at=2;for(int i=0;i<5;++i){auto count=p.at(at++);at+=count*7;}auto count=p.at(at++);for(unsigned i=0;i<count;++i)if(u32(p,at+i*11)==code)return i;return -1;}
 void activate(uint32_t code) {int index=activation(code);CHECK(index>=0);submit(integer((uint32_t(index)<<16)|5));for(unsigned n=0;now.checkpoint.prompt[0]==MSG_SELECT_CHAIN || now.checkpoint.prompt[0]==MSG_SELECT_PLACE;++n){CHECK(n<20);submit(automatic(now.checkpoint));}}
 DeckTestContext context()const{return {{31},{1},prefix.size(),now.checkpoint,core->Resources()->Fingerprint(),1};}
};
static std::vector<uint32_t> codes(const std::vector<CardReference>& refs){std::vector<uint32_t> out;for(auto& r:refs)out.push_back(r.code);std::sort(out.begin(),out.end());return out;}
static void equivalent(const std::vector<PoolQueryObservation>& a,const std::vector<PoolQueryObservation>& b) {
 CHECK(!a.empty());CHECK(a.size()==b.size());
 auto refs=[](const std::vector<CardReference>& x,const std::vector<CardReference>& y){CHECK(x.size()==y.size());for(size_t i=0;i<x.size();++i){CHECK(x[i].instance==y[i].instance);CHECK(x[i].code==y[i].code);CHECK(x[i].owner==y[i].owner);CHECK(x[i].source==y[i].source);CHECK(x[i].location->zone==y[i].location->zone);CHECK(x[i].location->sequence==y[i].location->sequence);CHECK(x[i].location->controller==y[i].location->controller);CHECK(x[i].location->position==y[i].location->position);}};
 for(size_t i=0;i<a.size();++i){auto& x=a[i];auto& y=b[i];auto& p=x.request;auto& q=y.request;
  CHECK(p.acceptedPrefix==q.acceptedPrefix);CHECK(p.context.historyCursor==q.context.historyCursor);CHECK(p.context.resourceVersion==q.context.resourceVersion);
  CHECK(p.context.checkpoint.prompt==q.context.checkpoint.prompt);CHECK(p.context.checkpoint.canonicalState==q.context.checkpoint.canonicalState);
  CHECK(p.script==q.script);CHECK(p.helper==q.helper);CHECK(p.handler==q.handler);CHECK(p.effectRegistration==q.effectRegistration);CHECK(p.effectCode==q.effectCode);
  CHECK(p.stage==q.stage);CHECK(p.role==q.role);CHECK(p.source==q.source);CHECK(p.api==q.api);CHECK(p.semantic==q.semantic);CHECK(p.callsite==q.callsite);
  CHECK(p.scope==q.scope);CHECK(p.parentScope==q.parentScope);CHECK(p.parameters==q.parameters);CHECK(p.callState==q.callState);CHECK(p.invocation==q.invocation);
  CHECK(p.target==q.target);CHECK(p.procedureRegistration==q.procedureRegistration);CHECK(p.selfLocation==q.selfLocation);CHECK(p.opponentLocation==q.opponentLocation);
  CHECK(x.completed && y.completed);CHECK(x.result==y.result);CHECK(x.reasonHandler==y.reasonHandler);CHECK(x.reasonRegistration==y.reasonRegistration);CHECK(x.afterCallState==y.afterCallState);
  CHECK(x.additionalCheckPresent==y.additionalCheckPresent);CHECK(x.additionalGoalPresent==y.additionalGoalPresent);CHECK(x.additionalCheck==y.additionalCheck);CHECK(x.additionalGoal==y.additionalGoal);CHECK(x.targetScript==y.targetScript);
  CHECK(x.chainMaterialEffects==0 && y.chainMaterialEffects==0);CHECK(x.extraMaterialEffects==0 && y.extraMaterialEffects==0);refs(x.input,y.input);refs(x.members,y.members);
 }
}
static const PoolQueryObservation& find(const std::vector<PoolQueryObservation>& observations,PoolQueryApi api) {
 auto i=std::find_if(observations.begin(),observations.end(),[&](const auto& o){return o.request.api==api;});CHECK(i!=observations.end());return *i;
}
static void describe(const char* label,const std::vector<PoolQueryObservation>& observations) {
 std::cout<<label<<'\n';for(auto& o:observations){auto& q=o.request;std::cout<<" prefix="<<q.context.historyCursor<<" api="<<unsigned(q.api)<<" semantic="<<unsigned(q.semantic)<<" stage="<<unsigned(q.stage)<<" call="<<q.callsite<<" handler="<<q.handler.value<<" effect="<<q.effectRegistration<<" target="<<q.target.value<<" procedure="<<q.procedureRegistration<<" reason="<<o.reasonHandler.value<<":"<<o.reasonRegistration<<" source="<<unsigned(q.source)<<" locations="<<q.selfLocation<<":"<<q.opponentLocation<<" scope="<<hex(q.scope).substr(0,12)<<" parent="<<hex(q.parentScope).substr(0,12)<<" input=";for(auto& c:o.input)std::cout<<c.instance.value<<":"<<c.code<<",";std::cout<<" members=";for(auto& c:o.members)std::cout<<c.instance.value<<":"<<c.code<<",";std::cout<<" additional="<<o.additionalCheckPresent<<":"<<o.additionalGoalPresent<<" result="<<o.result<<'\n';}
}
struct ActualCard {uint64_t id;uint32_t code,location,status,controller,sequence,position;};
static std::vector<ActualCard> actual(const CoreDriver& c) {
 auto d=c.DiagnosticState();std::vector<ActualCard> out;size_t at=32;
 for(unsigned n=0;n<u32(d,28);++n){out.push_back({uint64_t(u32(d,at))|(uint64_t(u32(d,at+4))<<32),u32(d,at+8),u32(d,at+20),u32(d,at+32),u32(d,at+16),u32(d,at+24),u32(d,at+28)});auto overlays=u32(d,at+52);at+=56+8*overlays;}
 return out;
}
static ActualCard actual(const CoreDriver& c,StableInstanceId id) {auto cards=actual(c);auto i=std::find_if(cards.begin(),cards.end(),[&](const auto& x){return x.id==id.value;});CHECK(i!=cards.end());return *i;}
static void finishFusion(Run& run) {
 CHECK(run.now.checkpoint.prompt[0]==MSG_SELECT_CARD);run.submit({1,0});
 // The original procedure owns its interactive whole-subgroup selection.
 CHECK(run.now.checkpoint.prompt[0]==MSG_SELECT_UNSELECT_CARD);
 auto checkpoint=run.now.checkpoint;auto state=run.core->DiagnosticState();
 run.core->Submit({2,0,0});auto rejected=run.core->Advance();CHECK(rejected.rejectedResponse);CHECK(rejected.checkpoint.prompt==checkpoint.prompt);CHECK(run.core->DiagnosticState()==state);
 for(unsigned n=0;run.now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n) {
  CHECK(n<24);auto& p=run.now.checkpoint.prompt;
  if(p[0]==MSG_SELECT_UNSELECT_CARD){CHECK(p.at(6)>0);run.submit({1,0});}
  else run.submit(automatic(run.now.checkpoint));
 }
}
static InitialState initial(std::shared_ptr<const ResourceView> r,uint32_t spell,bool legal=true) {
 InitialState s;s.seed.resize(SEED_COUNT,42);s.duelOptions=(5u<<16)|DUEL_PSEUDO_SHUFFLE;s.resourceDigest=r->Fingerprint();
 s.cards.push_back({spell,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK});
 s.cards.push_back({89631139,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 s.cards.push_back({89631139,1,1,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 if(spell==44362883){s.cards.push_back({legal?68468459u:89631139u,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});s.cards.push_back({87746184,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});}
 return s;
}
int main(int argc,char** argv) {
 try {
  CHECK(argc==2);auto r=ResourceView::Capture(argv[1]);std::cout<<"resources="<<hex(r->Fingerprint())<<'\n';
  const std::pair<const char*,const char*> pinned[]={
   {"c44362883.lua","df65c2875fe485cab0ba106f2f9a8926af85e5616f2c3aa9d20125becbf85469"},
   {"procedure.lua","df887c18619374f825fc14f5a5f05f6a090c910adbb52933bd8462c773973baa"},
   {"c87746184.lua","8336d726dcdd1720461111283d5a037d7689df96338300ab5843b2a3acb05db8"},
   {"c75500286.lua","45789f7d9fea6da47b798b2d08ac1612521c0ad9d603e9120a8babcb2b867e04"}};
  for(auto& p:pinned){auto hash=hex(Sha256(r->Read(std::string("script/")+p.first)));std::cout<<p.first<<'='<<hash<<'\n';CHECK(hash==p.second);}
  Run fusion(initial(r,44362883),r);fusion.idle();CHECK(fusion.activation(44362883)>=0);
  // Catches missing registration of the actual expansion helper's native
  // target check. The original physical-world closure must have executed.
  auto precheck=PoolQueryGate::Discover(*fusion.core,fusion.prefix,fusion.context());
  CHECK(!precheck.requests.empty());
  auto precheckAgain=PoolQueryGate::Discover(*fusion.core,fusion.prefix,fusion.context());equivalent(precheck.observations,precheckAgain.observations);describe("FUSION PRECHECK",precheck.observations);
  auto& outer=find(precheck.observations,PoolQueryApi::ExistingMatching);CHECK(outer.request.semantic==PoolQuerySemantic::FusionTarget);CHECK(outer.request.source==CardSource::OwnFacedownExtraDeck);CHECK(outer.result==1);
  auto& check=find(precheck.observations,PoolQueryApi::CheckFusion);CHECK(check.result==1);CHECK((codes(check.input)==std::vector<uint32_t>{68468459,89631139}));CHECK(check.additionalCheckPresent && check.additionalGoalPresent);
  // A material query is identified by its target, not a flat call ordinal:
  // adding a second extra-deck candidate must not alias its material scope.
  CHECK(find(precheck.observations,PoolQueryApi::FusionMaterials).request.target==check.request.target);
  auto& procedure=find(precheck.observations,PoolQueryApi::FusionProcedure);CHECK(procedure.result==1);CHECK(procedure.request.handler==outer.request.handler);CHECK(procedure.reasonHandler==check.request.target);CHECK(procedure.reasonRegistration==check.request.procedureRegistration);CHECK(procedure.request.parentScope==check.request.scope);
  for(auto& o:precheck.observations)if(o.request.semantic==PoolQuerySemantic::ExtraMaterial){CHECK(o.members.empty());CHECK(o.request.scope!=outer.request.scope);CHECK(o.request.parentScope==outer.request.scope);CHECK(o.request.source==CardSource::ExistingState);}
  CHECK(find(precheck.observations,PoolQueryApi::FusionMaterials).request.selfLocation==(LOCATION_HAND|LOCATION_DECK|LOCATION_MZONE));
  fusion.activate(44362883);CHECK(fusion.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(fusion.now.checkpoint.prompt,6)==87746184);
  Run first(PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context()),fusion.prefix);
  Run second(PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context()),fusion.prefix);
  auto atTarget=PoolQueryGate::Observations(*first.core);equivalent(atTarget,PoolQueryGate::Observations(*second.core));describe("FUSION OPERATION",atTarget);
  CHECK(find(atTarget,PoolQueryApi::MatchingGroup).request.semantic==PoolQuerySemantic::FusionTarget);CHECK((codes(find(atTarget,PoolQueryApi::MatchingGroup).members)==std::vector<uint32_t>{87746184}));
  auto initialCount=actual(*first.core).size();finishFusion(first);finishFusion(second);
  auto completed=PoolQueryGate::Observations(*first.core);equivalent(completed,PoolQueryGate::Observations(*second.core));describe("FUSION COMPLETED",completed);
  auto& chosen=find(completed,PoolQueryApi::SelectedFusion);CHECK(chosen.result==1);CHECK((codes(chosen.members)==std::vector<uint32_t>{68468459,89631139}));CHECK(chosen.additionalCheckPresent && chosen.additionalGoalPresent);
  CHECK(chosen.request.handler==outer.request.handler);CHECK(chosen.request.stage==PoolQueryStage::ResolutionSelection);CHECK(chosen.request.role==SelectionRole::ResolutionMaterial);
  CHECK(actual(*first.core).size()==initialCount);CHECK(first.now.checkpoint.canonicalState==second.now.checkpoint.canonicalState);
  finishFusion(fusion);CHECK(fusion.core->DiagnosticState()==first.core->DiagnosticState());CHECK(PoolQueryGate::Observations(*fusion.core).empty());
  auto summoned=actual(*first.core,chosen.request.target);CHECK(summoned.location==LOCATION_MZONE);CHECK(summoned.status&STATUS_PROC_COMPLETE);
  for(auto& material:chosen.members){auto c=actual(*first.core,material.instance);CHECK(c.location==LOCATION_GRAVE);auto query=first.core->QueryCard(c.controller,c.location,c.sequence,QUERY_REASON);CHECK((u32(query,8)&(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION))==(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION));}
  Run invalid(initial(r,44362883,false),r);invalid.idle();CHECK(invalid.activation(44362883)<0);
  auto invalidQueries=PoolQueryGate::Discover(*invalid.core,invalid.prefix,invalid.context());CHECK(find(invalidQueries.observations,PoolQueryApi::CheckFusion).result==0);describe("INVALID NO ALBAZ",invalidQueries.observations);
  auto darkState=initial(r,44362883);darkState.cards[1].code=46986414;
  Run dark(darkState,r);dark.idle();CHECK(dark.activation(44362883)<0);auto darkQueries=PoolQueryGate::Discover(*dark.core,dark.prefix,dark.context());CHECK(find(darkQueries.observations,PoolQueryApi::FusionProcedure).result==0);
  // Both targets build their own original material closure. Adding a sibling
  // changes neither the first target's scope nor its per-scope invocation.
  auto twinState=initial(r,44362883);twinState.cards.push_back({87746184,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  Run twins(twinState,r);twins.idle();twins.activate(44362883);
  auto twinsTrace=PoolQueryGate::Discover(*twins.core,twins.prefix,twins.context()).observations;
  auto twinsAgain=PoolQueryGate::Discover(*twins.core,twins.prefix,twins.context()).observations;equivalent(twinsTrace,twinsAgain);
  std::vector<PoolQueryRequest> materialScopes;for(auto& o:twinsTrace)if(o.request.api==PoolQueryApi::FusionMaterials){CHECK((codes(o.members)==std::vector<uint32_t>{68468459,89631139}));materialScopes.push_back(o.request);}
  CHECK(materialScopes.size()==2);CHECK(materialScopes[0].target!=materialScopes[1].target);CHECK(materialScopes[0].scope!=materialScopes[1].scope);
  for(auto& q:materialScopes){CHECK(q.invocation==1);if(q.target==check.request.target)CHECK(q.scope==find(atTarget,PoolQueryApi::FusionMaterials).request.scope);}
  // King's original substitute is legal for Albion under Polymerization but
  // rejected by Branded Fusion's original additional whole-material closure.
  auto substituteState=initial(r,44362883);substituteState.cards[1].location=LOCATION_HAND;substituteState.cards[1].position=POS_FACEUP_ATTACK;
  substituteState.cards[3].code=79109599;substituteState.cards[3].location=LOCATION_HAND;substituteState.cards[3].position=POS_FACEUP_ATTACK;
  Run substitute(substituteState,r);substitute.idle();CHECK(substitute.activation(44362883)<0);
  auto substituteQueries=PoolQueryGate::Discover(*substitute.core,substitute.prefix,substitute.context());auto& rejectedSub=find(substituteQueries.observations,PoolQueryApi::FusionProcedure);CHECK(rejectedSub.result==0);CHECK(rejectedSub.additionalCheckPresent && rejectedSub.additionalGoalPresent);CHECK((codes(rejectedSub.input)==std::vector<uint32_t>{79109599,89631139}));
  auto polymerState=substituteState;polymerState.cards[0].code=24094653;Run polymer(polymerState,r);polymer.idle();CHECK(polymer.activation(24094653)>=0);polymer.activate(24094653);CHECK(polymer.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(polymer.now.checkpoint.prompt,6)==87746184);
  std::cout<<"two-target scoped membership and original Polymerization-positive / Branded-negative substitute closure passed\n";
  Run gold(initial(r,75500286),r);gold.idle();auto goldPre=PoolQueryGate::Discover(*gold.core,gold.prefix,gold.context());CHECK(find(goldPre.observations,PoolQueryApi::ExistingMatching).result==1);
  gold.activate(75500286);CHECK(gold.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(gold.now.checkpoint.prompt,6)==89631139);
  Run ga(PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context()),gold.prefix),gb(PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context()),gold.prefix);
  auto goldQueries=PoolQueryGate::Observations(*ga.core);equivalent(goldQueries,PoolQueryGate::Observations(*gb.core));describe("GOLD SELECTION",goldQueries);
  auto selected=find(goldQueries,PoolQueryApi::SelectMatching);CHECK(selected.request.stage==PoolQueryStage::ResolutionSelection);CHECK(selected.request.source==CardSource::OwnMainDeck);CHECK(selected.members.size()==1);auto goldId=selected.members[0].instance;
  ga.submit({1,0});gb.submit({1,0});ga.idle();gb.idle();CHECK(ga.core->DiagnosticState()==gb.core->DiagnosticState());auto removed=actual(*ga.core,goldId);CHECK(removed.code==89631139);CHECK(removed.location==LOCATION_REMOVED);CHECK(removed.position&POS_FACEUP);
  gold.submit({1,0});gold.idle();CHECK(gold.core->DiagnosticState()==ga.core->DiagnosticState());CHECK(PoolQueryGate::Observations(*gold.core).empty());
  CHECK(ResourceView::Capture(argv[1])->Fingerprint()==r->Fingerprint());std::cout<<"independent native fusion/Gold prefixes, legal whole materials, invalid whole materials and same-instance removal passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
