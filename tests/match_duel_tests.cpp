#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&){throw std::runtime_error("Unexpected GUI event");}
void Game::AddDebugMsg(const char*){throw std::runtime_error("Unexpected GUI diagnostic");}
void DeckBuilder::RefreshPackListScroll(){throw std::runtime_error("Unexpected editor callback");}
}
namespace irr {namespace io {IFileSystem* createFileSystem();}}
using namespace ygo;using namespace undo;
static Bytes integer(std::uint32_t n){return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}
static Bytes deck(bool sided=false,bool invalid=false) {
 Bytes b;BufferIO::VectorWrite<std::uint32_t>(b,6);BufferIO::VectorWrite<std::uint32_t>(b,1);
 for(unsigned i=0;i<6;++i)BufferIO::VectorWrite<std::uint32_t>(b,sided && !i?46986414:70368879);
 BufferIO::VectorWrite<std::uint32_t>(b,invalid?89631139:sided?70368879:46986414);return b;
}
struct Room {
 SessionId session=NewSessionId();DuelPlayer a{},b{};
 std::array<std::uint64_t,2> inputSequence{};
 std::array<std::unique_ptr<GamePacketStream>,2> stream;
 std::array<std::vector<Bytes>,2> packets;
 std::array<std::vector<Envelope>,2> output;
 std::array<std::vector<Bytes>,2> replays;
 std::array<std::uint64_t,2> clientEpoch{};
 std::array<bool,2> failedStatus{};
 int failRoundRecipient{-1};
 std::unique_ptr<UndoDuel> duel;
 explicit Room(std::shared_ptr<const ResourceView> resources,RoomMode mode=RoomMode::ConsentLan,bool match=true) {
  auto config=std::make_shared<RoomConfig>();config->resources=resources;
  config->capability.resources=resources->Fingerprint();config->capability.mode=mode;
  a.endpointId=1;b.endpointId=2;a.undoPeer.ready=b.undoPeer.ready=true;
  BufferIO::CopyCharArray(L"Host",a.name);BufferIO::CopyCharArray(L"Friend",b.name);
  for(auto& s:stream)s=std::make_unique<GamePacketStream>(session,0);
  duel=std::make_unique<UndoDuel>(match,config,session,[&](DuelPlayer* p,const Envelope& e){
   auto slot=p->endpointId-1;
   if(e.kind==WireKind::RoundStart && slot==failRoundRecipient){failRoundRecipient=-1;return false;}
   output[slot].push_back(e);
   if(e.kind==static_cast<WireKind>(15)) {
    CHECK(e.payload.size()==9);std::uint64_t epoch{};for(unsigned i=0;i<8;++i)epoch|=std::uint64_t(e.payload[i])<<(8*i);
    CHECK(epoch==e.key.epoch+1);stream[slot]->Reset(session,epoch);clientEpoch[slot]=epoch;
   } else if(e.kind==WireKind::Commit) {
    CHECK(e.payload.size()==8);std::uint64_t epoch{};for(unsigned i=0;i<8;++i)epoch|=std::uint64_t(e.payload[i])<<(8*i);
    stream[slot]->Reset(session,epoch);clientEpoch[slot]=epoch;
   } else if(e.kind==WireKind::Status && e.key.epoch==clientEpoch[slot]) {
    failedStatus[slot]=DecodeRoomStatus(e.payload).state==TxState::PausedFailed;
   } else if(e.kind==WireKind::Game) {
    auto packet=stream[slot]->Add(e);if(packet){
     packets[slot].push_back(packet->packet);
     if(packet->packet[0]==STOC_REPLAY)replays[slot].emplace_back(packet->packet.begin()+1,packet->packet.end());
    }
   }
   return true;
  });
  auto& h=*duel;h.host_info.mode=match?1:0;h.host_info.duel_rule=5;h.host_info.start_lp=80000;
  h.host_info.start_hand=5;h.host_info.draw_count=1;h.host_info.time_limit=180;
  h.host_info.no_check_deck=true;h.host_info.no_shuffle_deck=true;
  h.JoinGame(&a,nullptr,true);CTOS_JoinGame join{};join.version=PRO_VERSION;h.JoinGame(&b,reinterpret_cast<unsigned char*>(&join),false);
  auto d=deck();h.UpdateDeck(&a,d.data(),d.size());h.UpdateDeck(&b,d.data(),d.size());
  h.PlayerReady(&a,true);h.PlayerReady(&b,true);
  // The isolated host has no listening socket; enter native hand selection directly.
  h.duel_stage=DUEL_STAGE_FINGER;a.state=b.state=CTOS_HAND_RESULT;
  h.HandResult(&a,1);h.HandResult(&b,3);CHECK(a.state==CTOS_TP_RESULT);
  h.TPResult(&a,0);CHECK(h.HasActiveDuel());idle();CHECK(a.type==1 && b.type==0);
 }
 DuelPlayer* actor(){return duel->CurrentBoundary().checkpoint.player==a.type?&a:&b;}
 TxKey token(){return {session,duel->InstalledEpoch(),duel->Status().prompt,0,{}};}
 void confirm(){auto* p=actor();for(const auto& e:EncodeGamePacket(session,duel->InstalledEpoch(),duel->Status().prompt,++inputSequence[p->endpointId-1],{CTOS_TIME_CONFIRM}))duel->ReceiveUndo(p,e);}
 void respond(Bytes response,Origin origin=Origin::Manual){confirm();duel->ReceiveUndo(actor(),EncodeResponse(token(),origin,response));}
 void idle(){for(unsigned i=0;duel->HasActiveDuel() && duel->CurrentBoundary().checkpoint.prompt.at(0)!=MSG_SELECT_IDLECMD;++i){CHECK(i<30);CHECK(duel->CurrentBoundary().checkpoint.prompt.at(0)==MSG_SELECT_CHAIN);respond(integer(0xffffffffu),Origin::Automatic);}}
 void endTurn(){respond(integer(7));idle();}
 std::size_t packetCount(std::uint8_t opcode,int slot=0) const {return std::count_if(packets[slot].begin(),packets[slot].end(),[&](const auto& p){return p[0]==opcode;});}
 std::size_t roundCount(int slot=0) const {return std::count_if(output[slot].begin(),output[slot].end(),[](const auto& e){return e.kind==static_cast<WireKind>(15);});}
 void checkEnded(bool matchEnded,unsigned games) {
  CHECK(!duel->HasActiveDuel());CHECK(duel->duel_stage==(matchEnded?DUEL_STAGE_END:DUEL_STAGE_SIDING));
  CHECK(duel->History().Records().empty());CHECK(duel->Status().eligibleMask==0);
  const auto state=duel->Status();duel->ReceiveUndo(&a,{WireKind::Request,{session,duel->InstalledEpoch(),state.nextRequest,0,{}},{}});
  CHECK(duel->Status().state==TxState::Running && duel->Status().eligibleMask==0);
  for(unsigned p=0;p<2;++p){
   CHECK(replays[p].size()==games);CHECK(packetCount(STOC_CHANGE_SIDE,p)==(matchEnded?games-1:games));
   CHECK(packetCount(STOC_DUEL_END,p)==(matchEnded?1:0));
   Replay replay;CHECK(replay.LoadUndoReplay(replays[p].back()));CHECK(replay.UndoInitial().cards.size()==12);
  }
 }
 void next(DuelPlayer* loser,bool sided=false) {
  const auto epoch=duel->InstalledEpoch();const auto rounds=roundCount();CHECK(a.type==0 && b.type==1);
  CHECK(a.state==CTOS_UPDATE_DECK && b.state==CTOS_UPDATE_DECK);
  auto bad=deck(sided,true);duel->UpdateDeck(&a,bad.data(),bad.size());CHECK(duel->duel_stage==DUEL_STAGE_SIDING);
  CHECK(packetCount(STOC_ERROR_MSG)>0);
  auto d=deck(sided);duel->UpdateDeck(&a,d.data(),d.size());CHECK(duel->duel_stage==DUEL_STAGE_SIDING);
  duel->UpdateDeck(&b,d.data(),d.size());CHECK(duel->duel_stage==DUEL_STAGE_FIRSTGO);CHECK(loser->state==CTOS_TP_RESULT);
  duel->TPResult(loser,1);CHECK(duel->HasActiveDuel());CHECK(duel->InstalledEpoch()==epoch+1);
  inputSequence={};idle();CHECK(duel->History().Records().empty());CHECK(duel->Status().eligibleMask==0);
  CHECK(duel->Initial().cards.size()==12);CHECK(loser->type==0);
  CHECK(duel->Status().clock.remainingMs[0]==180000 && duel->Status().clock.remainingMs[1]==180000);
  CHECK(std::count_if(duel->Initial().cards.begin(),duel->Initial().cards.end(),[](const auto& c){return c.code==46986414;})==(sided?2:0));
  for(unsigned p=0;p<2;++p){CHECK(roundCount(p)==rounds+1);const auto& events=output[p];
   auto round=std::find_if(events.rbegin(),events.rend(),[](const auto& e){return e.kind==static_cast<WireKind>(15);});
   CHECK(round!=events.rend() && round->key.epoch==epoch && round->payload.back()==rounds+2);
  }
 }
 void cancelUndo(DuelPlayer* p,bool reject) {
  auto& h=*duel;const auto epoch=h.InstalledEpoch();const auto before=h.CurrentBoundary().checkpoint;
  const auto count=h.History().Records().size();const auto clock=h.Status().clock;
  h.ReceiveUndo(p,{WireKind::Request,{session,epoch,h.Status().nextRequest,0,{}},{}});auto key=h.ActiveKey();
  if(h.Status().state==TxState::Consent)h.ReceiveUndo(p==&a?&b:&a,{WireKind::Consent,key,{std::uint8_t(!reject)}});
  if(!reject){CHECK(h.Status().state==TxState::Preparing);h.ReceiveUndo(&a,{WireKind::Ready,key,{0}});
   CHECK(h.Status().state==TxState::Aborting);h.ReceiveUndo(&a,{WireKind::AbortAck,key,{}});h.ReceiveUndo(&b,{WireKind::AbortAck,key,{}});
  }
  for(unsigned i=0;h.Status().eligibleMask==0 && i<10000;++i){h.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(h.Status().state==TxState::Running && h.InstalledEpoch()==epoch && h.Status().eligibleMask!=0);
  CHECK(h.History().Records().size()==count && SamePosition(h.CurrentBoundary().checkpoint,before));
  CHECK(h.Status().clock.remainingMs==clock.remainingMs);
 }
 TxKey undo(DuelPlayer* p) {
  auto& h=*duel;const auto keep=*h.History().Target(p->type);const auto before=h.History().Records().at(keep).before;
  const auto oldEpoch=h.InstalledEpoch();h.ReceiveUndo(p,{WireKind::Request,{session,oldEpoch,h.Status().nextRequest,0,{}},{}});
  auto key=h.ActiveKey();if(h.Status().state==TxState::Consent)h.ReceiveUndo(p==&a?&b:&a,{WireKind::Consent,key,{1}});
  CHECK(h.Status().state==TxState::Preparing);h.ReceiveUndo(&a,{WireKind::Ready,key,{1}});h.ReceiveUndo(&b,{WireKind::Ready,key,{1}});
  for(unsigned i=0;h.Status().state==TxState::Preparing && i<10000;++i){h.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(h.Status().state==TxState::Committing);Bytes epoch(8);for(unsigned i=0;i<8;++i)epoch[i]=std::uint8_t((oldEpoch+1)>>(8*i));
  h.ReceiveUndo(&a,{WireKind::CommitAck,key,epoch});h.ReceiveUndo(&b,{WireKind::CommitAck,key,epoch});
  CHECK(h.Status().state==TxState::Running && h.InstalledEpoch()==oldEpoch+1);
  CHECK(h.History().Records().size()==keep);CHECK(SamePosition(h.CurrentBoundary().checkpoint,before));inputSequence={};return key;
 }
};
int main(int argc,char** argv){try {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);CHECK(argc==2 || argc==3);
 const std::string mode=argc==3?argv[2]:"series";
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
 dataManager.IrrFileSystem=files.get();CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
 auto resources=ResourceView::Capture(argv[1]);
 if(mode=="series")for(bool straight:{false,true}) {
  Room r(resources);r.endTurn();r.endTurn();r.duel->Surrender(&r.a);r.checkEnded(false,1);
  r.next(&r.a,true);r.duel->Surrender(straight?&r.a:&r.b);r.checkEnded(straight,2);
  if(!straight){r.next(&r.b);r.duel->Surrender(&r.a);r.checkEnded(true,3);}
  std::cout<<"PASS native Match surrender score, side validation, seats and "<<(straight?"2-0":"2-1")<<" completion\n";
 }
 if(mode=="epochs")for(auto mode:{RoomMode::ConsentLan,RoomMode::LoopbackFree}) {
  Room r(resources,mode);r.endTurn();r.endTurn();auto old=r.token();auto key=r.undo(&r.a);auto committed=r.token();
  r.duel->Surrender(&r.a);r.checkEnded(false,1);r.next(&r.a);
  CHECK(r.duel->InstalledEpoch()==2);auto before=r.duel->CurrentBoundary().checkpoint;
  for(auto stale:{old,committed}) {
   r.duel->ReceiveUndo(r.actor(),EncodeResponse(stale,Origin::Manual,integer(7)));
   r.duel->ReceiveUndo(&r.a,{WireKind::Request,{r.session,stale.epoch,1,0,{}},{}});
   for(const auto& e:EncodeGamePacket(r.session,stale.epoch,r.duel->Status().prompt,1,{CTOS_SURRENDER}))r.duel->ReceiveUndo(&r.a,e);
  }
  r.duel->ReceiveUndo(&r.a,{WireKind::Ready,key,{1}});r.duel->ReceiveUndo(&r.b,{WireKind::AbortAck,key,{}});
  CHECK(r.duel->Status().state==TxState::Running && r.duel->History().Records().empty());
  CHECK(SamePosition(before,r.duel->CurrentBoundary().checkpoint));
  r.endTurn();r.endTurn();if(mode==RoomMode::ConsentLan)r.cancelUndo(&r.a,true);r.cancelUndo(&r.a,false);
  r.undo(&r.a);CHECK(r.duel->InstalledEpoch()==3);r.endTurn();
  std::cout<<"PASS Match round epoch fences old response, request, surrender and transaction; new round undo works\n";
 }
 if(mode=="terminal")for(bool timeout:{false,true}) {
  Room r(resources);DuelPlayer* loser=nullptr;
  if(timeout){loser=r.actor();r.confirm();for(unsigned i=0;i<180;++i)r.duel->TimerTick();}
  else {for(unsigned i=0;r.duel->HasActiveDuel();++i){CHECK(i<20);r.endTurn();}
   const auto& p=r.packets[0];auto win=std::find_if(p.rbegin(),p.rend(),[](const auto& b){return b.size()==4 && b[0]==STOC_GAME_MSG && b[1]==MSG_WIN;});
   CHECK(win!=p.rend() && (*win)[2]<2);loser=(*win)[2]==0?&r.a:&r.b; // Round one engine seats are swapped.
  }
  r.checkEnded(false,1);r.next(loser);r.duel->Surrender(loser);r.checkEnded(true,2);
  std::cout<<"PASS native Match "<<(timeout?"timer":"core deck-out")<<" result progresses through next game\n";
 }
 if(mode=="draws") {
  // Exercise the existing host decoder's draw score/turn-choice behavior. Real
  // core terminal/replay production is covered separately by deck-out above.
  Room r(resources);for(unsigned game=1;game<=3;++game){
   Bytes draw{MSG_WIN,2,1};CHECK(r.duel->Analyze(draw.data(),draw.size())==2);r.duel->DuelEndProc();
   CHECK(!r.duel->HasActiveDuel() && r.duel->History().Records().empty());
   CHECK(r.duel->duel_stage==(game==3?DUEL_STAGE_END:DUEL_STAGE_SIDING));
   if(game<3)r.next(game==1?&r.b:&r.a);
  }
  for(unsigned p=0;p<2;++p){CHECK(r.packetCount(STOC_CHANGE_SIDE,p)==2 && r.packetCount(STOC_DUEL_END,p)==1);
   CHECK(std::count(r.packets[p].begin(),r.packets[p].end(),Bytes{STOC_GAME_MSG,MSG_WIN,2,1})==3);
  }
  std::cout<<"PASS native draw decoder, alternating turn choice and final drawn Match\n";
 }
 if(mode=="bot-match") {
  auto config=std::make_shared<RoomConfig>();config->resources=resources;config->bot=BotLaunchData{};
  bool rejected=false;try{UndoDuel h(true,config,NewSessionId());}
  catch(const std::exception& e){rejected=std::string(e.what()).find("Match")!=std::string::npos;}
  CHECK(rejected);std::cout<<"PASS bot Match rejects explicitly before private process initialization\n";
 }
 if(mode=="round-failure") {
  Room r(resources);r.duel->Surrender(&r.a);r.checkEnded(false,1);
  auto d=deck();r.duel->UpdateDeck(&r.a,d.data(),d.size());r.duel->UpdateDeck(&r.b,d.data(),d.size());
  r.failRoundRecipient=1;r.duel->TPResult(&r.a,1);
  CHECK(r.clientEpoch[0]==1 && r.clientEpoch[1]==0);
  CHECK(r.duel->Status().state==TxState::PausedFailed && !r.duel->HasActiveDuel());
  CHECK(r.failedStatus[0] && r.failedStatus[1]);
  const auto count=r.duel->History().Records().size();r.duel->ReceiveUndo(&r.a,EncodeResponse(r.token(),Origin::Manual,integer(7)));
  CHECK(r.duel->History().Records().size()==count && r.duel->Status().state==TxState::PausedFailed);
  std::cout<<"PASS partial RoundStart delivery pauses both client epochs and never starts the next game\n";
 }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
