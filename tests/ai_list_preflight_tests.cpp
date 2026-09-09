#include "test_support.h"
#include "measurement_process.h"
#include "data_manager.h"
#include "file_system.h"
#include "network.h"
#include "undo/bot_controller.h"
#include <irrlicht.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <iomanip>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace undo;
namespace fs=std::filesystem;
namespace {
struct Entry { std::string name,executor,dialog; bool hidden{}; };
Bytes read(const fs::path& path) { std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+path.u8string());return Bytes(std::istreambuf_iterator<char>(f),{}); }
std::string hex(const Digest& d) {std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:d)s<<std::setw(2)<<unsigned(b);return s.str();}
struct Reader {
 const Bytes& bytes;std::size_t at{};
 std::uint32_t number(){CHECK(at+4<=bytes.size());std::uint32_t n{};for(unsigned i=0;i<4;++i)n|=std::uint32_t(bytes[at++])<<(8*i);return n;}
 std::string text(){auto n=number();CHECK(n<=4096 && n<=bytes.size()-at);std::string s(bytes.begin()+at,bytes.begin()+at+n);at+=n;return s;}
};
std::string csv(const std::string& s) {std::string out="\"";for(char c:s){if(c=='"')out+='"';out+=c;}return out+'"';}
void require(bool ok,const std::string& what){if(!ok)throw std::runtime_error(what);}
bool extension(const char* name,const char* suffix) {return fs::u8path(name).extension().u8string()==suffix;}
std::shared_ptr<const ResourceView> capture(const fs::path& root,ygo::DataManager& data,irr::io::IFileSystem* files) {
 require(data.LoadDB((root/"cards.cdb").u8string().c_str()),"Cannot load base cards database");
 FileSystem::TraversalDir((root/"expansions").u8string().c_str(),[&](const char* n,bool directory){
  if(directory)return;auto path=(root/"expansions"/fs::u8path(n)).u8string();
  if(extension(n,".cdb"))require(data.LoadDB(path.c_str()),"Cannot load expansion "+path);
  else if(extension(n,".zip")||extension(n,".ypk"))require(files->addFileArchive(path.c_str(),true,false,irr::io::EFAT_ZIP),"Cannot load archive "+path);
 });
 for(irr::u32 i=0;i<files->getFileArchiveCount();++i){auto* list=files->getFileArchive(i)->getFileList();for(irr::u32 j=0;j<list->getFileCount();++j){auto n=list->getFullFileName(j);if(extension(n.c_str(),".cdb"))require(data.LoadDB(n.c_str()),"Cannot load archived database");}}
 bool prefer=false;for(const char* config:{"system.conf","load-once.conf"}){std::ifstream f(root/config);std::string line;while(std::getline(f,line)){int value;if(std::sscanf(line.c_str(),"prefer_expansion_script = %d",&value)==1)prefer=value!=0;}}
 return ResourceView::Capture(root.u8string(),data,prefer);
}
std::vector<Bytes> packets(const std::vector<BotOutput>& outputs){std::vector<Bytes> out;for(const auto& output:outputs)out.push_back(output.packet);return out;}
std::uint32_t word(const Bytes& b,std::size_t at){CHECK(at+4<=b.size());return b[at]|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;}
void put(Bytes& b,std::uint32_t value,unsigned n=4){for(unsigned i=0;i<n;++i)b.push_back(std::uint8_t(value>>(8*i)));}
Bytes join(){ygo::STOC_JoinGame body{};body.info.duel_rule=5;body.info.start_lp=8000;body.info.start_hand=5;body.info.draw_count=1;Bytes b{STOC_JOIN_GAME};const auto* p=reinterpret_cast<const std::uint8_t*>(&body);b.insert(b.end(),p,p+sizeof(body));return b;}
Bytes only(const std::vector<BotOutput>& output,std::uint8_t opcode){Bytes result;for(const auto& o:output)if(o.packet.at(0)==opcode){CHECK(result.empty());result=o.packet;}CHECK(!result.empty());return result;}
void cleanup(DWORD baseline){require(measurement::CountDescendants()==baseline,"Private process descendants leaked");}
std::string exercise(const std::wstring& exe,const BotLaunchData& base,const Entry& entry,const Bytes* catalog=nullptr) {
 auto init=base;init.name=entry.name;init.executor=entry.executor;init.dialog=entry.dialog;
 if(catalog){init.name="W3 Random";init.executor="";init.selectionCommand="Random=W3_LIST";init.selectionCatalog=*catalog;}
 const auto session=NewSessionId();BotController bot(exe,init,session,0);
 require(bot.State()==BotState::Running,"Initialization failed: "+bot.Failure());
 const auto selected=bot.Selection();
 if(!catalog)require(selected.executor==entry.executor && selected.name==entry.name && selected.dialog==entry.dialog,"Named list entry silently resolved to another configuration");
 auto output=bot.Dispatch(session,0,1,join());require(bot.State()==BotState::Running,"Join callback failed: "+bot.Failure());
 const auto deck=only(output,CTOS_UPDATE_DECK);const auto main=word(deck,1),side=word(deck,5);
 require(main>0 && main<=75 && side<=15 && deck.size()==9+4*(main+side),"Actual deck callback returned malformed/empty deck");
 for(std::size_t at=9;at<deck.size();at+=4)require(word(deck,at)!=0,"Actual deck callback contains zero code");
 auto ready=only(bot.Dispatch(session,0,2,{STOC_TYPE_CHANGE,1}),CTOS_HS_READY);CHECK(ready.size()==1);
 auto hand=only(bot.Dispatch(session,0,3,{STOC_SELECT_HAND}),CTOS_HAND_RESULT);CHECK(hand.size()==5 && word(hand,1)>=1 && word(hand,1)<=3);
 auto tp=only(bot.Dispatch(session,0,4,{STOC_SELECT_TP}),CTOS_TP_RESULT);CHECK(tp.size()==5 && word(tp,1)<=1);
 const auto cursor=bot.Cursor();const auto original=bot.ActivePid();
 std::vector<std::vector<Bytes>> expected;
 for(unsigned i=0;i<8;++i)expected.push_back(packets(bot.Dispatch(session,0,5+i,{STOC_SELECT_HAND})));
 TxKey key{session,0,1,0,{}};key.targetDigest[0]=1;
 require(bot.Prepare(key,cursor),"Candidate replay failed: "+bot.Failure());
 const auto candidate=bot.CandidatePid();CHECK(candidate && candidate!=original);
 CHECK(bot.Commit(key) && bot.RetainedPid()==original && bot.Resume(key,1));
 CHECK(bot.ActivePid()==candidate && bot.RetainedPid()==0);
 for(unsigned i=0;i<8;++i)require(packets(bot.Dispatch(session,1,5+i,{STOC_SELECT_HAND}))==expected[i],"Candidate next real RPS output diverged");
 require(bot.State()==BotState::Running,"Candidate stopped after continuation");
 return selected.executor;
}
void stateful(const std::wstring& exe,const BotLaunchData& base,const Entry& row){
 auto init=base;init.name=row.name;init.executor=row.executor;init.dialog=row.dialog;
 auto session=NewSessionId();BotController bot(exe,init,session,0);
 bot.Dispatch(session,0,1,join());
 Bytes start{STOC_GAME_MSG,MSG_START,0,5};put(start,8000);put(start,8000);put(start,56,2);put(start,3,2);put(start,40,2);put(start,0,2);
 Bytes draw{STOC_GAME_MSG,MSG_DRAW,0,2};put(draw,98645731);put(draw,60990740);
 Bytes activate{STOC_GAME_MSG,MSG_SELECT_IDLECMD,0,0,0,0,0,0,1};put(activate,98645731);activate.insert(activate.end(),{0,2,0});put(activate,0);activate.insert(activate.end(),{0,1,0});
 Bytes summon{STOC_GAME_MSG,MSG_SELECT_IDLECMD,0,1};put(summon,60990740);summon.insert(summon.end(),{0,2,1,0,0,0,0,0,0,1,0});
 bot.Dispatch(session,0,2,start);bot.Dispatch(session,0,3,draw);
 CHECK(word(only(bot.Dispatch(session,0,4,activate),CTOS_RESPONSE),1)==5);
 auto cursor=bot.Cursor();auto expected=packets(bot.Dispatch(session,0,5,summon));
 CHECK(word(only(bot.Dispatch(session,0,6,summon),CTOS_RESPONSE),1)==7);
 TxKey key{session,0,1,0,{}};key.targetDigest[0]=2;
 require(bot.Prepare(key,cursor),"Stateful ChainBurn candidate failed: "+bot.Failure());
 CHECK(bot.Commit(key) && bot.Resume(key,1));
 CHECK(packets(bot.Dispatch(session,1,5,summon))==expected);
}
}
int main(int argc,char** argv){try{
 CHECK(argc==6);const auto exe=fs::absolute(fs::u8path(argv[1]));const auto runtime=fs::absolute(fs::u8path(argv[2]));
 const auto manifest=read(fs::u8path(argv[3]));Reader r{manifest};CHECK(r.number()==0x314c3357);
 Digest listDigest{};for(auto& byte:listDigest){CHECK(r.at<manifest.size());byte=manifest[r.at++];}
 CHECK(listDigest==Sha256(read(runtime/"bots.json")));const auto count=r.number();CHECK(count>0 && count<=256);
 std::vector<Entry> rows;std::set<std::string> distinct;
 for(unsigned i=0;i<count;++i){Entry row{r.text(),r.text(),r.text(),false};CHECK(r.at<manifest.size() && manifest[r.at]<=1);row.hidden=manifest[r.at++]!=0;CHECK(!row.name.empty() && !row.executor.empty() && !row.dialog.empty());distinct.insert(row.executor);rows.push_back(std::move(row));}CHECK(r.at==manifest.size());
 std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
 ygo::DataManager data;data.IrrFileSystem=files.get();auto resources=capture(runtime.parent_path(),data,files.get());
 BotLaunchData base;base.runtimeRoot=runtime.u8string();base.seed=31871;base.engine=Sha256(read(exe));base.resources=resources->Fingerprint();base.cardView=CaptureBotCardView(*resources,data,base.engine);
 std::ofstream result(fs::u8path(argv[4]),std::ios::binary);CHECK(result);result<<"botId,name,executor,dialog,hidden,preflight,fullMatch,detail\n";
 std::ofstream provenance(fs::u8path(argv[5]),std::ios::binary);CHECK(provenance);provenance<<"listSha256="<<hex(listDigest)<<"\nrows="<<count<<"\ndistinctExecutors="<<distinct.size()<<"\nbotExecutableSha256="<<hex(base.engine)<<"\nlogicalResources="<<hex(base.resources)<<"\ncardBridge="<<hex(Sha256(base.cardView))<<"\n";provenance.close();
 const auto baseline=measurement::CountDescendants();unsigned passed=0;
 for(unsigned i=0;i<count;++i){bool ok=false;std::string detail;
  try{exercise(exe.wstring(),base,rows[i]);cleanup(baseline);ok=true;++passed;detail="private initialize/lobby callbacks/candidate/next RPS";}catch(const std::exception& e){detail=e.what();try{cleanup(baseline);}catch(const std::exception& c){detail+="; "+std::string(c.what());}}
  const auto& row=rows[i];result<<i+1<<','<<csv(row.name)<<','<<csv(row.executor)<<','<<csv(row.dialog)<<','<<row.hidden<<','<<(ok?"pass":"fail")<<",pending,"<<csv(detail)<<'\n';result.flush();
  std::cout<<(ok?"PASS ":"FAIL ")<<i+1<<'/'<<count<<' '<<row.executor<<" : "<<detail<<std::endl;
 }
 bool extras=true;
 try{
  std::string catalog;for(const auto& row:rows)catalog+="!"+row.name+"\nDeck='"+row.executor+"' Name='"+row.name+"' Dialog='"+row.dialog+"'\nlist fixture\nW3_LIST\n";
  Bytes bytes(catalog.begin(),catalog.end());auto selected=exercise(exe.wstring(),base,rows.front(),&bytes);CHECK(distinct.count(selected));cleanup(baseline);std::cout<<"PASS representative once-selected list Random: "<<selected<<std::endl;
  auto found=std::find_if(rows.begin(),rows.end(),[](const Entry& e){return e.executor=="ChainBurn";});CHECK(found!=rows.end());stateful(exe.wstring(),base,*found);cleanup(baseline);std::cout<<"PASS representative real ChainBurn no_sp callback/candidate continuation (protocol fixture, not full match)"<<std::endl;
 }catch(const std::exception& e){extras=false;std::cerr<<"FAIL representative: "<<e.what()<<std::endl;}
 std::cout<<"LIST PREFLIGHT "<<passed<<'/'<<count<<" rows, "<<distinct.size()<<" distinct executors; full matches pending"<<std::endl;
 return passed==count && extras?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}