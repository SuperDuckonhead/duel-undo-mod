#pragma once
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
