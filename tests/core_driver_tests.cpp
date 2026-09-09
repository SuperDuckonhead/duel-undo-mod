#include "core_fixture.h"
#include "undo/core_driver.h"
#include "ocgapi.h"
#include <future>
#include <iostream>
using namespace undo;
static bool same(const Boundary& a,const Boundary& b) {
 return a.kind==b.kind && a.checkpoint.player==b.checkpoint.player && a.checkpoint.prompt==b.checkpoint.prompt && a.checkpoint.canonicalState==b.checkpoint.canonicalState && a.checkpoint.transcriptDigest==b.checkpoint.transcriptDigest;
}
static void waiting(const Boundary& b) {
 if(b.kind!=BoundaryKind::AwaitResponse)throw std::runtime_error("Expected waiting: "+b.failure);
 CHECK(!b.checkpoint.prompt.empty());
}
int main(int argc,char** argv) {
 try {
  if(argc==2) {
   auto view=ResourceView::Capture(argv[1]);
   CHECK(view->ScriptCount()>10000);CHECK(view->Cards().size()>10000);
   std::cout<<"runtime capture: "<<view->ScriptCount()<<" scripts, "<<view->Cards().size()<<" normalized cards\n";
   InitialState real;real.seed.resize(SEED_COUNT,42);real.resourceDigest=view->Fingerprint();real.duelOptions=5u<<16;
   uint32_t code=0;for(auto& entry:view->Cards())if((entry.second.type&0x11)==0x11 && entry.second.alias==0){code=entry.first;break;}CHECK(code);
   for(uint8_t p=0;p<2;++p){real.players[p]={8000,5,1};for(int i=0;i<20;++i)real.cards.push_back({code,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});}
   auto a=CoreDriver::Create(real,view);auto b=CoreDriver::Create(real,view);auto first=a->Advance();waiting(first);CHECK(same(first,b->Advance()));
   a->Submit({7,0,0,0});auto second=a->Advance();waiting(second);CHECK(same(first,b->Current()));b->Submit({7,0,0,0});CHECK(same(second,b->Advance()));
   std::cout<<"installed resources real engine interleaving/replay passed, normal card "<<code<<'\n';return 0;
  }
  const std::string root=UNDO_CORE_FIXTURE;fixture::database(root);
  fixture::sql(root+"/cards.cdb","INSERT INTO datas SELECT 900000002,ot,alias,setcode,33,atk,def,level,race,attribute,category FROM datas WHERE id=900000001; INSERT INTO datas SELECT 900000003,ot,alias,setcode,33,atk,def,level,race,attribute,category FROM datas WHERE id=900000001; INSERT INTO texts(id,name) VALUES(900000002,\'late\'),(900000003,\'missing\');");
  fixture::WriteFixtureFile(root+"/script/constant.lua",fixture::bytes("Debug.Message('first:'..math.random(1,2147483647)); math.randomseed(); Debug.Message('second:'..math.random(1,2147483647))"));
  fixture::WriteFixtureFile(root+"/script/utility.lua",{});fixture::WriteFixtureFile(root+"/script/procedure.lua",{});
  fixture::WriteFixtureFile(root+"/script/c900000002.lua",fixture::bytes("Debug.Message('frozen-late'); c900000002.initial_effect=function(c) end"));
  fixture::WriteFixtureFile(root+"/single/isolation.lua",fixture::bytes(R"lua(
local e=Effect.GlobalEffect()
e:SetType(0x802)
e:SetCode(1040)
e:SetOperation(function(e)
 e:Reset()
 local yes=Duel.SelectYesNo(0,123)
 if yes then Duel.CreateToken(0,900000002) else Duel.CreateToken(0,900000003) end
 Duel.SelectYesNo(1,456)
 Duel.SetLP(0,math.random(1000,7000)+Duel.TossDice(0,1))
end)
Duel.RegisterEffect(e,0)
Debug.Message('parameters:'..UNDO_SCENARIO_PARAMETERS)
)lua"));
  auto resources=ResourceView::Capture(root);
  InitialState initial;initial.seed.resize(SEED_COUNT);initial.seed[0]=123;initial.duelOptions=5u<<16;
  initial.resourceDigest=resources->Fingerprint();initial.scenarioName="single/isolation.lua";initial.scenarioParameters=fixture::bytes("fixed");
  for(uint8_t p=0;p<2;++p)for(int i=0;i<5;++i)initial.cards.push_back({900000001,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  auto live=CoreDriver::Create(initial,resources);
  CHECK(live->Current().failure=="Not advanced");
  bool early=false;try{live->Submit({1});}catch(const std::logic_error&){early=true;}CHECK(early);
  auto liveStart=live->Advance();waiting(liveStart);CHECK(liveStart.checkpoint.prompt[0]==MSG_SELECT_YESNO);CHECK(liveStart.checkpoint.player==0);
  auto transcript=live->Transcript();auto logs=live->Logs();
  fixture::WriteFixtureFile(root+"/script/c900000002.lua",fixture::bytes("error('mutable disk read')"));
  auto candidate=CoreDriver::Create(initial,resources);
  auto candidateStart=candidate->Advance();waiting(candidateStart);CHECK(same(liveStart,candidateStart));
  CHECK(same(liveStart,live->Current()));CHECK(logs==live->Logs());CHECK(transcript==live->Transcript());
  live->Submit({99,0,0,0});auto retry=live->Advance();waiting(retry);CHECK(retry.rejectedResponse);CHECK(same(liveStart,retry));CHECK(live->Transcript()==transcript);
  candidate->Submit({0,0,0,0});auto failed=candidate->Advance();CHECK(failed.kind==BoundaryKind::Failed);std::cerr << "candidate failure: " << failed.failure << "\n";CHECK(failed.failure=="Pinned resource missing: script/c900000003.lua");
  candidate.reset();CHECK(same(liveStart,live->Current()));CHECK(live->Logs()==logs);
  live->Submit({1,0,0,0});auto second=live->Advance();waiting(second);CHECK(second.checkpoint.player==1);CHECK(!second.rejectedResponse);
  CHECK(live->Logs().back()=="frozen-late");
  auto replay=CoreDriver::Create(initial,resources);CHECK(same(liveStart,replay->Advance()));replay->Submit({1,0,0,0});CHECK(same(second,replay->Advance()));
  live->Submit({1,0,0,0});auto end=live->Advance();waiting(end);
  replay->Submit({1,0,0,0});CHECK(same(end,replay->Advance()));CHECK(live->Logs()==replay->Logs());
  CHECK(end.checkpoint.canonicalState!=second.checkpoint.canonicalState);
  // Creating/processing another session on another caller thread is serialized.
  auto concurrent=std::async(std::launch::async,[&](){auto d=CoreDriver::Create(initial,resources);return d->Advance();});
  CHECK(same(liveStart,concurrent.get()));CHECK(same(end,live->Current()));
  bool digest=false;auto wrong=initial;wrong.resourceDigest[0]^=1;try{CoreDriver::Create(wrong,resources);}catch(const std::exception&){digest=true;}CHECK(digest);
  std::cout<<"real core isolation, retry exclusion, frozen late read, failure recovery and replay passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}