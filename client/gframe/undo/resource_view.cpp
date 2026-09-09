#include "resource_view.h"
#include "../data_manager.h"
#include "../file_system.h"
#include <IFileSystem.h>
#include <IFileArchive.h>
#include <IFileList.h>
#include <IReadFile.h>
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
namespace undo {
namespace {
namespace fs = std::filesystem;
// Match the fixed Irrlicht stringc::make_lower rule, without depending on the
// process locale. Card/framework logical names are ASCII; UTF-8 bytes outside
// ASCII are preserved, as they are by the archive reader.
std::string caseKey(std::string s) {
 for(char& c:s) if(c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
 return s;
}
std::string logical(std::string s) {
 std::replace(s.begin(),s.end(),'\\','/');
 while(s.rfind("./",0)==0) s.erase(0,2);
 fs::path p=fs::u8path(s);
 if(p.is_absolute() || s.find(':')!=std::string::npos) throw std::runtime_error("Invalid logical resource path: " + s);
 for(const auto& part:p) if(part=="..") throw std::runtime_error("Invalid logical resource path: " + s);
 return caseKey(p.lexically_normal().generic_u8string());
}
std::optional<Bytes> loose(const fs::path& p) {
 std::ifstream f(p,std::ios::binary); if(!f) return std::nullopt;
 Bytes b((std::istreambuf_iterator<char>(f)),{});
 if(f.bad()) throw std::runtime_error("Resource read failed: "+p.u8string());
 if(b.size()>=0x100000) throw std::runtime_error("Script exceeds core reader limit: "+p.u8string());
 return b;
}
Bytes read(irr::io::IReadFile* f) {
 if(!f) throw std::runtime_error("Archive resource read failed");
 auto size=f->getSize();
 if(size<0 || size>=0x100000) { f->drop(); throw std::runtime_error("Archive script exceeds core reader limit"); }
 Bytes bytes(size); auto n=f->read(bytes.data(),size); f->drop();
 if(n!=size) throw std::runtime_error("Archive resource truncated"); return bytes;
}
void word(Bytes& b,uint64_t n,unsigned width=8) { for(unsigned i=0;i<width;++i)b.push_back(static_cast<uint8_t>(n>>(8*i))); }
void blob(Bytes& b,const Bytes& value) { word(b,value.size()); b.insert(b.end(),value.begin(),value.end()); }
void name(Bytes& b,const std::string& value) { blob(b,Bytes(value.begin(),value.end())); }
bool extension(const std::string& n,const char* ext) { auto e=fs::u8path(n).extension().u8string(); std::transform(e.begin(),e.end(),e.begin(),[](unsigned char c){return std::tolower(c);}); return e==ext; }
}
Digest Sha256(const Bytes& bytes) {
 BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; Digest digest{};
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider failed");
 NTSTATUS status=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
 if(status>=0) {
  for(size_t at=0;at<bytes.size() && status>=0;) { auto n=static_cast<ULONG>(std::min<size_t>(bytes.size()-at,0x40000000)); status=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()+at),n,0); at+=n; }
  if(status>=0) status=BCryptFinishHash(hash,digest.data(),digest.size(),0);
  BCryptDestroyHash(hash);
 }
 BCryptCloseAlgorithmProvider(alg,0); if(status<0) throw std::runtime_error("SHA256 failed"); return digest;
}
std::optional<Bytes> ResourceView::Resolve(const std::string& root,irr::io::IFileSystem* files,bool prefer,const std::string& input) {
 auto n=logical(input); auto base=fs::u8path(root);
 // Matches ScriptReaderEx: only script/ gets expansion and archive resolution.
 if(n.rfind("script",0)!=0) return loose(base/fs::u8path(n));
 if(prefer) if(auto b=loose(base/"expansions"/fs::u8path(n))) return b;
 for(irr::u32 i=0;files && i<files->getFileArchiveCount();++i)
  if(auto* f=files->getFileArchive(i)->createAndOpenFile(n.c_str())) return read(f);
 if(auto b=loose(base/fs::u8path(n))) return b;
 if(!prefer) if(auto b=loose(base/"expansions"/fs::u8path(n))) return b;
 return std::nullopt;
}
std::shared_ptr<const ResourceView> ResourceView::Capture(const std::string& root) {
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){if(p)p->drop();});
 if(!files) throw std::runtime_error("Irrlicht filesystem creation failed");
 ygo::DataManager data; data.IrrFileSystem=files.get();
 auto base=fs::absolute(fs::u8path(root));
 if(fs::exists(base/"cards.cdb") && !data.LoadDB((base/"cards.cdb").u8string().c_str())) throw std::runtime_error(data.errmsg);
 FileSystem::TraversalDir((base/"expansions").u8string().c_str(),[&](const char* n,bool dir){
  if(dir)return; auto path=(base/"expansions"/fs::u8path(n)).u8string();
  if(extension(n,".cdb")) { if(!data.LoadDB(path.c_str())) throw std::runtime_error(data.errmsg); }
  else if(extension(n,".zip") || extension(n,".ypk")) { if(!files->addFileArchive(path.c_str(),true,false,irr::io::EFAT_ZIP))throw std::runtime_error("Archive load failed: "+path); }
 });
 for(irr::u32 i=0;i<files->getFileArchiveCount();++i) {
  auto* list=files->getFileArchive(i)->getFileList();
  for(irr::u32 j=0;j<list->getFileCount();++j) {
   auto n=list->getFullFileName(j); if(extension(n.c_str(),".cdb") && !data.LoadDB(n.c_str()))throw std::runtime_error(data.errmsg);
  }
 }
 bool prefer=false;
 for(const char* config:{"system.conf","load-once.conf"}) {
  std::ifstream f(base/config); std::string line;
  while(std::getline(f,line)) { int v; if(std::sscanf(line.c_str(),"prefer_expansion_script = %d",&v)==1)prefer=v!=0; }
 }
 return Capture(root,data,prefer);
}
std::shared_ptr<const ResourceView> ResourceView::Capture(const std::string& root,const ygo::DataManager& data,bool prefer) {
 auto result=std::shared_ptr<ResourceView>(new ResourceView); std::set<std::string> names;
 const auto base=fs::absolute(fs::u8path(root)).lexically_normal(); auto* files=data.IrrFileSystem;
 result->source_root_=base.u8string();
 result->priority_.push_back(prefer ? "expansion,archives,loose" : "archives,loose,expansion");
 for(const char* folder:{"script","expansions/script","single"}) {
  auto path=base/folder; if(!fs::exists(path))continue;
  for(auto& entry:fs::recursive_directory_iterator(path)) if(entry.is_regular_file()) {
   auto n=logical(entry.path().lexically_relative(base).generic_u8string());
   if(n.rfind("expansions/script/",0)==0)n.erase(0,11);
   // Include every file, not just .lua: scripts can load arbitrary logical names.
   names.insert(n);
  }
 }
 for(irr::u32 i=0;files && i<files->getFileArchiveCount();++i) {
  auto* archive=files->getFileArchive(i); auto* list=archive->getFileList();
  result->priority_.push_back(caseKey(fs::u8path(archive->getArchiveName().c_str()).filename().u8string()));
  for(irr::u32 j=0;j<list->getFileCount();++j) if(!list->isDirectory(j)) {
   auto n=logical(list->getFullFileName(j).c_str()); if(n.rfind("script/",0)==0)names.insert(n);
  }
 }
 // Resolve each equivalence class once in loader order; enumeration order and
 // colliding source spellings must never select the winner.
 for(auto& n:names) { auto b=Resolve(root,files,prefer,n); if(!b)throw std::runtime_error("Resource capture failed: "+(base/fs::u8path(n)).lexically_normal().u8string()+"; expansion/archive source directory: "+(base/"expansions").u8string()); result->contents_.emplace(n,std::move(*b)); }
 for(auto& entry:data.GetDataTable()) { card_data cd{}; data.GetData(entry.first,&cd); result->cards_.emplace(entry.first,cd); }
 Bytes encoded; name(encoded,"ygopro-resources-v1");
 for(auto& p:result->priority_)name(encoded,p);
 word(encoded,result->contents_.size()); for(auto& entry:result->contents_) { name(encoded,entry.first); blob(encoded,entry.second); }
 word(encoded,result->cards_.size());
 for(auto& entry:result->cards_) {
  auto& c=entry.second; word(encoded,c.code,4); word(encoded,c.alias,4); for(auto v:c.setcode)word(encoded,v,2);
  for(auto v:{c.type,c.level,c.attribute,c.race,static_cast<uint32_t>(c.attack),static_cast<uint32_t>(c.defense),c.lscale,c.rscale,c.link_marker,c.rule_code})word(encoded,v,4);
 }
 result->digest_=Sha256(encoded); return result;
}
const Bytes& ResourceView::Read(const std::string& input) const {
 auto n=logical(input); auto it=contents_.find(n); if(it==contents_.end())throw std::runtime_error("Resource absent from frozen view (no disk lookup): "+(fs::u8path(source_root_)/fs::u8path(n)).lexically_normal().u8string()+"; captured expansion/archive source directory: "+(fs::u8path(source_root_)/"expansions").u8string()); return it->second;
}
const card_data& ResourceView::Card(uint32_t code) const {
 auto it=cards_.find(code); if(it==cards_.end())throw std::runtime_error("Pinned card missing: "+std::to_string(code)); return it->second;
}
}
