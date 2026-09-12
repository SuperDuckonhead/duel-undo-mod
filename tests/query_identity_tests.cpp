#include "core_fixture.h"
#include "undo/deck_test_query.h"
#include "common.h"
#include <iostream>
#include <functional>
using namespace undo;

static Bytes integer(uint32_t n) {return {uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};}
static uint32_t u32(const Bytes& b,size_t at) {
 CHECK(at+4<=b.size());
 return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
static Bytes response(const Checkpoint& c) {
 if(c.prompt[0]==MSG_SELECT_CHAIN)return integer(-1);
 if(c.prompt[0]==MSG_SELECT_PLACE)return {0,LOCATION_MZONE,0};
 if(c.prompt[0]==MSG_SELECT_POSITION)return integer(POS_FACEUP_ATTACK);
 throw std::runtime_error("Unexpected native prompt "+std::to_string(c.prompt[0]));
}
static std::shared_ptr<const ResourceView> resources(const ResourceView& original) {
 const std::string root=UNDO_IDENTITY_FIXTURE;
 fixture::database(root);
 for(auto name:{"constant.lua","utility.lua","procedure.lua","c62962630.lua"})
  fixture::WriteFixtureFile(root+"/script/"+name,original.Read(std::string("script/")+name));
 const auto& c=original.Card(89631139);
 fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES(89631139,0,0,0,"+std::to_string(c.type)+",3000,2500,8,8192,16,0); INSERT INTO texts(id,name) VALUES(89631139,'original');");
 for(uint32_t code:{900000120u,900000121u,900000122u})
  fixture::sql(root+"/cards.cdb","INSERT INTO datas VALUES("+std::to_string(code)+",0,0,0,33,1800,1200,4,1,1,0); INSERT INTO texts(id,name) VALUES("+std::to_string(code)+",'identity fixture');");
 std::ifstream script(UNDO_IDENTITY_SCRIPT,std::ios::binary);
 CHECK(script.good());
 fixture::WriteFixtureFile(root+"/script/c900000120.lua",Bytes(std::istreambuf_iterator<char>(script),{}));
 for(auto code:{900000121,900000122})fixture::WriteFixtureFile(root+"/script/c"+std::to_string(code)+".lua",fixture::bytes("function c"+std::to_string(code)+".initial_effect(c) end\n"));
 return ResourceView::Capture(root);
}
static InitialState initial(std::shared_ptr<const ResourceView> r,const std::string& mode="") {
 InitialState s;s.seed.resize(SEED_COUNT,42);s.duelOptions=(5u<<16)|DUEL_PSEUDO_SHUFFLE;s.resourceDigest=r->Fingerprint();
 for(uint8_t p=0;p<2;++p)for(unsigned n=0;n<10;++n)s.cards.push_back({89631139,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 s.cards.push_back({900000120,0,0,LOCATION_HAND,0,POS_FACEUP_ATTACK});s.scenarioParameters=fixture::bytes(mode);return s;
}
struct Run {
 std::unique_ptr<CoreDriver> core;std::vector<ResponseRecord> prefix;std::vector<Bytes> output;Boundary now;
 Run(const InitialState& s,std::shared_ptr<const ResourceView> r):core(CoreDriver::Create(s,r)) {now=core->Advance();}
 Run(std::shared_ptr<const ResourceView> r,const std::string& mode=""):Run(initial(r,mode),r) {}
 void submit(Bytes b) {
  auto before=now.checkpoint;core->Submit(b);now=core->Advance([&](const Bytes& m){output.push_back(m);});
  if(now.kind==BoundaryKind::Failed)throw std::runtime_error(now.failure);
  CHECK(!now.rejectedResponse);prefix.push_back({before.player,Origin::Manual,b,before});
 }
 void summon() {
  for(unsigned n=0;now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n){CHECK(n<20);submit(response(now.checkpoint));}
  CHECK(now.checkpoint.prompt[2]==1);submit(integer(0));
  for(unsigned n=0;n<3;++n)submit(response(now.checkpoint));
 }
 DeckTestContext context() const {return {{51},{1},prefix.size(),now.checkpoint,core->Resources()->Fingerprint(),1};}
};
struct Snapshot {
 const CoreDriver* pointer;Boundary boundary;Bytes diagnostic,transcript;
 std::vector<std::string> logs;std::vector<Bytes> output;Digest history,accepted;size_t introductions;
 explicit Snapshot(const Run& r):pointer(r.core.get()),boundary(r.core->Current()),diagnostic(r.core->DiagnosticState()),
  transcript(r.core->Transcript()),logs(r.core->Logs()),output(r.output),history(PoolQueryGate::PrefixDigest(r.prefix)),
  accepted(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*r.core))),introductions(PoolQueryGate::Introductions(*r.core).size()){}
 void unchanged(const Run& r) const {
  CHECK(r.core.get()==pointer);auto b=r.core->Current();
  CHECK(b.kind==boundary.kind && b.checkpoint.prompt==boundary.checkpoint.prompt);
  CHECK(b.checkpoint.player==boundary.checkpoint.player && b.checkpoint.canonicalState==boundary.checkpoint.canonicalState);
  CHECK(b.checkpoint.transcriptDigest==boundary.checkpoint.transcriptDigest && b.checkpoint.clock.remainingMs==boundary.checkpoint.clock.remainingMs);
  CHECK(b.checkpoint.aiLogCursor==boundary.checkpoint.aiLogCursor);
  CHECK(r.core->DiagnosticState()==diagnostic && r.core->Transcript()==transcript && r.core->Logs()==logs && r.output==output);
  CHECK(PoolQueryGate::PrefixDigest(r.prefix)==history);
  CHECK(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*r.core))==accepted);
  CHECK(PoolQueryGate::Introductions(*r.core).size()==introductions);
 }
};
template<class Action> static void rejects(Action action,const std::string& field) {
 bool rejected=false;
 try{action();}catch(const std::exception& e){
  std::cout<<"rejected: "<<e.what()<<'\n';CHECK(std::string(e.what()).find(field)!=std::string::npos);rejected=true;
 }
 CHECK(rejected);
}
static void continueNative(Run& run,const PoolQueryDiscovery& discovery,const PoolQueryEvidence& evidence) {
 auto output=run.output;
 const auto count=u32(run.core->DiagnosticState(),28);
 PoolQueryGate::Install(run.core,run.prefix,run.context(),discovery,evidence);run.now=run.core->Current();
 CHECK(run.now.checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);CHECK(run.output==output);
 CHECK(u32(run.now.checkpoint.prompt,2)==900000120);
 CHECK(u32(run.core->DiagnosticState(),28)==count);
 run.submit(integer(1));
 for(unsigned n=0;run.now.kind==BoundaryKind::AwaitResponse;++n){CHECK(n<10);run.submit(response(run.now.checkpoint));}
 CHECK(run.now.kind==BoundaryKind::AwaitPoolQuery);
 auto recreated=PoolQueryGate::Recreate(*run.core,run.prefix,run.context());
 CHECK(recreated->DiagnosticState()==run.core->DiagnosticState());
 auto query=PoolQueryGate::PendingQueries(*run.core).back();
 CHECK(query.stage==PoolQueryStage::ResolutionSelection);
 SelectionPlan plan{run.context(),SelectionRole::ResolutionTarget,BindingStage::Resolution,
  {{CardReferenceKind::NewCopy,{1001},900000121,0,CardSource::OwnMainDeck,{}}}};
 PoolQueryGate::Introduce(run.core,run.context(),query,plan);run.now=run.core->Current();
 const auto record=PoolQueryGate::Introductions(*run.core).at(0);
 CHECK(record.evidenceMilestones.size()==1);
 CHECK(record.evidenceMilestones[0].parameters==evidence.Request().parameters);
 auto replay=PoolQueryGate::ReplayIntroduction(run.core->Initial(),run.core->Resources(),record);
 CHECK(replay->DiagnosticState()==run.core->DiagnosticState());
 auto hand=run.core->QueryField(0,LOCATION_HAND,QUERY_CODE);
 CHECK(hand.size()==12 && u32(hand,8)==900000121);
 CHECK(u32(run.core->DiagnosticState(),28)==count+1);
 std::cout<<"correct evidence survived refusal, native activation and original source move\n";
}
int main(int argc,char** argv) {
 try {
  CHECK(argc==2);auto original=ResourceView::Capture(argv[1]);auto r=resources(*original);
  Run run(r);run.summon();CHECK(run.now.checkpoint.prompt[0]==MSG_SELECT_CHAIN);CHECK(run.now.checkpoint.prompt[2]==0);
  auto discovery=PoolQueryGate::Discover(*run.core,run.prefix,run.context());
  // Missing registration must fail at the actual native two-filter boundary.
  CHECK(discovery.requests.size()==2);
  CHECK(discovery.observations.size()==2);
  auto repeat=PoolQueryGate::Discover(*run.core,run.prefix,run.context());
  const auto& qa=discovery.requests[0];const auto& qb=discovery.requests[1];
  CHECK(qa.handler==qb.handler && qa.handlerCode==900000120 && qb.handlerCode==900000120);
  CHECK(qa.effectRegistration==1 && qb.effectRegistration==1);
  CHECK(qa.stage==PoolQueryStage::TargetCheck && qb.stage==qa.stage);
  CHECK(qa.callsite==qb.callsite && qa.caller==qb.caller && qa.scope==qb.scope);
  CHECK(qa.parentScope!=Digest{} && qa.scope!=Digest{});
  CHECK(qa.invocation==1 && qb.invocation==2);
  CHECK(qa.predicate!=qb.predicate && qa.parameters!=qb.parameters);
  CHECK(qa.selfLocation==LOCATION_DECK && qb.selfLocation==LOCATION_DECK && !qa.opponentLocation && !qb.opponentLocation);
  for(size_t i=0;i<2;++i){
   const auto& observation=discovery.observations[i];const auto& again=repeat.requests[i];
   CHECK(observation.completed && observation.result==0);
   CHECK(observation.request.callState==discovery.requests[i].callState);
   CHECK(observation.afterCallState==discovery.requests[i].afterCallState);
   CHECK(again.parameters==discovery.requests[i].parameters && again.invocation==i+1 && again.scope==qa.scope);
   CHECK(again.callState==discovery.requests[i].callState && again.afterCallState==discovery.requests[i].afterCallState);
  }
  auto a=PoolQueryGate::SearchAsync(run.core->Initial(),r,run.prefix,discovery.requests[0],{900000121,900000122,89631139}).get();
  auto b=PoolQueryGate::SearchAsync(run.core->Initial(),r,run.prefix,discovery.requests[1],{900000121,900000122,89631139}).get();
  CHECK(a.Errors().empty() && b.Errors().empty());
  CHECK(a.Candidates()==std::vector<uint32_t>{900000121});CHECK(b.Candidates()==std::vector<uint32_t>{900000122});
  std::cout<<"native two-filter exact witnesses accepted\n";
  const Snapshot before(run);
  Run reverse(r,"reverse");reverse.summon();
  CHECK(PoolQueryGate::PrefixDigest(reverse.prefix)==PoolQueryGate::PrefixDigest(run.prefix));
  CHECK(reverse.core->DiagnosticState()==run.core->DiagnosticState());
  CHECK(reverse.now.checkpoint.prompt==run.now.checkpoint.prompt);
  CHECK(reverse.now.checkpoint.canonicalState==run.now.checkpoint.canonicalState);
  CHECK(reverse.now.checkpoint.transcriptDigest==run.now.checkpoint.transcriptDigest);
  auto wrongSearch=PoolQueryGate::SearchAsync(reverse.core->Initial(),r,reverse.prefix,discovery.requests[0],{900000121}).get();
  CHECK(wrongSearch.Candidates().empty());
  CHECK(wrongSearch.Errors().size()==1);
  CHECK(wrongSearch.Errors()[0].find("parameters")!=std::string::npos);
  const Snapshot reverseBefore(reverse);
  rejects([&]{PoolQueryGate::Install(reverse.core,reverse.prefix,reverse.context(),discovery,a);},"Replay query signature: parameters");
  reverseBefore.unchanged(reverse);before.unchanged(run);
  // Narrow discovery to the current B boundary: installing A must compare A
  // against B, rather than accepting A merely because a full discovery has A.
  auto onlyB=discovery;onlyB.requests={discovery.requests[1]};
  rejects([&]{PoolQueryGate::Install(run.core,run.prefix,run.context(),onlyB,a);},"parameters");before.unchanged(run);
  auto onlyA=discovery;onlyA.requests={discovery.requests[0]};
  rejects([&]{PoolQueryGate::Install(run.core,run.prefix,run.context(),onlyA,b);},"parameters");before.unchanged(run);
  using Change=std::pair<const char*,std::function<void(PoolQueryRequest&)>>;
  const std::vector<Change> changes={
   {"handler",[](auto& q){++q.handler.value;}},{"handlerCode",[](auto& q){++q.handlerCode;}},
   {"effectRegistration",[](auto& q){++q.effectRegistration;}},{"effectCode",[](auto& q){++q.effectCode;}},
   {"stage",[](auto& q){q.stage=PoolQueryStage::ResolutionSelection;}},{"api",[](auto& q){q.api=PoolQueryApi::MatchingGroup;}},
   {"role",[](auto& q){q.role=SelectionRole::ActivationCost;}},{"source",[](auto& q){q.source=CardSource::ExistingState;}},
   {"callsite",[](auto& q){++q.callsite;}},{"invocation",[](auto& q){++q.invocation;}},
   {"selfLocation",[](auto& q){q.selfLocation=LOCATION_HAND;}},{"opponentLocation",[](auto& q){q.opponentLocation=LOCATION_DECK;}},
   {"minimum",[](auto& q){++q.minimum;}},{"maximum",[](auto& q){++q.maximum;}},
   {"parameters",[](auto& q){q.parameters[0]^=1;}},{"callState",[](auto& q){q.callState[0]^=1;}},
   {"afterCallState",[](auto& q){q.afterCallState[0]^=1;}},
   {"caller",[](auto& q){q.caller[0]^=1;}},{"predicate",[](auto& q){q.predicate[0]^=1;}},
   {"script",[](auto& q){q.script[0]^=1;}},{"helper",[](auto& q){q.helper[0]^=1;}},
   {"scope",[](auto& q){q.scope[0]^=1;}},{"parentScope",[](auto& q){q.parentScope[0]^=1;}},
   {"target",[](auto& q){++q.target.value;}},{"procedureRegistration",[](auto& q){++q.procedureRegistration;}},
   {"semantic",[](auto& q){q.semantic=PoolQuerySemantic::FusionTarget;}},{"player",[](auto& q){q.player=1;}},
   {"acceptedPrefix",[](auto& q){q.acceptedPrefix[0]^=1;}}
  };
  for(const auto& change:changes){
   auto stale=onlyA;change.second(stale.requests[0]);
   rejects([&]{PoolQueryGate::Install(run.core,run.prefix,run.context(),stale,a);},change.first);before.unchanged(run);
  }
  auto reverseDiscovery=PoolQueryGate::Discover(*reverse.core,reverse.prefix,reverse.context());
  auto fresh=PoolQueryGate::SearchAsync(reverse.core->Initial(),r,reverse.prefix,reverseDiscovery.requests[0],{900000121,900000122}).get();
  CHECK(fresh.Candidates()==std::vector<uint32_t>{900000122});CHECK(fresh.Errors().empty());
  continueNative(reverse,reverseDiscovery,fresh);
  continueNative(run,onlyA,a);
  Run second(r);second.summon();continueNative(second,onlyB,b);
  Run random(r,"rng");random.summon();const Snapshot randomBefore(random);
  auto rd=PoolQueryGate::Discover(*random.core,random.prefix,random.context());
  CHECK(rd.requests.size()==2);
  CHECK(rd.requests[0].callState!=rd.requests[0].afterCallState);
  CHECK(rd.requests[1].callState==rd.requests[0].afterCallState);
  auto rr=PoolQueryGate::Discover(*random.core,random.prefix,random.context());
  CHECK(rr.requests[0].callState==rd.requests[0].callState && rr.requests[1].afterCallState==rd.requests[1].afterCallState);
  auto re=PoolQueryGate::SearchAsync(random.core->Initial(),r,random.prefix,rd.requests[0],{900000121}).get();
  CHECK(re.Errors().empty() && re.Candidates()==std::vector<uint32_t>{900000121});randomBefore.unchanged(random);
  PoolQueryGate::Install(random.core,random.prefix,random.context(),rd,re);
  CHECK(random.core->Current().checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  std::cout<<"before/after native predicate RNG digests replay and accept distinctly\n";
  // A later prefix must retain the already accepted source and milestone.
  run.prefix=PoolQueryGate::AcceptedResponses(*run.core);
  CHECK(run.now.kind==BoundaryKind::AwaitResponse);
  auto endpoint=PoolQueryGate::Recreate(*run.core,run.prefix,run.context());
  CHECK(endpoint->DiagnosticState()==run.core->DiagnosticState());
  CHECK(PoolQueryGate::Introductions(*endpoint).size()==1);
  run.submit(response(run.now.checkpoint));
  const Snapshot introducedBefore(run);
  auto later=PoolQueryGate::Recreate(*run.core,run.prefix,run.context());
  CHECK(later->DiagnosticState()==run.core->DiagnosticState());
  CHECK(PoolQueryGate::Introductions(*later).size()==1);
  CHECK(PoolQueryGate::Introductions(*later)[0].nativeInstance==PoolQueryGate::Introductions(*run.core)[0].nativeInstance);
  CHECK(PoolQueryGate::PrefixDigest(PoolQueryGate::AcceptedResponses(*later))==PoolQueryGate::PrefixDigest(run.prefix));
  auto laterDiscovery=PoolQueryGate::Discover(*run.core,run.prefix,run.context());
  CHECK(laterDiscovery.diagnostic==run.core->DiagnosticState());
  auto laterRequest=a.Request();laterRequest.context=run.context();
  laterRequest.acceptedPrefix=PoolQueryGate::PrefixDigest(run.prefix);
  auto unsupported=PoolQueryGate::SearchAsync(run.core->Initial(),r,run.prefix,laterRequest,{900000121}).get();
  CHECK(unsupported.Candidates().empty() && unsupported.Errors().size()==1);
  CHECK(unsupported.Errors()[0].find("Pool replay prefix diverged")!=std::string::npos);
  std::cout<<"later-prefix search without source-event transport: "<<unsupported.Errors()[0]<<'\n';
  auto stalePrefix=run.prefix;stalePrefix.back().response[0]^=1;
  rejects([&]{PoolQueryGate::Recreate(*run.core,stalePrefix,run.context());},"prefix/context");
  auto staleContext=run.context();++staleContext.branch.value;
  rejects([&]{PoolQueryGate::Discover(*run.core,run.prefix,staleContext);},"prefix/context");
  rejects([&]{PoolQueryGate::Install(run.core,run.prefix,run.context(),onlyA,a);},"Stale");
  introducedBefore.unchanged(run);
  std::cout<<"longer-prefix recreation/discovery and stale rejection preserve source history\n";
  auto duplicateInitial=initial(r);
  duplicateInitial.cards.push_back({900000120,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  Run duplicate(duplicateInitial,r);duplicate.summon();
  auto dd=PoolQueryGate::Discover(*duplicate.core,duplicate.prefix,duplicate.context());
  CHECK(dd.requests.size()==2);
  const auto diagnostic=duplicate.core->DiagnosticState();StableInstanceId otherHandler;
  for(size_t i=0,at=32;i<u32(diagnostic,28);++i){
   if(u32(diagnostic,at+8)==900000120 && u32(diagnostic,at+20)==LOCATION_GRAVE)
    otherHandler={uint64_t(u32(diagnostic,at))|(uint64_t(u32(diagnostic,at+4))<<32)};
   at+=56+8*u32(diagnostic,at+52);
  }
  CHECK(otherHandler.value && otherHandler!=dd.requests[0].handler);
  auto de=PoolQueryGate::SearchAsync(duplicate.core->Initial(),r,duplicate.prefix,dd.requests[0],{900000121}).get();
  CHECK(de.Errors().empty() && de.Candidates()==std::vector<uint32_t>{900000121});
  auto other=dd;other.requests={dd.requests[0]};other.requests[0].handler=otherHandler;
  const Snapshot duplicateBefore(duplicate);
  rejects([&]{PoolQueryGate::Install(duplicate.core,duplicate.prefix,duplicate.context(),other,de);},"handler");
  duplicateBefore.unchanged(duplicate);continueNative(duplicate,dd,de);
  auto physicalInitial=initial(r);physicalInitial.cards.push_back({900000121,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  Run physical(physicalInitial,r);physical.summon();
  CHECK(physical.now.checkpoint.prompt[0]==MSG_SELECT_EFFECTYN);
  auto pd=PoolQueryGate::Discover(*physical.core,physical.prefix,physical.context());
  CHECK(pd.observations.size()==2 && pd.observations[0].result==1 && pd.observations[1].result==0);
  CHECK(pd.requests.size()==1 && pd.requests[0].parameters==qb.parameters);
  std::cout<<"same-code distinct native handler rejected; physical observations stay separate from missing-target requests\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
