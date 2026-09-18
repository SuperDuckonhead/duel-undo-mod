#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "game.h"
#include "netserver.h"
#include "mysocket.h"
#include <event2/thread.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <windows.h>
#if __has_include("undo/deck_test_upload.h")
#include "undo/deck_test_upload.h"
#define HAS_TEST_UPLOAD 1
#endif
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&){throw std::runtime_error("Unexpected GUI event");}
void Game::AddDebugMsg(const char*){throw std::runtime_error("Unexpected GUI diagnostic");}
void DeckBuilder::RefreshPackListScroll(){throw std::runtime_error("Unexpected editor callback");}
}
namespace irr {namespace io {IFileSystem* createFileSystem();}}
using namespace ygo;using namespace undo;
struct HostInspect : SingleDuel {
 static const Deck& DeckAt(SingleDuel& host){return (host.*(&HostInspect::pdeck))[0];}
 static bool Ready(SingleDuel& host){return (host.*(&HostInspect::ready))[0];}
 static void FaultFirstCard(SingleDuel& host,const CardDataC* card){(host.*(&HostInspect::pdeck))[0].main[0]=card;}
};
static void fixedOpening(const std::shared_ptr<const ResourceView>& resources) {
 WSADATA winsock{};CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);CHECK(evthread_use_windows_threads()==0);
 // The persistent room-control poll keeps this owner loop alive after the
 // ordinary opening disables accepts, including between the following cases.
 Hello capability;capability.mode=RoomMode::LoopbackFree;
 unsigned short port{};CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&capability));CHECK(port);
 struct StopListener {~StopListener(){NetServer::StopServer();for(int i=0;i<2500&&NetServer::IsRunning();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));}} stop;
 HostInfo settings{};settings.rule=5;settings.mode=MODE_SINGLE;settings.duel_rule=5;
 settings.start_lp=8000;settings.start_hand=5;settings.draw_count=1;settings.no_check_deck=true;settings.no_shuffle_deck=true;
 const std::vector<std::uint32_t> cards{89631139,46986414,89631139,46986414,89631139,46986414,89631139,46986414,89631139,46986414};
 for(int scenario:{0,1,2}) {
  const bool test=scenario!=0,broken=scenario==2;
  auto config=std::make_shared<RoomConfig>();config->resources=resources;config->capability.mode=RoomMode::LoopbackFree;
  auto snapshot=std::make_shared<const TestDuelConfig>(7,cards,std::vector<std::uint32_t>{},settings,L"host");
  if(test)config->deckTest=snapshot;
  SessionId session{};session[0]=8;DuelPlayer human{},opponent{};human.endpointId=1;opponent.endpointId=2;human.undoPeer.ready=opponent.undoPeer.ready=true;
  std::vector<Envelope> received;
  UndoDuel host(false,config,session,[&](DuelPlayer*,const Envelope& e){received.push_back(e);return true;});host.host_info=settings;
  host.JoinGame(&human,nullptr,true);CTOS_JoinGame join{};join.version=PRO_VERSION;host.JoinGame(&opponent,reinterpret_cast<unsigned char*>(&join),false);
  Bytes ordinary;BufferIO::VectorWrite<std::uint32_t>(ordinary,cards.size());BufferIO::VectorWrite<std::uint32_t>(ordinary,0);
  for(auto code:cards)BufferIO::VectorWrite<std::uint32_t>(ordinary,code);
  if(test){auto deck=EncodeTestDeck(*snapshot,session);host.UpdateTestDeck(&human,deck.data(),deck.size());}
  else host.UpdateDeck(&human,ordinary.data(),ordinary.size());
  host.UpdateDeck(&opponent,ordinary.data(),ordinary.size());host.PlayerReady(&human,true);
  host.StartDuel(&human);CHECK(!host.HasActiveDuel());
  host.PlayerReady(&opponent,true);host.StartDuel(&opponent);CHECK(!host.HasActiveDuel());
  if(test){host.host_info.start_lp=1;host.host_info.start_hand=1;host.host_info.no_shuffle_deck=false;}
  CardDataC unavailable=dataManager.GetDataTable().at(cards.front());
  if(broken){unavailable.code=UINT32_MAX;HostInspect::FaultFirstCard(host,&unavailable);}
  host.StartDuel(&human);
  if(!test){CHECK(human.state==CTOS_HAND_RESULT && opponent.state==CTOS_HAND_RESULT);continue;}
  if(broken) {
   CHECK(!host.HasActiveDuel());CHECK(host.Status().state==TxState::PausedFailed);
   CHECK(std::any_of(received.begin(),received.end(),[](const Envelope& e){
    if(e.kind!=WireKind::Status)return false;const auto status=DecodeRoomStatus(e.payload);
    return status.state==TxState::PausedFailed && !status.prompt && !status.eligibleMask && status.promptPlayer==2;
   }));
   continue;
  }
  CHECK(host.HasActiveDuel());CHECK(human.type==0 && opponent.type==1);
  CHECK(human.state!=CTOS_HAND_RESULT && human.state!=CTOS_TP_RESULT);
  CHECK(host.Initial().noCheckDeck && host.Initial().noShuffleDeck);
  CHECK(host.Initial().players[0].lp==8000);
  CHECK(host.Initial().players[0].startCount==5 && host.Initial().players[0].drawCount==1);
  CHECK(host.Initial().duelOptions==((5u<<16)|DUEL_PSEUDO_SHUFFLE));
  std::vector<std::uint32_t> initial;
  for(const auto& card:host.Initial().cards)if(card.owner==0 && card.location==LOCATION_DECK)initial.push_back(card.code);
  CHECK(initial==std::vector<std::uint32_t>(cards.rbegin(),cards.rend()));
  auto baseline=CoreDriver::Create(host.Initial(),resources);const auto ordinaryBoundary=baseline->Advance();
  CHECK(ordinaryBoundary.kind==host.CurrentBoundary().kind);
  CHECK(ordinaryBoundary.checkpoint.prompt==host.CurrentBoundary().checkpoint.prompt);
  CHECK(ordinaryBoundary.checkpoint.canonicalState==host.CurrentBoundary().checkpoint.canonicalState);
  CHECK(ordinaryBoundary.checkpoint.transcriptDigest==host.CurrentBoundary().checkpoint.transcriptDigest);
  const auto before=host.Initial().seed;host.StartDuel(&human);CHECK(host.Initial().seed==before);
 }
 std::cout<<"PASS ordinary RPS retained; test host alone starts once with fixed first seat and exact initial deck order\n";
}
int main(int argc,char** argv){try {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);CHECK(argc==2);
 auto* files=irr::io::createFileSystem();dataManager.IrrFileSystem=files;
 CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
 auto config=std::make_shared<RoomConfig>();config->resources=ResourceView::Capture(argv[1]);
 config->capability.mode=RoomMode::LoopbackFree;
 SessionId session{};session[0]=7;
 DuelPlayer human{};human.endpointId=1;human.undoPeer.ready=true;
 std::vector<uint32_t> main(61,89631139),extra(16,23995346);main[1]=46986414;
#ifdef HAS_TEST_UPLOAD
 config->deckTest=std::make_shared<const TestDuelConfig>(42,main,extra,HostInfo{},L"Frozen human");
#endif
 UndoDuel host(false,config,session);host.host_info.no_check_deck=true;
 host.JoinGame(&human,nullptr,true);
 Bytes bytes;
#ifdef HAS_TEST_UPLOAD
 bytes=EncodeTestDeck(*config->deckTest,session);
 host.UpdateTestDeck(&human,bytes.data(),bytes.size());
#else
 BufferIO::VectorWrite<uint32_t>(bytes,77);BufferIO::VectorWrite<uint32_t>(bytes,0);
 for(auto code:main)BufferIO::VectorWrite<uint32_t>(bytes,code);
 for(auto code:extra)BufferIO::VectorWrite<uint32_t>(bytes,code);
 host.UpdateDeck(&human,bytes.data(),bytes.size());
#endif
 CHECK(HostInspect::DeckAt(host).main.size()==61);CHECK(HostInspect::DeckAt(host).extra.size()==16);
 CHECK(HostInspect::DeckAt(host).main[1]->code==46986414);CHECK(HostInspect::DeckAt(host).side.empty());
 std::cout<<"PASS actual host preserves nonstandard main/extra counts and order\n";
#ifdef HAS_TEST_UPLOAD
 // Failed replacement must never publish a prefix or admit Ready, even with
 // no_check_deck. Ordinary update must not bypass the test upload contract.
 for(auto malformed:{Bytes(bytes.begin(),bytes.end()-1),Bytes(70000,0),Bytes{}}) {
  host.UpdateTestDeck(&human,malformed.data(),malformed.size());
  host.PlayerReady(&human,true);CHECK(!HostInspect::Ready(host));
  CHECK(HostInspect::DeckAt(host).main.size()==61);CHECK(HostInspect::DeckAt(host).extra.size()==16);
 }
 host.UpdateTestDeck(&human,bytes.data(),bytes.size());host.PlayerReady(&human,true);
 CHECK(HostInspect::Ready(host));
 host.UpdateTestDeck(&human,bytes.data(),bytes.size());CHECK(HostInspect::Ready(host));
 // Independent literal wire expectation: side has no count or IDs at all.
 auto small=std::make_shared<const TestDuelConfig>(42,std::vector<uint32_t>{1,2,1},std::vector<uint32_t>{3},HostInfo{},L"x");
 auto wire=EncodeTestDeck(*small,session);
 CHECK(wire.size()==48);CHECK(wire[24]==3 && wire[28]==1);
 CHECK((Bytes(wire.begin()+32,wire.end())==Bytes{1,0,0,0,2,0,0,0,1,0,0,0,3,0,0,0}));
 // Every event permutation and duplicate must enqueue once. Wrong generation,
 // session, and acceptance before all prerequisites cannot authorize Ready.
 std::array<int,3> order{0,1,2};
 do {
  DeckTestUpload upload(small);auto success=TestDeckResult(42,session,TestDeckError::None);
  CHECK(!upload.Accept(42,success));
  upload.Joined(41);upload.Seat(41,0x10);upload.Capability(41,session);CHECK(!upload.TakeUpload(42));
  for(int event:order){
   if(event==0)upload.Joined(42);
   if(event==1){upload.Seat(42,0x11);CHECK(!upload.TakeUpload(42));upload.Seat(42,0x10);}
   if(event==2)upload.Capability(42,session);
  }
  CHECK(upload.TakeUpload(42)==wire);CHECK(!upload.TakeUpload(42));
  auto stale=success;stale[8]++;CHECK(!upload.Accept(42,stale));
  stale=success;stale[0]--;CHECK(!upload.Accept(42,stale));
  CHECK(!upload.Accept(41,success));CHECK(upload.Accept(42,success));CHECK(!upload.Accept(42,success));
  upload.Joined(42);upload.Seat(42,0x10);upload.Capability(42,session);CHECK(!upload.TakeUpload(42));
  upload.Ready(42,PLAYERCHANGE_READY);upload.Ready(42,0x10|PLAYERCHANGE_READY);
  CHECK(upload.Inspect().humanReady && upload.Inspect().opponentReady);
 }while(std::next_permutation(order.begin(),order.end()));
 for(bool cancel:{false,true}) {
  DeckTestUpload upload(small);upload.Joined(42);upload.Seat(42,0x10);upload.Capability(42,session);CHECK(upload.TakeUpload(42));
  if(cancel)upload.Cancel(42);
  else CHECK(!upload.Accept(42,TestDeckResult(42,session,TestDeckError::UnknownCard)));
  CHECK(!upload.Accept(42,TestDeckResult(42,session,TestDeckError::None)));CHECK(!upload.TakeUpload(42));
 }
 for(auto count:{250u,251u,17000u}) {
  auto input=std::make_shared<const TestDuelConfig>(1,std::vector<uint32_t>(count,89631139),std::vector<uint32_t>{},HostInfo{},L"x");
  DeckTestUpload upload(input);upload.Joined(1);upload.Seat(1,0x10);upload.Capability(1,session);
  CHECK(bool(upload.TakeUpload(1))==(count==250));
  if(count!=250)CHECK(upload.Inspect().error==TestDeckError::Capacity && !upload.Inspect().readySent);
 }
 // Ordinary loader continues to cap/repartition; new code must not change it.
 std::vector<uint32_t> ordinary(61,89631139);ordinary.insert(ordinary.end(),16,23995346);ordinary.push_back(46986414);
 Deck old;CHECK(DeckManager::LoadDeck(old,ordinary.data(),77,1)==0);
 CHECK(old.main.size()==60 && old.extra.size()==15 && old.side.size()==1);
 std::cout<<"PASS failed upload atomicity/Ready gate, immutable encoding, event permutations/stale replies/cancellation/capacity, ordinary loading\n";
 fixedOpening(config->resources);
#endif
 files->drop();
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
