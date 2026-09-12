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
struct CurrentDirectory {
 std::filesystem::path previous{std::filesystem::current_path()};
 explicit CurrentDirectory(const std::string& root) {std::filesystem::current_path(std::filesystem::u8path(root));}
 ~CurrentDirectory() {std::filesystem::current_path(previous);}
};
static void join(const Hello& hostCapability,const Hello& guestCapability,bool compatible,
                 std::shared_ptr<const RoomConfig> hostConfig = {}) {
 static unsigned attempt=0;
 std::cerr<<"TCP resource case "<<++attempt<<" expects "<<(compatible?"join":"rejection")<<'\n';
 unsigned short port{};
 CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&hostCapability,std::move(hostConfig)));
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
  CHECK(host->resources->Fingerprint()!=host->capability.resources);
  // Exercise real host configuration admission, not merely a capability-only
  // listener. The authoritative rebuild digest deliberately differs from the
  // client card-table compatibility digest.
  join(host->capability,CaptureClientRoomConfig(guestData,mode)->capability,true,host);
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
 CHECK(host->resources->Fingerprint()!=guest->resources->Fingerprint());
 CHECK(Compatibility(host->capability,guest->capability).empty());
 join(host->capability,guest->capability,true);
 // Different hosts keep their own full, immutable effect-script snapshot.
 fixture::WriteFixtureFile(guestRoot+"/script/c900000001.lua",fixture::bytes("-- later edit"));
 CHECK(guest->resources->Read("script/c900000001.lua")==fixture::bytes("-- changed card script"));
 CHECK(host->resources->Read("script/c900000001.lua")==fixture::bytes("-- same card script"));
 fixture::WriteFixtureFile(guestRoot+"/script/c900000001.lua",fixture::bytes("-- same card script"));
 fixture::sql(guestRoot+"/cards.cdb","UPDATE datas SET atk=1900;");
 CHECK(guestData.LoadDB((guestRoot+"/cards.cdb").c_str()));
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 join(host->capability,guest->capability,false);
 fixture::database(guestRoot);CHECK(guestData.LoadDB((guestRoot+"/cards.cdb").c_str()));
 guest=CaptureRoomConfig(guestData,guestRoot,true,RoomMode::ConsentLan);
 join(host->capability,guest->capability,true);
 const auto originalLists=deckManager._lfList;
 LFList list{};list.hash=123;list.listName=L"scope test";list.content.emplace(900000001,1);
 deckManager._lfList.push_back(list);
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 deckManager._lfList=originalLists;
 join(host->capability,guest->capability,true);

 // A joining client requires only its already-loaded numeric card table. Even
 // without a file-system provider, and with missing or malformed script roots,
 // it must never freeze resources or read the executable/restriction lists.
 for(const auto* label:{"missing-scripts","script-is-file","oversized-script"}) {
  const auto path=root+"/"+label;
  fixture::database(path);
  if(std::string(label)=="script-is-file")fixture::WriteFixtureFile(path+"/script",fixture::bytes("not a directory"));
  if(std::string(label)=="oversized-script")fixture::WriteFixtureFile(path+"/script/large.lua",Bytes(0x100000,1));
  if(std::string(label)!="missing-scripts")CHECK(rejects([&]{CaptureRoomConfig(hostData,path,false,RoomMode::ConsentLan);}));
  {
   CurrentDirectory directory(path);
   guestData.IrrFileSystem=nullptr;
   guest=CaptureClientRoomConfig(guestData,RoomMode::ConsentLan);
   guestData.IrrFileSystem=guestFiles.get();
  }
  CHECK(!guest->resources && !guest->bot && guest->botExecutable.empty());
  CHECK(guest->capability.resources!=host->resources->Fingerprint());
  join(host->capability,guest->capability,true,host);
 }
 const auto numericRoot=root+"/numeric-data";
 // Cover native declaration fields, normalized aliases/rule codes, pendulum
 // scales/link markers, and other logical numeric columns; not database bytes.
 for(const auto* change:{"ot=1","category=8","alias=900000002","alias=100","setcode=1",
     "type=33","atk=1900","def=1500","level=5","level=1048580","level=16777220",
     "type=67108865,def=3","race=2","attribute=2"}) {
  fixture::database(numericRoot);fixture::sql(numericRoot+"/cards.cdb",std::string("UPDATE datas SET ")+change+";");
  DataManager changed;changed.IrrFileSystem=guestFiles.get();CHECK(changed.LoadDB((numericRoot+"/cards.cdb").c_str()));
  auto capability=CaptureClientRoomConfig(changed,RoomMode::ConsentLan)->capability;
  CHECK(Compatibility(host->capability,capability)=="logical card data");
 }
 fixture::database(numericRoot);fixture::sql(numericRoot+"/cards.cdb","UPDATE texts SET name='translated card',desc='translated effect',str1='translated choice'; VACUUM;");
 DataManager translated;translated.IrrFileSystem=guestFiles.get();CHECK(translated.LoadDB((numericRoot+"/cards.cdb").c_str()));
 guest=CaptureClientRoomConfig(translated,RoomMode::ConsentLan);
 join(host->capability,guest->capability,true);

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
 CHECK(host->resources->Fingerprint()!=guest->resources->Fingerprint());
 join(host->capability,guest->capability,true);
 guestFiles->moveFileArchive(1,-1);
 guest=CaptureRoomConfig(guestData,guestRoot,false,RoomMode::ConsentLan);
 CHECK(guest->resources->Read("script/choice.lua")==fixture::bytes("archive-a"));
 join(host->capability,guest->capability,true);
 WSACleanup();
 std::cout<<"normal LAN/local TCP joins allow differing scripts, preferences, archive priority, restriction lists and translations; clients read no scripts; numeric card data remains checked\n";
 } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';if(NetServer::IsRunning()){NetServer::StopServer();stopped();}return 1; }
}
