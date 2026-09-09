#include "test_support.h"
#include "netserver.h"
#include "game.h"
#include "mysocket.h"
#include "undo/room_admission.h"
#include "undo/room_wire.h"
#include <chrono>
#include <thread>
#include <iostream>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) {throw std::runtime_error("Unexpected GUI policy event");}
void Game::AddDebugMsg(const char*) {throw std::runtime_error("Unexpected GUI policy diagnostic");}
void DeckBuilder::RefreshPackListScroll() {throw std::runtime_error("Unexpected policy editor callback");}
}
using namespace ygo;
using namespace undo;
#include "tcp_peer.h"
static STOC_ErrorMsg policy(Peer& peer) {
    for(int i=0;i<30;++i) {
        auto packet=peer.Read();CHECK(!packet.empty());
        if(packet[0]!=STOC_ERROR_MSG)continue;
        CHECK(packet.size()==1+sizeof(STOC_ErrorMsg));
        STOC_ErrorMsg result{};std::memcpy(&result,packet.data()+1,sizeof result);
        CHECK(result.msg==0x7e);return result;
    }
    throw std::runtime_error("Missing explicit room policy notification");
}
static void stopped() {
    for(int i=0;i<1500 && NetServer::IsRunning();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(!NetServer::IsRunning());
}
int main(){try {
    WSADATA winsock;CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
    Hello cap;cap.engine[0]=1;cap.rules[0]=2;cap.resources[0]=3;
    unsigned short port{};CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&cap));
    CTOS_CreateGame create{};create.info.duel_rule=5;
    CTOS_JoinGame join{};join.version=PRO_VERSION;
    Peer host(port);ClientRoomHandshake hh(cap,true);host.Envelope(hh.Offer());
    host.SendStruct(CTOS_CREATE_GAME,create);
    auto echo=hh.Receive(host.NextHello());CHECK(echo);host.Envelope(*echo);hh.Receive(host.NextHello());CHECK(hh.Ready());
    Peer guest(port);ClientRoomHandshake gh(cap,true);guest.Envelope(gh.Offer());guest.SendStruct(CTOS_JOIN_GAME,join);
    echo=gh.Receive(guest.NextHello());CHECK(echo);guest.Envelope(*echo);gh.Receive(guest.NextHello());CHECK(gh.Ready());
    host.Send(CTOS_HS_TOOBSERVER);
    auto notice=policy(host);CHECK(notice.code==2); // nonfatal observer refusal
    // The refusal leaves both player slots occupied and the host socket usable.
    uint16_t message[3]={'o','k',0};
    host.Send(CTOS_CHAT,Bytes(reinterpret_cast<unsigned char*>(message),reinterpret_cast<unsigned char*>(message)+sizeof message));
    bool chat=false;for(int i=0;i<30 && !chat;++i){auto packet=host.Read();CHECK(!packet.empty());chat=packet[0]==STOC_CHAT;}
    CHECK(chat);
    for(int attempt=0;attempt<2;++attempt) {
        Peer third(port);ClientRoomHandshake th(cap,true);third.Envelope(th.Offer());third.SendStruct(CTOS_JOIN_GAME,join);
        auto reason=policy(third);CHECK(reason.code==(0x80000000u|3u));CHECK(third.Read().empty());
    }
    guest.Close();host.Close();stopped();
    for(const int mode:{2,1}) {
        CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&cap));
        Peer unsupported(port);ClientRoomHandshake handshake(cap,true);unsupported.Envelope(handshake.Offer());
        create.info.mode=mode;unsupported.SendStruct(CTOS_CREATE_GAME,create);
        auto reason=policy(unsupported);CHECK(reason.code==(0x80000000u|(mode==2?1u:7u)));CHECK(unsupported.Read().empty());
        unsupported.Close();NetServer::StopServer();stopped();
    }
    WSACleanup();
    std::cout<<"actual policy notice: nonfatal observer refusal, full slots and fatal Tag/Match reasons delivered before EOF"<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
