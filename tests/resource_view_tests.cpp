#include "core_fixture.h"
#include "data_manager.h"
#include <IFileSystem.h>
#include <IFileArchive.h>
#include <iostream>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace undo;
int main() {
 try {
  const std::string root=UNDO_RESOURCE_FIXTURE;
  fixture::database(root);
  fixture::WriteFixtureFile(root+"/script/c900000001.lua",{1,2,3});
  auto view=ResourceView::Capture(root);auto original=view->Read("script/c900000001.lua");
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
  bool missing=false;try {view->Read("script/created-after-capture.lua");}catch(const std::exception& e){missing=std::string(e.what())=="Pinned resource missing: script/created-after-capture.lua";}CHECK(missing);
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
  auto hash=Sha256(fixture::bytes("abc")); const Digest expected={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};CHECK(hash==expected);
  std::cout << "resource pinning, normalized database, SHA256, archive priority passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}