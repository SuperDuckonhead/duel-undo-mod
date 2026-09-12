#include "core_fixture.h"
#include "undo/core_driver.h"
#include "undo/deck_test_query.h"
#include "common.h"
#include "effect.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include "mtrandom.h"
using namespace undo;
static Bytes integer(uint32_t n) { return {uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)}; }
static void waiting(const Boundary& b) { if(b.kind!=BoundaryKind::AwaitResponse) throw std::runtime_error(b.failure); CHECK(!b.rejectedResponse); }
static Bytes response(const Checkpoint& c) {
 const auto& p=c.prompt;
 if(p[0]==MSG_SELECT_CHAIN)return integer(0xffffffff);
 if(p[0]==MSG_SELECT_PLACE)return {0,LOCATION_MZONE,0};
 if(p[0]==MSG_SELECT_POSITION)return integer(POS_FACEUP_ATTACK);
 throw std::runtime_error("Unexpected prompt "+std::to_string(p[0]));
}
struct Run {
 std::unique_ptr<CoreDriver> core; std::vector<ResponseRecord> prefix; Boundary now; std::vector<Bytes> output;
 explicit Run(const InitialState& initial,std::shared_ptr<const ResourceView> resources):core(CoreDriver::Create(initial,resources)) {now=core->Advance([&](const Bytes& b){output.push_back(b);});waiting(now);}
 void submit(Bytes bytes) { auto before=now.checkpoint; core->Submit(bytes); now=core->Advance([&](const Bytes& b){output.push_back(b);}); waiting(now); prefix.push_back({before.player,Origin::Manual,bytes,before}); }
 void summon() {
  for(unsigned n=0;now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n) {CHECK(n<20);submit(response(now.checkpoint));}
  CHECK(now.checkpoint.prompt[2]==1); // The actual Aluber is the sole normal summon.
  submit(integer(0));
  // Placement, then each player's summon-response window. Stop immediately at
  // the resulting optional-trigger boundary; declining later windows would
  // already accept an input after the missing-target query.
  submit(response(now.checkpoint));
  submit(response(now.checkpoint));
  submit(response(now.checkpoint));
 }
};
static InitialState initial(std::shared_ptr<const ResourceView> resources,bool physical=false) {
 InitialState s;s.seed.resize(SEED_COUNT,42);s.duelOptions=(5u<<16)|DUEL_PSEUDO_SHUFFLE;s.resourceDigest=resources->Fingerprint();
 for(uint8_t p=0;p<2;++p)for(unsigned n=0;n<10;++n)s.cards.push_back({89631139,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 s.cards.push_back({62962630,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK});
 if(physical)s.cards.push_back({44362883,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 return s;
}
static std::string hex(const Digest& value) {std::ostringstream out;for(auto b:value)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();}
static std::shared_ptr<const ResourceView> faultResources(const ResourceView& original) {
 const std::string root=UNDO_POOL_FIXTURE;fixture::database(root);
 for(auto name:{"constant.lua","utility.lua","procedure.lua","c62962630.lua","c44362883.lua"})
  fixture::WriteFixtureFile(root+"/script/"+name,original.Read(std::string("script/")+name));
 for(uint32_t code:{89631139u,62962630u,44362883u}) {
  const auto& c=original.Card(code);uint64_t sets=0;for(unsigned i=0;i<4;++i)sets|=uint64_t(c.setcode[i])<<(16*i);
  fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES("+std::to_string(code)+",0,"+std::to_string(c.alias)+","+std::to_string(sets)+","+std::to_string(c.type)+","+std::to_string(c.attack)+","+std::to_string(c.defense)+","+std::to_string(c.level)+","+std::to_string(c.race)+","+std::to_string(c.attribute)+",0); INSERT INTO texts(id,name) VALUES("+std::to_string(code)+",'original');");
 }
 for(uint32_t code:{900000010u,900000011u,900000012u,900000013u})
  fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES("+std::to_string(code)+",0,0,349,2,0,0,0,0,0,0); INSERT INTO texts(id,name) VALUES("+std::to_string(code)+",'candidate');");
 fixture::WriteFixtureFile(root+"/script/c900000010.lua",fixture::bytes(R"lua(
function c900000010.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_SINGLE)
 e:SetCode(EFFECT_CANNOT_TO_HAND)
 c:RegisterEffect(e)
end
)lua"));
 fixture::WriteFixtureFile(root+"/script/c900000011.lua",fixture::bytes(R"lua(
function c900000011.initial_effect(c)
 math.random()
 Debug.Message('worker-private-initialization')
 error('deliberate candidate initialization failure')
end
)lua"));
 fixture::WriteFixtureFile(root+"/script/c900000012.lua",fixture::bytes(R"lua(
function c900000012.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_FIELD)
 e:SetProperty(EFFECT_FLAG_PLAYER_TARGET)
 e:SetCode(EFFECT_CANNOT_ACTIVATE)
 e:SetTargetRange(1,0)
 e:SetValue(1)
 Duel.RegisterEffect(e,0)
end
)lua"));
 fixture::WriteFixtureFile(root+"/script/c900000013.lua",fixture::bytes(R"lua(
function c900000013.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_FIELD)
 e:SetRange(LOCATION_DECK)
 e:SetCode(EFFECT_CANNOT_TO_HAND)
 e:SetTargetRange(LOCATION_DECK,0)
 e:SetTarget(function(e,tc) return tc==e:GetHandler() end)
 c:RegisterEffect(e)
end
)lua"));
 return ResourceView::Capture(root);
}
template<class Action> static void rejects(Action action) {bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
static uint32_t u32(const Bytes& b,size_t n) {CHECK(n+4<=b.size());return uint32_t(b[n])|(uint32_t(b[n+1])<<8)|(uint32_t(b[n+2])<<16)|(uint32_t(b[n+3])<<24);}
static std::vector<uint32_t> codes(const CoreDriver& core,uint8_t location) {
 auto b=core.QueryField(0,location,QUERY_CODE);std::vector<uint32_t> out;
 for(size_t n=0;n<b.size();) {auto size=u32(b,n);CHECK(size>=4 && n+size<=b.size());if(size>8)out.push_back(u32(b,n+8));n+=size;}return out;
}
int main(int argc,char** argv) {
 try {
  CHECK(argc==2);auto resources=ResourceView::Capture(argv[1]);
  const auto resourceHash=resources->Fingerprint();
  std::cout<<"resource="<<hex(resourceHash)<<" script="<<hex(Sha256(resources->Read("script/c62962630.lua")))<<'\n';
  for(auto name:{"constant.lua","utility.lua","procedure.lua","c44362883.lua"})std::cout<<name<<"="<<hex(Sha256(resources->Read(std::string("script/")+name)))<<'\n';
  Run physical(initial(resources,true),resources);physical.summon();CHECK(physical.now.checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  DeckTestContext physicalContext{{3},{1},physical.prefix.size(),physical.now.checkpoint,resources->Fingerprint(),1};
  CHECK(PoolQueryGate::Discover(*physical.core,physical.prefix,physicalContext).requests.empty());
  Run editing(initial(resources),resources);editing.summon();
  CHECK(editing.now.checkpoint.prompt[0]==MSG_SELECT_CHAIN);CHECK(editing.now.checkpoint.prompt[2]==0);
  CHECK(PoolQueryGate::PendingQueries(*editing.core).empty());
  const auto before=editing.core->DiagnosticState();const auto outputBefore=editing.output;const auto transcript=editing.core->Transcript();const auto logs=editing.core->Logs();
  CHECK(u32(before,28)==22);CHECK(codes(*editing.core,LOCATION_DECK).size()==10);
  // Missing pool evidence/prompt installation must not pass merely because a
  // physical target was inserted into the starting deck.
  DeckTestContext context{{1},{1},editing.prefix.size(),editing.now.checkpoint,resources->Fingerprint(),1};
  auto discovery=PoolQueryGate::Discover(*editing.core,editing.prefix,context);
  CHECK(!discovery.requests.empty());
  CHECK(discovery.diagnostic==before);CHECK(discovery.requests[0].stage==PoolQueryStage::TargetCheck);
  CHECK(discovery.requests[0].effectRegistration==1);CHECK(discovery.requests[0].effectCode==EVENT_SUMMON_SUCCESS);
  auto pending=PoolQueryGate::SearchAsync(editing.core->Initial(),resources,editing.prefix,discovery.requests[0],{44362883,89631139,83764718});
  CHECK(editing.core->DiagnosticState()==before);
  auto evidence=pending.get();
  for(const auto& error:evidence.Errors())std::cerr<<error<<'\n';
  CHECK(evidence.Candidates()==std::vector<uint32_t>{44362883});
  CHECK(evidence.Errors().empty());
  CHECK(editing.core->DiagnosticState()==before);CHECK(editing.core->Transcript()==transcript);CHECK(editing.core->Logs()==logs);CHECK(editing.output==outputBefore);
  for(unsigned variant=0;variant<7;++variant) {
   auto stale=context;auto stalePrefix=editing.prefix;auto staleDiscovery=discovery;
   if(variant==0)++stale.modeGeneration;
   if(variant==1)stale.resourceVersion[0]^=1;
   if(variant==2)stale.checkpoint.prompt.push_back(255);
   if(variant==3)stalePrefix.back().response[0]^=1;
   if(variant==4)++stale.branch.value;
   if(variant==5)staleDiscovery.requests[0].callsite++;
   if(variant==6)staleDiscovery.requests[0].handler.value++;
   auto* main=editing.core.get();rejects([&]{PoolQueryGate::Install(editing.core,stalePrefix,stale,staleDiscovery,evidence);});
   CHECK(editing.core.get()==main);CHECK(editing.core->DiagnosticState()==before);CHECK(editing.output==outputBefore);
  }
  PoolQueryGate::Install(editing.core,editing.prefix,context,discovery,evidence);
  editing.now=editing.core->Current();
  CHECK(editing.now.checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  CHECK(u32(editing.core->DiagnosticState(),28)==22);CHECK(codes(*editing.core,LOCATION_DECK).size()==10);CHECK(editing.output==outputBefore);
  auto offContext=context;offContext.checkpoint=editing.now.checkpoint;++offContext.modeGeneration;
  PoolQueryGate::RecomputeActual(editing.core,editing.prefix,offContext);
  CHECK(editing.core->Current().checkpoint.prompt==context.checkpoint.prompt);CHECK(editing.core->DiagnosticState()==before);
  CHECK(PoolQueryGate::PendingQueries(*editing.core).empty());
  auto* offMain=editing.core.get();auto onAgain=context;onAgain.modeGeneration=3;
  rejects([&]{PoolQueryGate::Install(editing.core,editing.prefix,onAgain,discovery,evidence);});CHECK(editing.core.get()==offMain);
  auto again=PoolQueryGate::Discover(*editing.core,editing.prefix,onAgain);
  auto refreshed=PoolQueryGate::SearchAsync(editing.core->Initial(),resources,editing.prefix,again.requests.at(0),{44362883}).get();
  PoolQueryGate::Install(editing.core,editing.prefix,onAgain,again,refreshed);
  editing.core->Submit(integer(0xffffffff));auto retry=editing.core->Advance();CHECK(retry.rejectedResponse);
  CHECK(PoolQueryGate::PendingQueries(*editing.core).at(0).acceptedPrefix==again.acceptedPrefix);
  editing.core->Submit(integer(1));editing.now=editing.core->Advance();
  for(unsigned n=0;editing.now.kind==BoundaryKind::AwaitResponse;++n) {
   CHECK(n<10);editing.core->Submit(response(editing.now.checkpoint));editing.now=editing.core->Advance();
  }
  if(editing.now.kind==BoundaryKind::Failed)throw std::runtime_error(editing.now.failure);
  CHECK(editing.now.kind==BoundaryKind::AwaitPoolQuery);
  auto queries=PoolQueryGate::PendingQueries(*editing.core);
  std::cout<<"precheck prefix="<<again.context.historyCursor<<" call="<<again.requests.at(0).callsite
   <<" handler="<<again.requests.at(0).handler.value<<" effect="<<again.requests.at(0).effectRegistration
   <<" resolution prefix="<<queries.back().context.historyCursor<<" call="<<queries.back().callsite<<'\n';
  CHECK(queries.back().stage==PoolQueryStage::ResolutionSelection);
  CHECK(queries.back().role==SelectionRole::ResolutionTarget);
  CHECK(queries.back().source==CardSource::OwnMainDeck);
  CHECK(queries.back().handler==discovery.requests[0].handler);
  CHECK(queries.back().context.historyCursor>onAgain.historyCursor);
  CHECK(queries.back().acceptedPrefix!=again.acceptedPrefix);
  CHECK(editing.now.checkpoint.prompt.empty());
  CHECK(u32(editing.core->DiagnosticState(),28)==22);CHECK(codes(*editing.core,LOCATION_DECK).size()==10);CHECK(codes(*editing.core,LOCATION_HAND).empty());
  std::cout<<"native MSG_SELECT_EFFECTYN accepted; original SelectMatchingCard reached privately\n";
  auto faults=faultResources(*resources);Run restricted(initial(faults),faults);restricted.summon();
  DeckTestContext faultContext{{2},{1},restricted.prefix.size(),restricted.now.checkpoint,faults->Fingerprint(),1};
  auto faultDiscovery=PoolQueryGate::Discover(*restricted.core,restricted.prefix,faultContext);
  const auto faultState=restricted.core->DiagnosticState();const auto faultOutput=restricted.output;
  auto physicalRestricted=initial(faults);physicalRestricted.cards.push_back({900000013,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  Run deckRestriction(physicalRestricted,faults);deckRestriction.summon();CHECK(deckRestriction.now.checkpoint.prompt[0]==MSG_SELECT_CHAIN);CHECK(deckRestriction.now.checkpoint.prompt[2]==0);
  auto faultEvidence=PoolQueryGate::SearchAsync(restricted.core->Initial(),faults,restricted.prefix,faultDiscovery.requests.at(0),{900000010,900000011,900000012,900000013,44362883}).get();
  for(auto c:faultEvidence.Candidates())std::cout<<"accepted candidate="<<c<<'\n';
  CHECK(faultEvidence.Candidates()==std::vector<uint32_t>{44362883});
  CHECK(faultEvidence.Errors().size()==1);
  CHECK(faultEvidence.Errors()[0].find("deliberate candidate initialization failure")!=std::string::npos);
  CHECK(restricted.core->DiagnosticState()==faultState);CHECK(restricted.output==faultOutput);CHECK(restricted.core->Logs().empty());
  auto failed=PoolQueryGate::SearchAsync(restricted.core->Initial(),faults,restricted.prefix,faultDiscovery.requests[0],{900000011}).get();
  auto* unchanged=restricted.core.get();rejects([&]{PoolQueryGate::Install(restricted.core,restricted.prefix,faultContext,faultDiscovery,failed);});
  CHECK(restricted.core.get()==unchanged);CHECK(restricted.core->DiagnosticState()==faultState);CHECK(restricted.output==faultOutput);
  // Frozen copies retain the resolved original script even after fixture disk
  // bytes change. The real installation is never modified.
  auto changedScript=resources->Read("script/c62962630.lua");auto suffix=fixture::bytes("\n-- changed fixture bytes\n");changedScript.insert(changedScript.end(),suffix.begin(),suffix.end());
  fixture::WriteFixtureFile(std::string(UNDO_POOL_FIXTURE)+"/script/c62962630.lua",changedScript);
  CHECK(Sha256(faults->Read("script/c62962630.lua"))==Sha256(resources->Read("script/c62962630.lua")));
  PoolQueryGate::Install(restricted.core,restricted.prefix,faultContext,faultDiscovery,faultEvidence);
  CHECK(restricted.core->Current().checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  auto changed=ResourceView::Capture(UNDO_POOL_FIXTURE);CHECK(changed->Fingerprint()!=faults->Fingerprint());
  rejects([&]{(void)PoolQueryGate::SearchAsync(initial(changed),changed,restricted.prefix,faultDiscovery.requests[0],{44362883}).get();});
  Run changedRun(initial(changed),changed);changedRun.summon();DeckTestContext changedContext{{4},{1},changedRun.prefix.size(),changedRun.now.checkpoint,changed->Fingerprint(),1};
  rejects([&]{PoolQueryGate::Discover(*changedRun.core,changedRun.prefix,changedContext);});
  // Native physical-target flow continues to hand with no query bridge.
  physical.core->Submit(integer(1));physical.now=physical.core->Advance();
  for(unsigned n=0;physical.now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n) {
   CHECK(n<20);auto p=physical.now.checkpoint.prompt;
   physical.core->Submit(p[0]==MSG_SELECT_CARD?Bytes{1,0}:response(physical.now.checkpoint));physical.now=physical.core->Advance();waiting(physical.now);
  }
  CHECK(codes(*physical.core,LOCATION_HAND)==std::vector<uint32_t>{44362883});CHECK(PoolQueryGate::PendingQueries(*physical.core).empty());
  mtrandom rng(42);const auto rngBefore=rng.diagnostic_state();CHECK(rng.diagnostic_state()==rngBefore);rng.rand();CHECK(rng.diagnostic_state()!=rngBefore);
  fixture::WriteFixtureFile(std::string(UNDO_POOL_FIXTURE)+"/script/c62962630.lua",resources->Read("script/c62962630.lua"));
  fixture::WriteFixtureFile(std::string(UNDO_POOL_FIXTURE)+"/single/lua-rng.lua",fixture::bytes("math.random()"));
  auto rngResources=ResourceView::Capture(UNDO_POOL_FIXTURE);auto without=initial(rngResources);auto with=without;with.scenarioName="single/lua-rng.lua";
  Run luaBefore(without,rngResources),luaAfter(with,rngResources);CHECK(luaBefore.now.checkpoint.canonicalState==luaAfter.now.checkpoint.canonicalState);
  CHECK(luaBefore.core->DiagnosticState()!=luaAfter.core->DiagnosticState());
  CHECK(ResourceView::Capture(argv[1])->Fingerprint()==resourceHash);
  std::cout<<"OFF/ON/OFF; stale context/prefix/handler/callsite; private worker faults; card/order/counters/MT/Lua RNG and live-output isolation passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
