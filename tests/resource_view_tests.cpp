#include "core_fixture.h"
#include "data_manager.h"
#include <IFileSystem.h>
#include <IFileArchive.h>
#include <iostream>
#include <windows.h>
#include <winioctl.h>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace undo;
// Windows junctions permit read-only installed resources without copying them.
static void junction(const std::filesystem::path& link,const std::filesystem::path& target){
 if(std::filesystem::exists(link))return;
 std::filesystem::create_directories(link);
 auto full=std::filesystem::absolute(target).make_preferred().wstring();auto substitute=L"\\??\\"+full;
 auto handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);CHECK(handle!=INVALID_HANDLE_VALUE);
 Bytes buffer;fixture::word(buffer,IO_REPARSE_TAG_MOUNT_POINT);fixture::word(buffer,8+(substitute.size()+full.size()+2)*2,2);fixture::word(buffer,0,2);
 fixture::word(buffer,0,2);fixture::word(buffer,substitute.size()*2,2);fixture::word(buffer,(substitute.size()+1)*2,2);fixture::word(buffer,full.size()*2,2);
 for(auto c:substitute)fixture::word(buffer,c,2);fixture::word(buffer,0,2);for(auto c:full)fixture::word(buffer,c,2);fixture::word(buffer,0,2);
 DWORD returned{};auto ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr,0,&returned,nullptr);CloseHandle(handle);CHECK(ok);
}
int main() {
 try {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const std::string root=UNDO_RESOURCE_FIXTURE;
  fixture::database(root);
  std::filesystem::remove(std::filesystem::u8path(root+"/script/created-after-capture.lua"));
  fixture::WriteFixtureFile(root+"/script/c900000001.lua",{1,2,3});
  fixture::WriteFixtureFile(root+"/single/dependencies/helper.dat",fixture::bytes("practice dependency"));
  auto view=ResourceView::Capture(root);auto original=view->Read("script/c900000001.lua");
  auto practice=ResourceView::Capture(root,ResourceScope::Practice);
  CHECK(practice->Fingerprint()==view->Fingerprint());
  CHECK(practice->Read("single/dependencies/helper.dat")==fixture::bytes("practice dependency"));
  fixture::WriteFixtureFile(root+"/single/dependencies/helper.dat",fixture::bytes("changed practice dependency"));
  CHECK(practice->Read("single/dependencies/helper.dat")==fixture::bytes("practice dependency"));
  CHECK(practice->Fingerprint()!=ResourceView::Capture(root,ResourceScope::Practice)->Fingerprint());
  fixture::WriteFixtureFile(root+"/single/dependencies/helper.dat",fixture::bytes("practice dependency"));
  fixture::WriteFixtureFile(root+"/pics/900000001.jpg",{9,8,7});
  CHECK(view->Fingerprint()==ResourceView::Capture(root)->Fingerprint());
  fixture::sql(root+"/cards.cdb","PRAGMA user_version=42; VACUUM;");
  CHECK(view->Fingerprint()==ResourceView::Capture(root)->Fingerprint());
  CHECK(view->Cards().size()==1); CHECK(view->Card(900000001).attack==1800);CHECK(view->Card(900000001).setcode[0]==0x1234);
  fixture::sql(root+"/cards.cdb","UPDATE datas SET atk=1900;");
  CHECK(view->Fingerprint()!=ResourceView::Capture(root)->Fingerprint());CHECK(view->Card(900000001).attack==1800);
  fixture::WriteFixtureFile(root+"/script/c900000001.lua",{9,9,9});
  CHECK(view->Read("script/c900000001.lua")==original);
  CHECK(view->Fingerprint()!=ResourceView::Capture(root)->Fingerprint());
  fixture::WriteFixtureFile(root+"/script/created-after-capture.lua",{4,5});
  bool missing=false;try {view->Read("script/created-after-capture.lua");}catch(const std::exception& e){missing=std::string(e.what()).find(std::filesystem::absolute(std::filesystem::u8path(root+"/script/created-after-capture.lua")).lexically_normal().u8string())!=std::string::npos;}CHECK(missing);
  // Clean only this known test-created file, so repeated runs keep the same precondition.
  std::filesystem::remove(std::filesystem::u8path(root+"/script/created-after-capture.lua"));
  const std::string archives=root+"-archives";
  fixture::database(archives);
  fixture::WriteFixtureFile(archives+"/script/choice.lua",fixture::bytes("loose"));
  fixture::WriteFixtureFile(archives+"/expansions/script/choice.lua",fixture::bytes("expansion"));
  fixture::zip(archives+"/a.zip","script/choice.lua",fixture::bytes("archive-a"));
  fixture::zip(archives+"/b.zip","script/choice.lua",fixture::bytes("archive-b"));
  std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
  CHECK(files->addFileArchive((archives+"/b.zip").c_str(),true,false,irr::io::EFAT_ZIP));CHECK(files->addFileArchive((archives+"/a.zip").c_str(),true,false,irr::io::EFAT_ZIP));
  ygo::DataManager manager;manager.IrrFileSystem=files.get();CHECK(manager.LoadDB((archives+"/cards.cdb").c_str()));
  auto archiveView=manager.CaptureResources(archives,false);
  CHECK(archiveView->Read("script/choice.lua")==fixture::bytes("archive-b"));
  CHECK(manager.CaptureResources(archives,true)->Read("script/choice.lua")==fixture::bytes("expansion"));
  files->moveFileArchive(1,-1);
  CHECK(manager.CaptureResources(archives,false)->Read("script/choice.lua")==fixture::bytes("archive-a"));
  CHECK(archiveView->Read("script/choice.lua")==fixture::bytes("archive-b"));
  CHECK(archiveView->Fingerprint()!=manager.CaptureResources(archives,false)->Fingerprint());
  // Case-equivalent logical names must match Windows loose-file and Irrlicht
  // lookups, including canonical fingerprints and precedence deduplication.
  const std::string mixed=root+"-case-mixed", lower=root+"-case-lower";
  fixture::database(mixed);fixture::database(lower);
  fixture::WriteFixtureFile(mixed+"/Script/C900000002.LUA",fixture::bytes("case-loose"));
  fixture::WriteFixtureFile(lower+"/script/c900000002.lua",fixture::bytes("case-loose"));
  auto mixedView=ResourceView::Capture(mixed),lowerView=ResourceView::Capture(lower);
  for(const char* request:{"script/c900000002.lua","./script/C900000002.LUA",".\\SCRIPT\\c900000002.Lua"}) {
   auto resolved=ResourceView::Resolve(mixed,nullptr,false,request);CHECK(resolved);CHECK(*resolved==fixture::bytes("case-loose"));
   CHECK(mixedView->Read(request)==*resolved);
  }
  CHECK(mixedView->ScriptCount()==1);CHECK(mixedView->Fingerprint()==lowerView->Fingerprint());
  fixture::WriteFixtureFile(mixed+"/Expansions/Script/c900000002.Lua",fixture::bytes("case-expansion"));
  fixture::WriteFixtureFile(lower+"/expansions/script/c900000002.lua",fixture::bytes("case-expansion"));
  fixture::zip(mixed+"/FIRST.ZIP","Script/C900000002.LUA",fixture::bytes("case-first"));
  fixture::zip(mixed+"/SECOND.ZIP","script/c900000002.lua",fixture::bytes("case-second"));
  fixture::zip(lower+"/first.zip","script/c900000002.lua",fixture::bytes("case-first"));
  fixture::zip(lower+"/second.zip","script/c900000002.lua",fixture::bytes("case-second"));
  std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> mixedFiles(irr::io::createFileSystem(),[](auto* p){p->drop();});
  std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> lowerFiles(irr::io::createFileSystem(),[](auto* p){p->drop();});
  for(const char* name:{"first.zip","second.zip"}) {
   CHECK(mixedFiles->addFileArchive((mixed+"/"+name).c_str(),true,false,irr::io::EFAT_ZIP));
   CHECK(lowerFiles->addFileArchive((lower+"/"+name).c_str(),true,false,irr::io::EFAT_ZIP));
  }
  ygo::DataManager mixedData,lowerData;mixedData.IrrFileSystem=mixedFiles.get();lowerData.IrrFileSystem=lowerFiles.get();
  CHECK(mixedData.LoadDB((mixed+"/cards.cdb").c_str()));CHECK(lowerData.LoadDB((lower+"/cards.cdb").c_str()));
  for(bool prefer:{false,true}) {
   auto pinned=mixedData.CaptureResources(mixed,prefer);auto equivalent=lowerData.CaptureResources(lower,prefer);
   CHECK(pinned->ScriptCount()==1);CHECK(pinned->Fingerprint()==equivalent->Fingerprint());
   for(const char* request:{"script/c900000002.lua","./script/C900000002.LUA",".\\SCRIPT\\c900000002.Lua"}) {
    auto resolved=ResourceView::Resolve(mixed,mixedFiles.get(),prefer,request);CHECK(resolved);
    CHECK(*resolved==fixture::bytes(prefer?"case-expansion":"case-first"));CHECK(pinned->Read(request)==*resolved);
   }
  }
  mixedFiles->moveFileArchive(1,-1);
  auto reordered=mixedData.CaptureResources(mixed,false);
  CHECK(reordered->ScriptCount()==1);CHECK(reordered->Read("SCRIPT/C900000002.LUA")==fixture::bytes("case-second"));
  CHECK(mixedData.CaptureResources(mixed,true)->Read("SCRIPT/C900000002.LUA")==fixture::bytes("case-expansion"));
  const std::string linked=root+"-junction";fixture::database(linked);
  junction(std::filesystem::u8path(linked+"/script"),std::filesystem::u8path(lower+"/script"));
  junction(std::filesystem::u8path(linked+"/expansions"),std::filesystem::u8path(lower+"/expansions"));
  for(bool prefer:{false,true}){
   auto direct=lowerData.CaptureResources(lower,prefer);auto alias=lowerData.CaptureResources(linked,prefer);
   CHECK(alias->Fingerprint()==direct->Fingerprint());CHECK(alias->Read("script/c900000002.lua")==direct->Read("script/c900000002.lua"));
  }
  auto hash=Sha256(fixture::bytes("abc")); const Digest expected={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};CHECK(hash==expected);
  std::cout << "resource pinning, normalized database, SHA256, archive priority passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
