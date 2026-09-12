#include "fusion_gold_fixture.h"
#include "single_duel.h"
#include "undo/player_restore.h"
#include "game.h"
#include "client_card.h"
#include "data_manager.h"
#include "undo/host_bot_seat.h"
#include "undo/coordinator.h"
#include "file_system.h"
#include <chrono>
#include <thread>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&){throw std::runtime_error("Unexpected GUI");}
void Game::AddDebugMsg(const char*){throw std::runtime_error("Unexpected GUI log");}
void DeckBuilder::RefreshPackListScroll(){throw std::runtime_error("Unexpected editor");}
}
struct RecipientBuilder : ygo::SingleDuel {
 const CoreDriver* current{};
 ygo::DuelPlayer endpoints[2];
 std::array<std::vector<Bytes>,2> frames;
 std::array<size_t,2> birthAt{};
 std::array<Bytes,2> birth;
 RecipientBuilder(const InitialState& initial):SingleDuel(false) {
  host_info.time_limit=0;
  for(uint8_t p=0;p<2;++p) {
   endpoints[p].type=p;endpoints[p].game=this;players[p]=&endpoints[p];
   Bytes start{MSG_START,p,5};fixture::word(start,8000);fixture::word(start,8000);
   for(unsigned owner=0;owner<2;++owner) {
    for(auto location:{LOCATION_DECK,LOCATION_EXTRA})fixture::word(start,std::count_if(initial.cards.begin(),initial.cards.end(),[&](const auto& c){return c.controller==owner && c.location==location;}),2);
   }
   frames[p].push_back(std::move(start));
  }
 }
 bool RoutePacket(ygo::DuelPlayer* p,uint8_t opcode,const unsigned char* data,size_t size) override {
  CHECK(opcode==STOC_GAME_MSG);frames[p->type].emplace_back(data,data+size);return true;
 }
 Bytes QueryFieldBytes(int p,int l,unsigned flags,int)override{return current->QueryField(p,l,flags&0xefffff);}
 Bytes QueryCardBytes(int p,int l,int seq,unsigned flags)override{return current->QueryCard(p,l,seq,flags&0xefffff);}
 void collect(const CoreDriver& core,const CoreOutput& output) {
  current=&core;
  if(output.sourceBirth)for(uint8_t p=0;p<2;++p) {
   CHECK(birth[p].empty());auto patch=*output.sourceBirth;patch.recipient=p;birth[p]=EncodeTestStatePatch(patch);birthAt[p]=frames[p].size();
  }
  else {auto frame=output.message;Analyze(frame.data(),unsigned(frame.size()));}
 }
};
static InitialState hiddenInitial(std::shared_ptr<const ResourceView> r,uint32_t code) {
 auto state=initial(r,code);auto spell=state.cards.front();state.cards.erase(state.cards.begin());
 spell.location=LOCATION_DECK;spell.position=POS_FACEDOWN_DEFENSE;state.cards.push_back(spell);state.players[0].startCount=1;return state;
}
static Bytes packet(const Bytes& frame) {Bytes b{STOC_GAME_MSG};b.insert(b.end(),frame.begin(),frame.end());return b;}
static PreparedContinuation continuation(const RecipientBuilder& builder,unsigned p) {
 PreparedContinuation c;c.birth=builder.birth[p];
 for(size_t i=builder.birthAt[p];i<builder.frames[p].size();++i)c.messages.push_back(packet(builder.frames[p][i]));
 return c;
}
static BotCompletion waitBot(HostBotSeat& bot,uint64_t job) {
 CHECK(job);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(40);
 while(std::chrono::steady_clock::now()<end) {
  for(auto& c:bot.Poll())if(c.job==job)return c;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 throw std::runtime_error("Real bot completion timeout");
}
static Bytes deckFor(const InitialState& state,unsigned owner) {
 std::string text="#main\n";
 for(const auto& c:state.cards)if(c.controller==owner && c.location==LOCATION_DECK)text+=std::to_string(c.code)+"\n";
 text+="#extra\n";for(const auto& c:state.cards)if(c.controller==owner && c.location==LOCATION_EXTRA)text+=std::to_string(c.code)+"\n";
 text+="!side\n";return Bytes(text.begin(),text.end());
}
static void exportRecipientFixture(const char* name,const InitialState& state,const RecipientBuilder& builder,const Bytes& cardView) {
 Bytes out;auto blob=[&](const Bytes& b){fixture::word(out,uint32_t(b.size()));out.insert(out.end(),b.begin(),b.end());};
 fixture::word(out,1);blob(cardView);
 for(unsigned p=0;p<2;++p) {
  blob(deckFor(state,p));fixture::word(out,uint32_t(builder.birthAt[p]));
  for(size_t i=0;i<builder.birthAt[p];++i)blob(packet(builder.frames[p][i]));
  blob(builder.birth[p]);fixture::word(out,uint32_t(builder.frames[p].size()-builder.birthAt[p]));
  for(size_t i=builder.birthAt[p];i<builder.frames[p].size();++i)blob(packet(builder.frames[p][i]));
 }
 fixture::WriteFixtureFile(std::string(UNDO_HIDDEN_FIXTURE)+"/"+name+".bin",out);
}
static void transaction(const char* name,const InitialState& state,RecipientBuilder& builder,std::unique_ptr<CoreDriver>& candidate,
 std::unique_ptr<CoreDriver>& active,const std::wstring& executable,BotLaunchData init) {
 auto session=NewSessionId();Coordinator coordinator(session,0,false);
 init.testStatePatchVersion=1;init.hasCustomDeck=true;init.customDeck=deckFor(state,1);init.customDeckSource="fixture:physical-source.ydk";
 HostBotSeat bot(executable,init,session,0,1);auto status=waitBot(bot,1);CHECK(status.accepted);const auto oldPid=status.identity.activePid;
 for(size_t i=0;i<builder.birthAt[1];++i) {
  status=waitBot(bot,bot.Dispatch(0,1,packet(builder.frames[1][i])));
  if(!status.accepted)throw std::runtime_error("Native prefix rejected by bot: "+status.failure);
 }
 status=waitBot(bot,bot.Fence());CHECK(status.accepted);const auto cursor=status.cursor;
 const auto extension=EncodePreparedContinuation(continuation(builder,1));
 const auto originalState=active->DiagnosticState();const auto originalTapeCursor=cursor;
 ygo::ClientField field;PlayerViewState view;ClientRestore client(field,view,0,session,0);CHECK(client.NegotiateTestStatePatch(1));
 auto human=BuildPlayerRestore(0,std::vector<Bytes>(builder.frames[0].begin(),builder.frames[0].begin()+builder.birthAt[0]),builder.frames[0].back());
 auto humanContinuation=continuation(builder,0);humanContinuation.messages.pop_back();
 human.continuation=EncodePreparedContinuation(humanContinuation);human.visibleDigest=HashVisibleRestore(human);
 human=DecodePlayerRestore(EncodePlayerRestore(human));
 for(uint64_t request=1;request<=3;++request) {
  CHECK(coordinator.Request({session,0,request,0,{}},0,int64_t(request)));
  AuthoritativeTarget target;target.publicDigest=Sha256(fixture::bytes("bounded hidden-source recipient gate"));
  coordinator.Boundary(int64_t(request),false,target,{});const auto key=coordinator.ActiveKey();CHECK(coordinator.State()==TxState::Preparing);
  CHECK(client.Prepare(key,human,human.prompt));coordinator.Ready(key,0);CHECK(coordinator.State()==TxState::Preparing);
  if(request==1) {
   auto invalid=extension;invalid[0]=2;
   status=waitBot(bot,bot.Prepare(key,cursor,invalid));CHECK(!status.accepted && status.identity.state==BotState::Running && status.identity.activePid==oldPid);
   invalid=extension;invalid[8]=0;
   status=waitBot(bot,bot.Prepare(key,cursor,invalid));CHECK(!status.accepted && status.identity.state==BotState::Running);
   invalid=extension;invalid[29]=1;
   status=waitBot(bot,bot.Prepare(key,cursor,invalid));CHECK(!status.accepted && status.identity.state==BotState::Running);
   invalid=extension;++invalid[27]; // valid patch syntax, wrong pre-birth count in private candidate
   status=waitBot(bot,bot.Prepare(key,cursor,invalid));CHECK(!status.accepted && status.identity.state==BotState::Frozen && !status.candidatePid);
   coordinator.Fail(key,false);client.Abort(key);coordinator.AbortAck(key,0);
   status=waitBot(bot,bot.Abort(key));CHECK(status.accepted);coordinator.AbortAck(key,1);
   CHECK(coordinator.State()==TxState::Running && status.identity.activePid==oldPid && status.cursor==cursor && active->DiagnosticState()==originalState);continue;
  }
  status=waitBot(bot,bot.Prepare(key,cursor,extension));
  if(!status.accepted)throw std::runtime_error("Native source bot prepare failed: "+status.failure);
  CHECK(status.identity.activePid==oldPid && status.candidatePid && status.candidatePid!=oldPid && status.outputs.empty());
  CHECK(active->DiagnosticState()==originalState && field.deck[0].empty());
  CHECK(waitBot(bot,bot.Prepare(key,cursor,extension)).accepted);
  auto changed=extension;changed[0]=2;CHECK(!waitBot(bot,bot.Prepare(key,cursor,changed)).accepted);
  if(request==2) {
   coordinator.Fail(key,false);CHECK(coordinator.State()==TxState::Aborting);
   client.Abort(key);coordinator.AbortAck(key,0);
   status=waitBot(bot,bot.Abort(key));CHECK(status.accepted);coordinator.AbortAck(key,1);
   CHECK(coordinator.State()==TxState::Running && status.identity.activePid==oldPid && status.cursor==originalTapeCursor && !status.candidatePid);
   CHECK(active->DiagnosticState()==originalState && field.deck[0].empty());continue;
  }
  const auto candidatePid=status.candidatePid;coordinator.Ready(key,1);CHECK(coordinator.State()==TxState::Committing);
  status=waitBot(bot,bot.Commit(key));CHECK(status.accepted && status.identity.activePid==candidatePid && status.retainedPid==oldPid && status.commitCount==1);
  CHECK(status.cursor>cursor && status.outputs.empty());
  CHECK(client.Commit(key,1));active.swap(candidate);coordinator.CommitAck(key,0,1);
  CHECK(coordinator.State()==TxState::Committing && !bot.Dispatch(1,2,{STOC_SELECT_HAND}));
  coordinator.CommitAck(key,1,1);CHECK(coordinator.State()==TxState::Running && coordinator.Epoch()==1);
  status=waitBot(bot,bot.Resume(key,1));CHECK(status.accepted && !status.retainedPid && client.Resume(key));
  status=waitBot(bot,bot.Dispatch(1,3,{STOC_SELECT_HAND}));CHECK(status.accepted && !status.outputs.empty());
  for(const auto& output:status.outputs)CHECK(AcceptsBotOutput(output,status.identity,3));
  // A later ordinary restore must replay the recipient-only patch from tape.
  auto ordinary=key;ordinary.epoch=1;ordinary.request=4;
  status=waitBot(bot,bot.Prepare(ordinary,status.cursor));CHECK(status.accepted);
  status=waitBot(bot,bot.Abort(ordinary));CHECK(status.accepted && status.identity.activePid==candidatePid);
 }
 std::cout<<"PASS "<<name<<" actual coordinator + ClientRestore + HostBotSeat/control: abort isolation, retained commit, Resume continuation, ordinary patch replay\n";
}
int main(int argc,char** argv) {
 try {
  CHECK(argc==2 || argc==4);auto resources=ResourceView::Capture(argv[1]);
  auto state=hiddenInitial(resources,75500286);Run gold(state,resources);gold.idle();gold.activate(75500286);
  gold.core=PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context());gold.now=gold.core->Current();
  auto queries=PoolQueryGate::PendingQueries(*gold.core);
  auto query=std::find_if(queries.begin(),queries.end(),[](const auto& q){return q.api==PoolQueryApi::SelectMatching;});CHECK(query!=queries.end());
  SelectionPlan plan;plan.context=gold.context();plan.role=SelectionRole::ResolutionTarget;plan.bindingStage=BindingStage::Resolution;
  plan.cards.push_back({CardReferenceKind::NewCopy,{1001},89631139,0,CardSource::OwnMainDeck,{}});
  RecipientBuilder recipients(state);
  auto validGold=PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context());
  PoolQueryGate::Introduce(gold.core,gold.context(),*query,plan,[&](const auto& core,const auto& event){recipients.collect(core,event);});
  CHECK(PoolQueryGate::Introductions(*gold.core).size()==1);
  const auto ordered=gold.core->OrderedOutput();
  CHECK(std::count_if(ordered.begin(),ordered.end(),[](const auto& e){return bool(e.sourceBirth);})==1);
  CHECK(std::any_of(ordered.begin(),ordered.end(),[](const auto& e){return !e.message.empty();}));
  CHECK(!recipients.birth[0].empty() && recipients.birthAt[0]<recipients.frames[0].size());
  for(auto opcode:{MSG_DRAW,MSG_CHAINING})
   CHECK(std::any_of(recipients.frames[0].begin(),recipients.frames[0].begin()+recipients.birthAt[0],[&](const auto& frame){return frame[0]==opcode;}));
  SessionId session{};session[0]=31;TxKey key{session,0,1,0,{}};key.targetDigest[0]=1;
  auto prefix=recipients.frames[0];auto prompt=prefix.back();prefix.pop_back();
  auto restore=BuildPlayerRestore(0,prefix,prompt);
  ygo::ClientField field;PlayerViewState view;ClientRestore client(field,view,0,session,0);
  CHECK(!client.Prepare(key,restore,prompt));
  if(client.Error()!="missing card")throw std::runtime_error("Unpatched original Gold failed at wrong boundary: "+client.Error());
  restore.frames.resize(recipients.birthAt[0]);
  PreparedContinuation continuation;continuation.birth=recipients.birth[0];
  for(size_t i=recipients.birthAt[0];i+1<recipients.frames[0].size();++i) {
   Bytes packet{STOC_GAME_MSG};packet.insert(packet.end(),recipients.frames[0][i].begin(),recipients.frames[0][i].end());continuation.messages.push_back(std::move(packet));
  }
  restore.continuation=EncodePreparedContinuation(continuation);restore.visibleDigest=HashVisibleRestore(restore);
  restore=DecodePlayerRestore(EncodePlayerRestore(restore));
  CHECK(!client.Prepare(key,restore,prompt));CHECK(client.NegotiateTestStatePatch(1));
  if(!client.Prepare(key,restore,prompt))throw std::runtime_error(client.Error());
  CHECK(client.PreparedField()->deck[0].size()==1 && client.PreparedField()->remove[0].size()==1);
  CHECK(client.PreparedField()->remove[0][0]->code==89631139);
  std::cout<<"PASS original Gold hidden source recipient restore\n";
  auto fs=hiddenInitial(resources,44362883);Run fusion(fs,resources);fusion.idle();fusion.activate(44362883);
  fusion.core=PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context());fusion.now=fusion.core->Current();
  fusion.submit({1,0});CHECK(fusion.now.checkpoint.prompt[0]==MSG_SELECT_UNSELECT_CARD);
  auto materialQueries=PoolQueryGate::PendingQueries(*fusion.core);
  auto material=std::find_if(materialQueries.begin(),materialQueries.end(),[](const auto& q){return q.api==PoolQueryApi::FusionMaterials;});CHECK(material!=materialQueries.end());
  std::cout<<"Fusion binding handler="<<material->handlerCode<<" role="<<unsigned(material->role)<<" stage="<<unsigned(material->stage)<<" target="<<material->target.value<<" prefix="<<material->context.historyCursor<<" context="<<fusion.context().historyCursor<<"\n";
  SelectionPlan materials;materials.context=fusion.context();materials.role=SelectionRole::ResolutionMaterial;materials.bindingStage=BindingStage::Resolution;
  materials.cards.push_back({CardReferenceKind::NewCopy,{1002},89631139,0,CardSource::OwnMainDeck,{}});
  for(const auto& card:actual(*fusion.core))if(card.code==68468459)materials.cards.push_back({CardReferenceKind::Existing,{card.id},card.code,0,CardSource::ExistingState,CardLocation{0,card.location,card.sequence,card.position}});
  CHECK(materials.cards.size()==2);
  RecipientBuilder fr(fs);
  auto validFusion=PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context());
  PoolQueryGate::Introduce(fusion.core,fusion.context(),*material,materials,[&](const auto& core,const auto& event){fr.collect(core,event);});
  CHECK(PoolQueryGate::Introductions(*fusion.core).size()==1);
  auto record=PoolQueryGate::Introductions(*fusion.core)[0];
  CHECK(record.nativeInstance.value!=materials.cards[1].instance.value);
  auto moved=actual(*fusion.core,record.nativeInstance);CHECK(moved.location==LOCATION_GRAVE);
  CHECK((u32(fusion.core->QueryCard(moved.controller,moved.location,moved.sequence,QUERY_REASON),8)&(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION))==(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION));
  auto replay=PoolQueryGate::ReplayIntroduction(fs,resources,record);CHECK(replay->DiagnosticState()==fusion.core->DiagnosticState());
  auto next=fusion.core->Current();
  for(unsigned step=0;next.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++step) {
   CHECK(step<12);auto response=automatic(next.checkpoint);fusion.core->Submit(response);replay->Submit(response);
   next=fusion.core->Advance([&](const Bytes& frame){fr.collect(*fusion.core,{frame,{}});});waiting(next);
   fr.collect(*fusion.core,{next.checkpoint.prompt,{}});waiting(replay->Advance());
  }
  CHECK(replay->DiagnosticState()==fusion.core->DiagnosticState());
  CHECK(actual(*fusion.core,material->target).status&STATUS_PROC_COMPLETE);
  std::cout<<"PASS original Fusion whole material selection consumed new hidden source\n";
  if(argc==4) {
   std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
   ygo::DataManager manager;manager.IrrFileSystem=files.get();const auto root=std::filesystem::u8path(argv[1]);
   CHECK(manager.LoadDB((root/"cards.cdb").u8string().c_str()));
   FileSystem::TraversalDir((root/"expansions").u8string().c_str(),[&](const char* name,bool directory){
    if(directory)return;auto path=root/"expansions"/std::filesystem::u8path(name);auto extension=path.extension().u8string();
    if(extension==".cdb")CHECK(manager.LoadDB(path.u8string().c_str()));
    else if(extension==".zip" || extension==".ypk")CHECK(files->addFileArchive(path.u8string().c_str(),true,false,irr::io::EFAT_ZIP));
   });
   for(irr::u32 i=0;i<files->getFileArchiveCount();++i){auto* list=files->getFileArchive(i)->getFileList();for(irr::u32 j=0;j<list->getFileCount();++j){auto name=list->getFullFileName(j);if(std::filesystem::u8path(name.c_str()).extension()==".cdb")CHECK(manager.LoadDB(name.c_str()));}}
   BotLaunchData launch;launch.runtimeRoot=argv[3];launch.executor="ChainBurn";launch.deckFile="AI_ChainBurn";launch.dialog="kiwi.zh-TW";launch.chat=false;launch.seed=31871;
   launch.engine=Sha256(fixture::bytes("hidden-source-gate-native-build"));launch.resources=resources->Fingerprint();launch.cardView=CaptureBotCardView(*resources,manager,launch.engine);
   exportRecipientFixture("Gold",state,recipients,launch.cardView);exportRecipientFixture("Fusion",fs,fr,launch.cardView);
   auto executable=std::filesystem::absolute(std::filesystem::u8path(argv[2])).wstring();
   transaction("Gold",state,recipients,gold.core,validGold,executable,launch);
   transaction("Fusion",fs,fr,fusion.core,validFusion,executable,launch);
  }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
