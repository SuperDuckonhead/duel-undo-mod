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
// The lobby must never invoke an engine/UI diagnostic sink.
void Game::AddDebugMsg(const char*) { throw std::runtime_error("Unexpected GUI diagnostic in lobby test"); }
void DeckBuilder::RefreshPackListScroll() { throw std::runtime_error("Unexpected editor pack refresh in lobby test"); }
}
using namespace ygo;
using undo::Bytes;
struct Peer {
 Socket socketValue{INVALID_SOCKET};
 explicit Peer(unsigned short port) {
  socketValue=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(socketValue!=INVALID_SOCKET);
  DWORD timeout=2000;setsockopt(socketValue,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(0x7f000001);
  CHECK(connect(socketValue,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
 }
 ~Peer(){Close();}
 void Close(){if(socketValue!=INVALID_SOCKET){closesocket(socketValue);socketValue=INVALID_SOCKET;}}
 void Send(unsigned char proto,const Bytes& body={}) {
  Bytes bytes{static_cast<unsigned char>((body.size()+1)&255),static_cast<unsigned char>((body.size()+1)>>8),proto};
  bytes.insert(bytes.end(),body.begin(),body.end());
  std::size_t sent=0;while(sent<bytes.size()){int n=send(socketValue,reinterpret_cast<const char*>(bytes.data()+sent),bytes.size()-sent,0);CHECK(n>0);sent+=n;}
 }
 template<class T> void SendStruct(unsigned char proto,const T& data) {
  auto p=reinterpret_cast<const unsigned char*>(&data);Send(proto,Bytes(p,p+sizeof data));
 }
 void Envelope(const undo::Envelope& e){Send(undo::RoomOuterOpcode,undo::Encode(e));}
 Bytes Read() {
  unsigned char prefix[2];if(!ReadExact(prefix,2))return {};
  unsigned size=prefix[0]|unsigned(prefix[1])<<8;CHECK(size);
  Bytes packet(size);CHECK(ReadExact(packet.data(),packet.size()));return packet;
 }
 bool ReadExact(unsigned char* bytes,std::size_t size) {
  for(std::size_t have=0;have<size;){int got=recv(socketValue,reinterpret_cast<char*>(bytes+have),size-have,0);if(!got)return false;CHECK(got>0);have+=got;}return true;
 }
 undo::Envelope NextHello() {
  for(int i=0;i<20;++i){auto packet=Read();CHECK(!packet.empty());if(packet[0]==undo::RoomOuterOpcode)return undo::Decode(Bytes(packet.begin()+1,packet.end()));}
  throw std::runtime_error("Missing server capability response");
 }
 void Rejected() {
  for(int i=0;i<20;++i){auto packet=Read();if(packet.empty())return;CHECK(packet[0]!=STOC_JOIN_GAME);}
  throw std::runtime_error("Legacy/incompatible peer was not rejected");
 }
};
static void stopped() {
 for(int i=0;i<1000&&NetServer::IsRunning();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 CHECK(!NetServer::IsRunning());
}
int main(){try {
 WSADATA winsock;CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
 undo::Hello capability;capability.engine[0]=1;capability.rules[0]=2;capability.resources[0]=3;
 auto freeCapability=capability;freeCapability.mode=undo::RoomMode::LoopbackFree;
 unsigned short port{};
 CHECK(!NetServer::StartServer(0,0,&port,false,&freeCapability)); // real INADDR_ANY bind cannot grant free undo
 CHECK(!NetServer::IsRunning());
 CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&freeCapability));CHECK(port && NetServer::IsRunning());
 Peer host(port);undo::ClientRoomHandshake hostHandshake(capability,true);
 host.Envelope(hostHandshake.Offer());
 CTOS_PlayerInfo hostName{};BufferIO::CopyCharArray(L"Host",hostName.name);host.SendStruct(CTOS_PLAYER_INFO,hostName);
 CTOS_CreateGame create{};create.info.duel_rule=5;create.info.no_check_deck=1;create.info.no_shuffle_deck=1;
 host.SendStruct(CTOS_CREATE_GAME,create);
 auto challenge=host.NextHello();auto answer=hostHandshake.Receive(challenge);CHECK(answer);
 host.Envelope(*answer);hostHandshake.Receive(host.NextHello());CHECK(hostHandshake.Ready());
 {
  Peer legacy(port);CTOS_JoinGame join{};join.version=PRO_VERSION;legacy.SendStruct(CTOS_JOIN_GAME,join);legacy.Rejected();
 }
 {
  Peer incompatible(port);auto wrong=capability;wrong.resources[1]=7;
  undo::ClientRoomHandshake mismatch(wrong,true);incompatible.Envelope(mismatch.Offer());
  // Two buffered frames exercise safe exit after an immediate rejection/disconnect.
  CTOS_JoinGame join{};join.version=PRO_VERSION;incompatible.SendStruct(CTOS_JOIN_GAME,join);incompatible.Rejected();
 }
 Peer friendPeer(port);undo::ClientRoomHandshake friendHandshake(capability,true);
 friendPeer.Envelope(friendHandshake.Offer());CTOS_PlayerInfo friendName{};BufferIO::CopyCharArray(L"Friend",friendName.name);friendPeer.SendStruct(CTOS_PLAYER_INFO,friendName);
 CTOS_JoinGame join{};join.version=PRO_VERSION;friendPeer.SendStruct(CTOS_JOIN_GAME,join);
 auto echo=friendHandshake.Receive(friendPeer.NextHello());CHECK(echo);friendPeer.Envelope(*echo);friendHandshake.Receive(friendPeer.NextHello());CHECK(friendHandshake.Ready());
 CHECK(friendHandshake.Session()==hostHandshake.Session());
 {
  Peer third(port);undo::ClientRoomHandshake thirdHandshake(capability,true);third.Envelope(thirdHandshake.Offer());third.SendStruct(CTOS_JOIN_GAME,join);third.Rejected();
 }
 friendPeer.Close();host.Close();stopped();
 // Optional capability parameter leaves the ordinary baseline room path usable.
 CHECK(NetServer::StartServer(0,0x7f000001,&port,false));
 Peer ordinary(port);ordinary.SendStruct(CTOS_PLAYER_INFO,hostName);ordinary.SendStruct(CTOS_CREATE_GAME,create);
 bool joined=false;for(int i=0;i<10&&!joined;++i){auto packet=ordinary.Read();CHECK(!packet.empty());joined=packet[0]==STOC_JOIN_GAME;}
 CHECK(joined);ordinary.Close();stopped();WSACleanup();
 std::cout<<"actual TCP capability challenge/ack, old/resource-mismatched peer rejection, loopback bind and two-seat limit passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
