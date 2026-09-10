#include "core_fixture.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "game.h"
#include "netserver.h"
#include "mysocket.h"
#include "undo/room_config.h"
#include "undo/room_wire.h"
#include <IFileSystem.h>
#include <chrono>
#include <iostream>
#include <thread>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) { throw std::runtime_error("Unexpected resource-scope GUI event"); }
void Game::AddDebugMsg(const char*) { throw std::runtime_error("Unexpected resource-scope GUI diagnostic"); }
void DeckBuilder::RefreshPackListScroll() { throw std::runtime_error("Unexpected resource-scope editor callback"); }
}
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace ygo;
using namespace undo;
#include "tcp_peer.h"
template<class F> static bool rejects(F action) {
 try { action(); } catch(const std::exception&) { return true; }
 return false;
}
static void stopped() {
 for(int i=0;i<1500 && NetServer::IsRunning();++i)
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 CHECK(!NetServer::IsRunning());
}
static void join(const Hello& hostCapability,const Hello& guestCapability,bool compatible) {
 static unsigned attempt=0;
 std::cerr<<"TCP resource case "<<++attempt<<" expects "<<(compatible?"join":"rejection")<<'\n';
 unsigned short port{};
 CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&hostCapability));
 Peer host(port);ClientRoomHandshake hostHandshake(hostCapability,true);
 host.Envelope(hostHandshake.Offer());
 CTOS_CreateGame create{};create.info.duel_rule=5;
 host.SendStruct(CTOS_CREATE_GAME,create);
 auto answer=hostHandshake.Receive(host.NextHello());CHECK(answer);
 host.Envelope(*answer);hostHandshake.Receive(host.NextHello());CHECK(hostHandshake.Ready());
 Peer guest(port);ClientRoomHandshake guestHandshake(guestCapability,true);
 guest.Envelope(guestHandshake.Offer());
 if(compatible) {
  CTOS_JoinGame request{};request.version=PRO_VERSION;guest.SendStruct(CTOS_JOIN_GAME,request);
  answer=guestHandshake.Receive(guest.NextHello());CHECK(answer);
  guest.Envelope(*answer);guestHandshake.Receive(guest.NextHello());
  CHECK(guestHandshake.Ready());CHECK(guestHandshake.Session()==hostHandshake.Session());
 } else {
  // An incompatible capability is rejected before any room request. Do not
  // add an unread join packet, which permits a TCP reset instead of EOF.
  try { guest.Rejected(); }
  catch(const std::exception& error) { throw std::runtime_error(std::string("TCP rejection: ")+error.what()+"; WSA="+std::to_string(WSAGetLastError())); }
 }
 guest.Close();host.Close();stopped();
}
int main() { try {
 SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
 const std::string root=UNDO_SCOPE_FIXTURE;
 const auto hostRoot=root+"/host",guestRoot=root+"/guest";
 for(const auto& path:{hostRoot,guestRoot}) {
  fixture::database(path);
  std::filesystem::remove(std::filesystem::u8path(path+"/single/oversized.bin"));
  fixture::WriteFixtureFile(path+"/script/c900000001.lua",fixture::bytes("-- same card script"));
 }
 fixture::WriteFixtureFile(hostRoot+"/single/practice.lua",fixture::bytes("-- host practice"));
 fixture::WriteFixtureFile(guestRoot+"/single/practice.lua",fixture::bytes("-- guest practice"));
 DataManager hostData,guestData;
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(
  irr::io::createFileSystem(),[](auto* p){if(p)p->drop();});
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> guestFiles(
  irr::io::createFileSystem(),[](auto* p){if(p)p->drop();});
 CHECK(files && guestFiles);
 hostData.IrrFileSystem=files.get();guestData.IrrFileSystem=guestFiles.get();
 CHECK(hostData.LoadDB((hostRoot+"/cards.cdb").c_str()));
 CHECK(guestData.LoadDB((guestRoot+"/cards.cdb").c_str()));
 WSADATA winsock{};CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
 for(auto mode:{RoomMode::ConsentLan,RoomMode::LoopbackFree}) {
  auto host=CaptureRoomConfig(hostData,hostRoot,false,mode);
  auto guest=CaptureRoomConfig(guestData,guestRoot,false,mode);
  // Exercise the real TCP challenge/confirmation: differing practice content
  // must not prevent two otherwise identical normal duel clients from joining.
  CHECK(Compatibility(host->capability,guest->capability).empty());
  join(host->capability,guest->capability,true);
  CHECK(rejects([&]{host->resources->Read("single/practice.lua");}));
  CHECK(host->resources->Read("script/c900000001.lua")==fixture::bytes("-- same card script"));
 }
 auto host=CaptureRoomConfig(hostData,hostRoot,false,RoomMode::ConsentLan);
 fixture::WriteFixtureFile(guestRoot+"/single/oversized.bin",Bytes(0x100000,1));
 auto guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 join(host->capability,guest->capability,true);
 CHECK(rejects([&]{guestData.CaptureResources(guestRoot,false);}));
 std::filesystem::remove(std::filesystem::u8path(guestRoot+"/single/oversized.bin"));
 const auto malformed=root+"/single-is-file";
 fixture::database(malformed);fixture::WriteFixtureFile(malformed+"/single",fixture::bytes("not a directory"));
 fixture::WriteFixtureFile(malformed+"/script/c900000001.lua",fixture::bytes("-- same card script"));
 auto ignored=CaptureRoomConfig(hostData,malformed,false,RoomMode::ConsentLan);
 join(host->capability,ignored->capability,true);
 CHECK(rejects([&]{hostData.CaptureResources(malformed,false);}));

 fixture::WriteFixtureFile(guestRoot+"/script/c900000001.lua",fixture::bytes("-- changed card script"));
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 join(host->capability,guest->capability,false);
 fixture::WriteFixtureFile(guestRoot+"/script/c900000001.lua",fixture::bytes("-- same card script"));
 fixture::sql(guestRoot+"/cards.cdb","UPDATE datas SET atk=1900;");
 CHECK(guestData.LoadDB((guestRoot+"/cards.cdb").c_str()));
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 join(host->capability,guest->capability,false);
 fixture::database(guestRoot);CHECK(guestData.LoadDB((guestRoot+"/cards.cdb").c_str()));
 guest=CaptureRoomConfig(guestData,guestRoot,true,RoomMode::ConsentLan);
 join(host->capability,guest->capability,false);
 const auto originalLists=deckManager._lfList;
 LFList list{};list.hash=123;list.listName=L"scope test";list.content.emplace(900000001,1);
 deckManager._lfList.push_back(list);
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 deckManager._lfList=originalLists;
 join(host->capability,guest->capability,false);

 for(const auto& path:{hostRoot,guestRoot}) {
  fixture::zip(path+"/a.zip","script/choice.lua",fixture::bytes("archive-a"));
  fixture::zip(path+"/b.zip","script/choice.lua",fixture::bytes("archive-b"));
 }
 for(const auto* name:{"a.zip","b.zip"})CHECK(files->addFileArchive((hostRoot+"/"+name).c_str(),true,false,irr::io::EFAT_ZIP));
 for(const auto* name:{"b.zip","a.zip"})CHECK(guestFiles->addFileArchive((guestRoot+"/"+name).c_str(),true,false,irr::io::EFAT_ZIP));
 host=CaptureRoomConfig(hostData,hostRoot,false,RoomMode::ConsentLan);
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 CHECK(host->resources->Read("script/choice.lua")==fixture::bytes("archive-a"));
 CHECK(guest->resources->Read("script/choice.lua")==fixture::bytes("archive-b"));
 join(host->capability,guest->capability,false);
 guestFiles->moveFileArchive(1,-1);
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 CHECK(guest->resources->Read("script/choice.lua")==fixture::bytes("archive-a"));
 join(host->capability,guest->capability,true);
 WSACleanup();
 std::cout<<"normal LAN/local TCP joins ignore practice resources and reject card, database, preference, archive priority and rule differences\n";
 } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';if(NetServer::IsRunning()){NetServer::StopServer();stopped();}return 1; }
}
