#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&){throw std::runtime_error("Unexpected GUI event");}
void Game::AddDebugMsg(const char*){throw std::runtime_error("Unexpected GUI diagnostic");}
void DeckBuilder::RefreshPackListScroll(){throw std::runtime_error("Unexpected editor callback");}
}
namespace irr {namespace io {IFileSystem* createFileSystem();}}
using namespace ygo;using namespace undo;
static Bytes integer(std::uint32_t n){return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}
struct Room {
 SessionId session=NewSessionId(); DuelPlayer a{},b{};std::array<std::uint64_t,2> sequence{};
 std::array<std::vector<Envelope>,2> output;std::unique_ptr<UndoDuel> duel;
 explicit Room(std::shared_ptr<const ResourceView> resources) {
  auto config=std::make_shared<RoomConfig>();config->resources=resources;
  config->capability.resources=resources->Fingerprint();config->capability.mode=RoomMode::ConsentLan;
  a.endpointId=1;b.endpointId=2;a.undoPeer.ready=b.undoPeer.ready=true;
  duel=std::make_unique<UndoDuel>(false,config,session,[&](DuelPlayer* p,const Envelope& e){output[p->endpointId-1].push_back(e);return true;});
  auto& h=*duel;h.host_info.duel_rule=5;h.host_info.start_lp=80000;h.host_info.start_hand=5;h.host_info.draw_count=1;
  h.host_info.time_limit=180;h.host_info.no_check_deck=true;h.host_info.no_shuffle_deck=true;
  h.JoinGame(&a,nullptr,true);CTOS_JoinGame join{};join.version=PRO_VERSION;h.JoinGame(&b,reinterpret_cast<unsigned char*>(&join),false);
  Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
  for(int i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,70368879);
  h.UpdateDeck(&a,deck.data(),deck.size());h.UpdateDeck(&b,deck.data(),deck.size());a.state=CTOS_TP_RESULT;h.TPResult(&a,1);
  CHECK(h.HasActiveDuel());idle();
 }
 DuelPlayer* actor(){return duel->CurrentBoundary().checkpoint.player==a.type?&a:&b;}
 TxKey token(){return {session,duel->InstalledEpoch(),duel->Status().prompt,0,{}};}
 void confirm(DuelPlayer* p=nullptr){if(!p)p=actor();for(const auto& e:EncodeGamePacket(session,duel->InstalledEpoch(),duel->Status().prompt,++sequence[p->endpointId-1],{CTOS_TIME_CONFIRM}))duel->ReceiveUndo(p,e);}
 void respond(Bytes response,Origin origin=Origin::Manual){duel->ReceiveUndo(actor(),EncodeResponse(token(),origin,response));}
 void idle(){for(unsigned i=0;duel->CurrentBoundary().checkpoint.prompt.at(0)!=MSG_SELECT_IDLECMD;++i){CHECK(i<30);CHECK(duel->CurrentBoundary().checkpoint.prompt.at(0)==MSG_SELECT_CHAIN);confirm();respond(integer(0xffffffffu),Origin::Automatic);}}
 void endTurn(){confirm();respond(integer(7));idle();}
 void ticks(unsigned n){while(n--)duel->TimerTick();}
 void pump(){for(unsigned i=0;i<10000 && duel->Status().state==TxState::Preparing;++i){duel->PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
};
int main(int argc,char** argv){try {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);CHECK(argc==2);
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
 dataManager.IrrFileSystem=files.get();CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
 auto resources=ResourceView::Capture(argv[1]);
 {
  Room r(resources);auto& h=*r.duel;auto* p=r.actor();CHECK(p->state==CTOS_TIME_CONFIRM);
  const auto before=h.History().Records().size();r.respond(integer(7));CHECK(h.History().Records().size()==before);
  r.confirm(p==&r.a?&r.b:&r.a);CHECK(p->state==CTOS_TIME_CONFIRM);
  r.ticks(3);CHECK(h.Status().clock.remainingMs[p->type]==177000); // Native one-second timer is the sole clock.
  r.confirm();CHECK(p->state==CTOS_RESPONSE);CHECK(h.Status().clock.remainingMs[p->type]==180000); // Native <10 s acknowledgement grace.
  r.ticks(4);r.confirm();CHECK(h.Status().clock.remainingMs[p->type]==176000); // Duplicate acknowledgement cannot renew time.
  const auto prompt=h.Status().prompt;r.respond(integer(0xfffffff0u));
  CHECK(h.CurrentBoundary().rejectedResponse && h.History().Records().size()==before && h.Status().prompt==prompt);
  CHECK(p->state==CTOS_TIME_CONFIRM);CHECK(h.Status().clock.remainingMs[p->type]==176000);
  r.confirm();r.ticks(2);r.respond(integer(5));CHECK(h.History().Records().size()==before+1);
  CHECK(h.History().Records().back().before.clock.remainingMs[p->type]==174000);
  CHECK(h.Status().clock.remainingMs[p->type]==174000 && r.actor()->state==CTOS_TIME_CONFIRM);
  std::cout<<"PASS native response gate, timer ticks/grace, duplicate confirm, rejected history and debit\n";
 }
 for(const char* mode:{"reject","prepare-failure","commit"}){
  Room r(resources);auto& h=*r.duel;r.endTurn();r.endTurn();
  auto* p=r.actor();r.confirm();r.ticks(5);const auto clock=h.Status().clock;
  const auto oldToken=r.token();const auto before=h.History().Records().size();const auto keep=*h.History().Target(p->type);
  const auto target=h.History().Records()[keep].before;TxKey request{r.session,h.InstalledEpoch(),h.Status().nextRequest,0,{}};
  h.ReceiveUndo(p,{WireKind::Request,request,{}});const auto key=h.ActiveKey();CHECK(h.Status().state==TxState::Consent);
  r.ticks(12);CHECK(h.Status().clock.remainingMs==clock.remainingMs);
  h.ReceiveUndo(p==&r.a?&r.b:&r.a,{WireKind::Consent,key,{std::uint8_t(std::string(mode)!="reject")}});
  if(std::string(mode)!="reject"){
   CHECK(h.Status().state==TxState::Preparing);
   h.ReceiveUndo(&r.a,{WireKind::Ready,key,{1}});h.ReceiveUndo(&r.b,{WireKind::Ready,key,{std::uint8_t(std::string(mode)=="commit")}});
   if(std::string(mode)=="commit"){
    r.pump();CHECK(h.Status().state==TxState::Committing);r.ticks(12);Bytes epoch(8);epoch[0]=1;
    h.ReceiveUndo(&r.a,{WireKind::CommitAck,key,epoch});h.ReceiveUndo(&r.b,{WireKind::CommitAck,key,epoch});
    CHECK(h.Status().state==TxState::Running && h.History().Records().size()==keep);
    CHECK(h.Status().clock.remainingMs==target.clock.remainingMs);r.sequence={};
    h.ReceiveUndo(p,EncodeResponse(oldToken,Origin::Manual,integer(7)));CHECK(h.History().Records().size()==keep);
   }else{
    CHECK(h.Status().state==TxState::Aborting);h.ReceiveUndo(&r.a,{WireKind::AbortAck,key,{}});h.ReceiveUndo(&r.b,{WireKind::AbortAck,key,{}});
   }
  }
  CHECK(h.Status().state==TxState::Running);auto resumed=h.Status().clock;
  if(std::string(mode)!="commit")CHECK(h.History().Records().size()==before && resumed.remainingMs==clock.remainingMs);
  p=r.actor();CHECK(p->state==CTOS_RESPONSE);r.ticks(2);CHECK(h.Status().clock.remainingMs[p->type]==resumed.remainingMs[p->type]-2000);
  r.respond(integer(7));r.idle();CHECK(h.Status().clock.remainingMs[0]==180000 && h.Status().clock.remainingMs[1]==180000);
  std::cout<<"PASS native pause/resume, input fence, continued turn and new-turn reset: "<<mode<<'\n';
 }
 for(bool pendingConfirmation:{false,true}) {
  Room r(resources);auto& h=*r.duel;r.endTurn();r.endTurn();auto* p=r.actor();CHECK(p->state==CTOS_TIME_CONFIRM);
  r.ticks(3);const auto clock=h.Status().clock;TxKey request{r.session,h.InstalledEpoch(),h.Status().nextRequest,0,{}};
  h.ReceiveUndo(p,{WireKind::Request,request,{}});const auto key=h.ActiveKey();CHECK(h.Status().state==TxState::Consent);
  if(pendingConfirmation)r.confirm();r.ticks(12);CHECK(p->state==CTOS_TIME_CONFIRM && h.Status().clock.remainingMs==clock.remainingMs);
  h.ReceiveUndo(p==&r.a?&r.b:&r.a,{WireKind::Consent,key,{0}});CHECK(h.Status().state==TxState::Running);
  CHECK(p->state==(pendingConfirmation?CTOS_RESPONSE:CTOS_TIME_CONFIRM));
  CHECK(h.Status().clock.remainingMs[p->type]==(pendingConfirmation?180000:177000));
  const auto before=h.History().Records().size();r.respond(integer(7));
  CHECK((h.History().Records().size()>before)==pendingConfirmation);
  std::cout<<"PASS abort preserves native unconfirmed gate, pending acknowledgement="<<pendingConfirmation<<'\n';
 }
 {
  Room r(resources);r.confirm();r.ticks(180);CHECK(!r.duel->HasActiveDuel());
  CHECK(!r.duel->History().Target(r.a.type));std::cout<<"PASS native timeout ends the duel\n";
 }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
