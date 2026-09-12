#include "core_fixture.h"
#include "undo/deck_test_query.h"
#include "common.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace undo;

static Bytes integer(uint32_t n) { return {uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)}; }
static uint32_t u32(const Bytes& b,size_t at) {
 CHECK(at+4<=b.size());
 return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
static std::string hex(const Digest& d) {
 std::ostringstream out;
 for(auto b:d)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
 return out.str();
}
static void waiting(const Boundary& b) {
 if(b.kind!=BoundaryKind::AwaitResponse)throw std::runtime_error(b.failure);
 CHECK(!b.rejectedResponse);
}
static Bytes response(const Checkpoint& c) {
 const auto& p=c.prompt;
 if(p[0]==MSG_SELECT_CHAIN)return integer(-1);
 if(p[0]==MSG_SELECT_PLACE)return {0,LOCATION_MZONE,0};
 if(p[0]==MSG_SELECT_POSITION)return integer(POS_FACEUP_ATTACK);
 throw std::runtime_error("Unexpected prompt "+std::to_string(p[0]));
}
static InitialState initial(std::shared_ptr<const ResourceView> r) {
 InitialState s;
 s.seed.resize(SEED_COUNT,42);
 s.duelOptions=(5u<<16)|DUEL_PSEUDO_SHUFFLE;
 s.resourceDigest=r->Fingerprint();
 for(uint8_t p=0;p<2;++p)for(unsigned n=0;n<10;++n)
  s.cards.push_back({89631139,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 s.cards.push_back({62962630,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK});
 return s;
}
struct Run {
 std::unique_ptr<CoreDriver> core;
 std::vector<ResponseRecord> prefix;
 std::vector<Bytes> output;
 Boundary now;
 explicit Run(std::shared_ptr<const ResourceView> r):core(CoreDriver::Create(initial(r),r)) {
  now=core->Advance();waiting(now);
 }
 void submit(Bytes b) {
  auto before=now.checkpoint;
  core->Submit(b);
  now=core->Advance([&](const Bytes& m){output.push_back(m);});
  if(now.kind==BoundaryKind::Failed)throw std::runtime_error(now.failure);
  CHECK(!now.rejectedResponse);
  prefix.push_back({before.player,Origin::Manual,b,before});
 }
 DeckTestContext context() const {return {{41},{1},prefix.size(),now.checkpoint,core->Resources()->Fingerprint(),1};}
 void resolution() {
  for(unsigned n=0;now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n) {
   CHECK(n<20);submit(response(now.checkpoint));
  }
  CHECK(now.checkpoint.prompt[2]==1);
  submit(integer(0));
  for(unsigned n=0;n<3;++n)submit(response(now.checkpoint));
  CHECK(now.checkpoint.prompt[0]==MSG_SELECT_CHAIN);
  CHECK(now.checkpoint.prompt[2]==0);
  auto discovery=PoolQueryGate::Discover(*core,prefix,context());
  auto evidence=PoolQueryGate::SearchAsync(core->Initial(),core->Resources(),prefix,discovery.requests.at(0),{44362883}).get();
  CHECK(evidence.Candidates()==std::vector<uint32_t>{44362883});
  PoolQueryGate::Install(core,prefix,context(),discovery,evidence);
  now=core->Current();
  CHECK(now.checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  CHECK(u32(core->DiagnosticState(),28)==22);
  submit(integer(1));
  for(unsigned n=0;now.kind==BoundaryKind::AwaitResponse;++n) {
   CHECK(n<10);submit(response(now.checkpoint));
  }
  CHECK(now.kind==BoundaryKind::AwaitPoolQuery);
  CHECK(u32(core->DiagnosticState(),28)==22);
 }
};
struct ActualCard {uint64_t id;uint32_t code,owner,controller,zone,sequence,position;};
static std::vector<ActualCard> actual(const CoreDriver& core) {
 auto b=core.DiagnosticState();
 std::vector<ActualCard> result;
 size_t at=32;
 for(unsigned n=0;n<u32(b,28);++n) {
  result.push_back({uint64_t(u32(b,at))|(uint64_t(u32(b,at+4))<<32),u32(b,at+8),u32(b,at+12),
   u32(b,at+16),u32(b,at+20),u32(b,at+24),u32(b,at+28)});
  at+=56+8*u32(b,at+52);
 }
 return result;
}
static ActualCard find(const CoreDriver& core,StableInstanceId id) {
 auto cards=actual(core);
 auto found=std::find_if(cards.begin(),cards.end(),[&](const auto& c){return c.id==id.value;});
 CHECK(found!=cards.end());return *found;
}
static void equal(const CoreDriver& a,const CoreDriver& b) {
 CHECK(a.Current().kind==b.Current().kind);
 auto x=a.Current().checkpoint,y=b.Current().checkpoint;
 CHECK(x.player==y.player && x.prompt==y.prompt && x.canonicalState==y.canonicalState);
 CHECK(x.transcriptDigest==y.transcriptDigest && x.clock.remainingMs==y.clock.remainingMs && x.aiLogCursor==y.aiLogCursor);
 CHECK(a.DiagnosticState()==b.DiagnosticState());
 CHECK(a.Transcript()==b.Transcript());CHECK(a.Logs()==b.Logs());
 CHECK(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(a))==PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(b)));
 CHECK(PoolQueryGate::Introductions(a).size()==PoolQueryGate::Introductions(b).size());
}
struct Snapshot {
 const CoreDriver* pointer;
 Boundary boundary;
 Bytes diagnostic,transcript;
 std::vector<std::string> logs;
 std::vector<Bytes> output;
 Digest prefix;
 size_t events;
 explicit Snapshot(const Run& run):pointer(run.core.get()),boundary(run.core->Current()),diagnostic(run.core->DiagnosticState()),
  transcript(run.core->Transcript()),logs(run.core->Logs()),output(run.output),
  prefix(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*run.core))),events(PoolQueryGate::Introductions(*run.core).size()){}
 void unchanged(const Run& run) const {
  CHECK(run.core.get()==pointer);
  auto now=run.core->Current();CHECK(now.kind==boundary.kind && now.checkpoint.prompt==boundary.checkpoint.prompt);
  CHECK(now.checkpoint.canonicalState==boundary.checkpoint.canonicalState && now.checkpoint.transcriptDigest==boundary.checkpoint.transcriptDigest);
  CHECK(now.checkpoint.player==boundary.checkpoint.player && now.checkpoint.clock.remainingMs==boundary.checkpoint.clock.remainingMs);
  CHECK(now.checkpoint.aiLogCursor==boundary.checkpoint.aiLogCursor);
  CHECK(run.core->DiagnosticState()==diagnostic && run.core->Transcript()==transcript && run.core->Logs()==logs && run.output==output);
  CHECK(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*run.core))==prefix);
  CHECK(PoolQueryGate::Introductions(*run.core).size()==events);
 }
};
template<class Action> static void rejects(Action action,const std::string& expected="") {
 try {action();}catch(const std::exception& error) {
  std::cout<<"rejected: "<<error.what()<<'\n';
  CHECK(std::string(error.what()).find(expected)!=std::string::npos);return;
 }
 throw std::runtime_error("Expected candidate preparation rejection");
}
static std::shared_ptr<const ResourceView> faultResources(const ResourceView& original) {
 const std::string root=UNDO_INTRODUCTION_FIXTURE;
 fixture::database(root);
 for(auto name:{"constant.lua","utility.lua","procedure.lua","c62962630.lua","c44362883.lua"})
  fixture::WriteFixtureFile(root+"/script/"+name,original.Read(std::string("script/")+name));
 for(uint32_t code:{89631139u,62962630u,44362883u,87746184u}) {
  const auto& c=original.Card(code);
  uint64_t sets=0;for(unsigned i=0;i<4;++i)sets|=uint64_t(c.setcode[i])<<(16*i);
  fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES("+std::to_string(code)+",0,"+std::to_string(c.alias)+","+std::to_string(sets)+","+std::to_string(c.type)+","+std::to_string(c.attack)+","+std::to_string(c.defense)+","+std::to_string(c.level)+","+std::to_string(c.race)+","+std::to_string(c.attribute)+",0); INSERT INTO texts(id,name) VALUES("+std::to_string(code)+",'original');");
 }
 for(uint32_t code:{900000021u,900000022u,900000023u,900000024u,900000025u,900000026u}) {
  auto type=code==900000026?TYPE_MONSTER|TYPE_NORMAL|TYPE_TOKEN:TYPE_SPELL;
  fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES("+std::to_string(code)+",0,0,349,"+std::to_string(type)+",0,0,0,0,0,0); INSERT INTO texts(id,name) VALUES("+std::to_string(code)+",'candidate fault fixture');");
 }
 fixture::WriteFixtureFile(root+"/script/c900000021.lua",fixture::bytes(R"lua(
function c900000021.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_SINGLE)
 e:SetCode(EFFECT_UPDATE_ATTACK)
 e:SetValue(100)
 c:RegisterEffect(e)
 math.random()
 Debug.Message('candidate-only initialization allocation and effect registered')
 error('source-initialization-fault-after-registration')
end
)lua"));
 const std::string restriction=R"lua(
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_FIELD)
 e:SetRange(LOCATION_DECK)
 e:SetCode(EFFECT_CANNOT_TO_HAND)
 e:SetTargetRange(LOCATION_DECK,0)
 e:SetTarget(function(e,tc) return tc==e:GetHandler() end)
 c:RegisterEffect(e)
)lua";
 fixture::WriteFixtureFile(root+"/script/c900000022.lua",fixture::bytes("function c900000022.initial_effect(c)\n"+restriction+"end\n"));
 // A second, legal actual member must not mask the confirmed source's failure.
 fixture::WriteFixtureFile(root+"/script/c900000023.lua",fixture::bytes("function c900000023.initial_effect(c)\n"+restriction+
  "Debug.AddCard(44362883,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE)\n"+
  "assert(Duel.IsExistingMatchingCard(c62962630.thfilter,0,LOCATION_DECK,0,1,nil))\nend\n"));
 fixture::WriteFixtureFile(root+"/script/c900000024.lua",fixture::bytes(R"lua(
function c900000024.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_SINGLE+EFFECT_TYPE_CONTINUOUS)
 e:SetCode(EVENT_TO_HAND)
 e:SetOperation(function(e)
  assert(e:GetHandler():IsLocation(LOCATION_HAND))
  math.random()
  Debug.Message('candidate-only original movement already reached hand')
  error('source-continuation-fault-after-original-move')
 end)
 c:RegisterEffect(e)
end
)lua"));
 fixture::WriteFixtureFile(root+"/script/c900000025.lua",fixture::bytes(R"lua(
function c900000025.initial_effect(c)
 Debug.AddCard(900000026,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK)
 Duel.IsExistingMatchingCard(c62962630.thfilter,0,LOCATION_DECK,0,1,nil)
 math.random()
end
)lua"));
 return ResourceView::Capture(root);
}
static void faultCases(std::shared_ptr<const ResourceView> resources) {
  auto faults=faultResources(*resources);
  Run failed(faults);failed.resolution();
  auto faultQuery=PoolQueryGate::PendingQueries(*failed.core).back();
  SelectionPlan faultPlan{failed.context(),SelectionRole::ResolutionTarget,BindingStage::Resolution,
   {{CardReferenceKind::NewCopy,{1001},44362883,0,CardSource::OwnMainDeck,{}}}};
  const Snapshot isolated(failed);
  const std::pair<uint32_t,const char*> failures[]={
   {900000021,"source-initialization-fault-after-registration"},
   {900000022,"original selection filter"},
   {900000023,"original selection filter"},
   {900000024,"source-continuation-fault-after-original-move"},
   {900000026,"not an own main-deck source"}};
  for(auto& failure:failures) {
   faultPlan.cards[0].code=failure.first;
   rejects([&]{PoolQueryGate::Introduce(failed.core,failed.context(),faultQuery,faultPlan);},failure.second);
   isolated.unchanged(failed);
  }
  faultPlan.cards[0].code=900000025;
  const auto nextNative=u32(failed.core->DiagnosticState(),0);
  PoolQueryGate::Introduce(failed.core,failed.context(),faultQuery,faultPlan);
  const auto allocationRecord=PoolQueryGate::Introductions(*failed.core).at(0);
  CHECK(allocationRecord.nativeInstance.value==nextNative+1);
  CHECK(find(*failed.core,allocationRecord.nativeInstance).code==900000025);
  CHECK(find(*failed.core,allocationRecord.nativeInstance).zone==LOCATION_HAND);
  auto allocationReplay=PoolQueryGate::ReplayIntroduction(failed.core->Initial(),faults,allocationRecord);
  equal(*failed.core,*allocationReplay);
  CHECK(failed.output==isolated.output);
}
int main(int argc,char** argv) {
 try {
  CHECK(argc==2 || argc==3);
  auto resources=ResourceView::Capture(argv[1]);
  std::cout<<"resources="<<hex(resources->Fingerprint())<<'\n';
  const std::pair<const char*,const char*> pinned[]={
   {"c62962630.lua","14432383c12ce67c8b171ff9e96326130714f676f505b3472b4c898969218141"},
   {"c44362883.lua","df65c2875fe485cab0ba106f2f9a8926af85e5616f2c3aa9d20125becbf85469"},
   {"procedure.lua","df887c18619374f825fc14f5a5f05f6a090c910adbb52933bd8462c773973baa"}};
  for(auto& p:pinned) {
   auto hash=hex(Sha256(resources->Read(std::string("script/")+p.first)));
   std::cout<<p.first<<'='<<hash<<'\n';CHECK(hash==p.second);
  }
  if(argc==3) {CHECK(std::string(argv[2])=="--faults-only");faultCases(resources);return 0;}
  Run live(resources);live.resolution();
  // A bare response prefix cannot reproduce the earlier native activation.
  // Removing evidence milestone replay must fail this real no-target chain.
  auto replay=PoolQueryGate::Recreate(*live.core,live.prefix,live.context());
  CHECK(replay->Current().kind==BoundaryKind::AwaitPoolQuery);
  CHECK(replay->DiagnosticState()==live.core->DiagnosticState());
  CHECK(replay->Transcript()==live.core->Transcript());
  std::cout<<"earlier evidence installation and accepted activation replayed without source creation\n";
  auto query=PoolQueryGate::PendingQueries(*live.core).back();
  SelectionPlan plan{live.context(),SelectionRole::ResolutionTarget,BindingStage::Resolution,
   {{CardReferenceKind::NewCopy,{1001},44362883,0,CardSource::OwnMainDeck,{}}}};
  CHECK(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*live.core))==PoolQueryGate::PrefixDigest(live.prefix));
  const Snapshot before(live);
  for(unsigned n=0;n<15;++n) {
   auto stale=plan;auto wrong=query;auto token=live.context();
   if(n==0)++token.modeGeneration;
   if(n==1)++token.branch.value;
   if(n==2)++token.historyCursor;
   if(n==3)token.resourceVersion[0]^=1;
   if(n==4)wrong.acceptedPrefix[0]^=1;
   if(n==5)++wrong.handler.value;
   if(n==6)++wrong.effectRegistration;
   if(n==7)++wrong.callsite;
   if(n==8)wrong.callState[0]^=1;
   if(n==9)wrong.script[0]^=1;
   if(n==10)stale.cards[0].owner=1;
   if(n==11)stale.cards[0].source=CardSource::OwnFacedownExtraDeck;
   if(n==12)stale.bindingStage=BindingStage::Activation;
   if(n==13)stale.cards.push_back(stale.cards[0]);
   if(n==14)stale.cards[0].code=87746184;
   rejects([&]{PoolQueryGate::Introduce(live.core,token,wrong,stale);});
   before.unchanged(live);
  }
  auto invalid=plan;invalid.cards[0].code=89631139;
  rejects([&]{PoolQueryGate::Introduce(live.core,live.context(),query,invalid);},"original selection filter");
  before.unchanged(live);
  const auto originalOutput=live.output;
  PoolQueryGate::Introduce(live.core,live.context(),query,plan);
  waiting(live.core->Current());
  auto hand=live.core->QueryField(0,LOCATION_HAND,QUERY_CODE|QUERY_REASON);
  CHECK(u32(hand,8)==44362883);
  CHECK(u32(hand,12)==REASON_EFFECT);
  CHECK(u32(live.core->DiagnosticState(),28)==23);
  CHECK(live.output==originalOutput);
  auto records=PoolQueryGate::Introductions(*live.core);CHECK(records.size()==1);
  const auto record=records[0];
  CHECK(record.evidenceMilestones.size()==1);
  CHECK(record.evidenceMilestones[0].context.historyCursor==4);
  CHECK(record.query.context.historyCursor>4);
  CHECK(record.plan.cards[0].instance.value==1001 && record.introduced.card.instance.value==1001);
  CHECK(record.nativeInstance.value==23 && record.sourceLocation.zone==LOCATION_DECK);
  CHECK(record.introduced.role==SelectionRole::ResolutionTarget && record.introduced.bindingStage==BindingStage::Resolution);
  CHECK(record.selection.response==Bytes({1,0}) && record.selection.before.prompt[5]==1);
  CHECK(find(*live.core,record.nativeInstance).code==44362883);
  CHECK(find(*live.core,record.nativeInstance).zone==LOCATION_HAND);
  auto wrongBranch=record;
  ++wrongBranch.evidenceMilestones.front().context.branch.value;
  rejects([&]{PoolQueryGate::ReplayIntroduction(live.core->Initial(),resources,wrongBranch);},"milestone");
  auto replayA=PoolQueryGate::ReplayIntroduction(live.core->Initial(),resources,record);
  auto replayB=PoolQueryGate::ReplayIntroduction(live.core->Initial(),resources,record);
  equal(*live.core,*replayA);equal(*replayA,*replayB);
  const Snapshot committed(live);
  rejects([&]{PoolQueryGate::Introduce(live.core,plan.context,query,plan);});
  committed.unchanged(live);
  for(unsigned n=0;n<9;++n) {
   auto damaged=record;
   if(n==0)damaged.evidenceMilestones.clear();
   if(n==1)damaged.evidenceMilestones.push_back(damaged.evidenceMilestones.front());
   if(n==2)++damaged.evidenceMilestones.front().context.historyCursor;
   if(n==3)damaged.evidenceMilestones.front().callState[0]^=1;
   if(n==4)++damaged.nativeInstance.value;
   if(n==5)damaged.selection.response={1,1};
   if(n==6)damaged.selection.response={2,0,0};
   if(n==7)damaged.diagnostic[0]^=1;
   if(n==8)++damaged.introduced.card.instance.value;
   rejects([&]{PoolQueryGate::ReplayIntroduction(live.core->Initial(),resources,damaged);});
   committed.unchanged(live);
  }
  auto next=live.core->Current();
  auto continuation=next.checkpoint.prompt[0]==MSG_SELECT_IDLECMD?integer(7):response(next.checkpoint);
  for(auto* core:{live.core.get(),replayA.get(),replayB.get()}) {
   core->Submit(continuation);waiting(core->Advance());
  }
  equal(*live.core,*replayA);equal(*replayA,*replayB);
  std::cout<<"original DECK -> HAND, plan 1001 -> native 23; two independent replays and next native response agree\n";
  faultCases(resources);
  CHECK(ResourceView::Capture(argv[1])->Fingerprint()==resources->Fingerprint());
  std::cout<<"initialization/field-adjust/filter/continuation faults isolated; initialization allocation maps actual returned identity\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
