#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include "undo/room_restore.h"
#include "undo/player_restore.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&){throw std::runtime_error("Unexpected GUI event");}
void Game::AddDebugMsg(const char*){throw std::runtime_error("Unexpected GUI diagnostic");}
void DeckBuilder::RefreshPackListScroll(){throw std::runtime_error("Unexpected editor callback");}
}
namespace irr {namespace io {IFileSystem* createFileSystem();}}
using namespace undo;using namespace ygo;
static Bytes integer(std::uint32_t n){return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}
static void run(const std::shared_ptr<const ResourceView>& resources,const std::string& mode){
 auto config=std::make_shared<RoomConfig>();config->resources=resources;config->capability.resources=resources->Fingerprint();
 config->capability.mode=mode=="consent"?RoomMode::ConsentLan:RoomMode::LoopbackFree;
 const auto session=NewSessionId();std::array<std::vector<Envelope>,2> output;
 std::size_t expectedFrozen{};bool checkFinal=false;
 DuelPlayer a{},b{};a.endpointId=1;b.endpointId=2;a.undoPeer.ready=b.undoPeer.ready=true;
 UndoDuel host(false,config,session,[&](DuelPlayer* p,const Envelope& e){
  output.at(p->endpointId-1).push_back(e);
  if(checkFinal && e.kind==WireKind::Abort && e.payload.size()==2 && e.payload[1]==1){
   CHECK(host.History().Records().size()==expectedFrozen);
   if(mode=="delivery-fail" && p==&b)return false;
  }
  return true;
 });
 BufferIO::CopyCharArray(L"First",a.name);BufferIO::CopyCharArray(L"Second",b.name);
 host.host_info.duel_rule=5;host.host_info.start_lp=80000;host.host_info.start_hand=5;host.host_info.draw_count=1;
 host.host_info.time_limit=60;host.host_info.no_check_deck=true;host.host_info.no_shuffle_deck=true;
 host.JoinGame(&a,nullptr,true);CTOS_JoinGame join{};join.version=PRO_VERSION;host.JoinGame(&b,reinterpret_cast<unsigned char*>(&join),false);
 Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
 for(int i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,70368879);
 host.UpdateDeck(&a,deck.data(),deck.size());host.UpdateDeck(&b,deck.data(),deck.size());a.state=CTOS_TP_RESULT;host.TPResult(&a,1);CHECK(host.HasActiveDuel());
 std::array<std::uint64_t,2> sequences{};
 auto respond=[&](Bytes response,Origin origin){
  auto* p=host.CurrentBoundary().checkpoint.player==a.type?&a:&b;
  for(const auto& e:EncodeGamePacket(session,host.InstalledEpoch(),host.Status().prompt,++sequences[p->endpointId-1],{CTOS_TIME_CONFIRM}))host.ReceiveUndo(p,e);
  host.ReceiveUndo(p,EncodeResponse({session,host.InstalledEpoch(),host.Status().prompt,0,{}},origin,response));
 };
 auto idle=[&]{for(int i=0;host.CurrentBoundary().checkpoint.prompt.at(0)!=MSG_SELECT_IDLECMD;++i){CHECK(i<30 && host.CurrentBoundary().checkpoint.prompt.at(0)==MSG_SELECT_CHAIN);respond(integer(0xffffffffu),Origin::Automatic);}};
 idle();respond(integer(7),Origin::Manual);idle();respond(integer(7),Origin::Manual);idle();
 CHECK(host.CurrentBoundary().checkpoint.player==a.type && host.History().Target(b.type));
 Bytes response=integer(7);Origin origin=Origin::Manual;
 if(mode=="automatic"){
  respond(integer(5),Origin::Manual);const auto prompt=host.CurrentBoundary().checkpoint.prompt;CHECK(prompt.at(0)==MSG_SELECT_PLACE && prompt.size()==7);
  const auto disabled=std::uint32_t(prompt[3])|std::uint32_t(prompt[4])<<8|std::uint32_t(prompt[5])<<16|std::uint32_t(prompt[6])<<24;
  unsigned slot=0;while(slot<5 && (disabled&(1u<<(8+slot))))++slot;CHECK(slot<5);response={a.type,LOCATION_SZONE,std::uint8_t(slot)};origin=Origin::Automatic;
 }
 auto waiting=host.CurrentBoundary();const auto token=TxKey{session,0,host.Status().prompt,0,{}};
 auto sent=EncodeResponse(token,origin,response);const auto before=host.History().Records().size();
 if(mode=="response-first"){respond(response,origin);CHECK(host.History().Records().size()==before+1);}
 auto request=TxKey{session,0,host.Status().nextRequest,0,{}};host.ReceiveUndo(&b,{WireKind::Request,request,{}});const auto key=host.ActiveKey();
 CHECK(host.Status().state==(mode=="consent"?TxState::Consent:TxState::Preparing));const auto frozen=host.Status().clock.remainingMs;
 auto stale=sent;stale.key.session[0]^=1;host.ReceiveUndo(&a,stale);stale=sent;++stale.key.epoch;host.ReceiveUndo(&a,stale);
 stale=sent;++stale.key.request;host.ReceiveUndo(&a,stale);host.ReceiveUndo(&b,sent);
 DuelPlayer impostor=a;host.ReceiveUndo(&impostor,sent); // same metadata cannot replace an authenticated endpoint

 for(const auto& e:EncodeGamePacket(session,0,token.request,++sequences[0],{CTOS_TIME_CONFIRM}))host.ReceiveUndo(&a,e);
 host.ReceiveUndo(&a,sent);host.ReceiveUndo(&a,sent);auto conflict=sent;conflict.payload.back()^=1;host.ReceiveUndo(&a,conflict);
 const auto frozenCount=host.History().Records().size();CHECK(frozenCount==before+(mode=="response-first"?1:0));CHECK(host.Status().clock.remainingMs==frozen);expectedFrozen=frozenCount;checkFinal=true;
 ClientField fields[2];PlayerViewState views[2];std::unique_ptr<ClientRestore> clients[2];
 if(mode=="consent")host.ReceiveUndo(&a,{WireKind::Consent,key,{0}});
 else {
  for(unsigned seat=0;seat<2;++seat){
   FragmentAssembler fragments;for(const auto& e:output[seat])if(e.kind==WireKind::Prepare && SameKey(e.key,key))fragments.Add(e.payload);
   auto restore=DecodePlayerRestore(DecodeRoomRestore(fragments.Finish()).visible);clients[seat]=std::make_unique<ClientRestore>(fields[seat],views[seat],seat,session,0);
   if(mode!="commit" && seat==1)restore.visibleDigest[0]^=1;
   const bool ready=clients[seat]->Prepare(key,restore,restore.prompt);CHECK(ready==(mode=="commit" || seat==0));
   host.ReceiveUndo(seat?&b:&a,{WireKind::Ready,key,{std::uint8_t(ready)}});
  }
  if(mode=="commit"){
   for(int i=0;i<5000 && host.Status().state==TxState::Preparing;++i){host.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   CHECK(host.Status().state==TxState::Committing);const auto retained=host.History().Records().size();Bytes epoch(8);epoch[0]=1;
   for(unsigned i=0;i<2;++i){CHECK(clients[i]->Commit(key,1));host.ReceiveUndo(i?&b:&a,{WireKind::CommitAck,key,epoch});CHECK(clients[i]->Resume(key));}
   CHECK(host.Status().state==TxState::Running && host.InstalledEpoch()==1);for(int i=0;i<10;++i)host.PollUndo();CHECK(host.History().Records().size()==retained);
   std::cout<<"PASS successful Commit discards old-token pending response\n";return;
  }
  CHECK(host.Status().state==TxState::Aborting);clients[0]->Abort(key);host.ReceiveUndo(&a,{WireKind::AbortAck,key,{}});
  for(int i=0;i<10;++i)host.PollUndo();CHECK(host.Status().state==TxState::Aborting && host.History().Records().size()==frozenCount);
  clients[1]->Abort(key);host.ReceiveUndo(&b,{WireKind::AbortAck,key,{}});
 }
 if(mode=="delivery-fail"){
  CHECK(host.Status().state==TxState::PausedFailed && host.InstalledEpoch()==0 && host.History().Records().size()==frozenCount);
  for(int i=0;i<10;++i)host.PollUndo();CHECK(host.History().Records().size()==frozenCount);
  std::cout<<"PASS failed final Abort delivery cannot drain pending response or reopen input\n";return;
 }
 CHECK(host.Status().state==TxState::Running && host.InstalledEpoch()==0);CHECK(host.History().Records().size()==before+1);
 if(mode!="response-first"){const auto& accepted=host.History().Records().back();CHECK(accepted.origin==origin && accepted.response==response && accepted.before.prompt==waiting.checkpoint.prompt);}
 host.ReceiveUndo(&a,sent);host.PollUndo();CHECK(host.History().Records().size()==before+1);
 for(unsigned p=0;p<2;++p){auto final=std::find_if(output[p].begin(),output[p].end(),[&](const Envelope& e){return e.kind==WireKind::Abort && SameKey(e.key,key) && e.payload.size()==2 && e.payload[1]==1;});CHECK(final!=output[p].end());}
 std::cout<<"PASS pending "<<mode<<" exact-token response resumes once only after complete Abort\n";
}
int main(int argc,char** argv){try{
 CHECK(argc==2);std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
 dataManager.IrrFileSystem=files.get();CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));auto resources=ResourceView::Capture(argv[1]);
 for(const auto* mode:{"prepare","consent","automatic","response-first","commit","delivery-fail"})run(resources,mode);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
