#include "test_support.h"
#include "core_fixture.h"
#include "data_manager.h"
#include "undo/bot_controller.h"
#include <IFileSystem.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace undo;
namespace {
Bytes start(){Bytes b{1,4,0,5};fixture::word(b,8000);fixture::word(b,8000);fixture::word(b,56,2);fixture::word(b,3,2);fixture::word(b,40,2);fixture::word(b,0,2);return b;}
Bytes draw(){Bytes b{1,90,0,2};fixture::word(b,98645731);fixture::word(b,60990740);return b;}
Bytes activate(){Bytes b{1,11,0,0,0,0,0,0,1};fixture::word(b,98645731);b.insert(b.end(),{0,2,0});fixture::word(b,0);b.insert(b.end(),{0,1,0});return b;}
Bytes summon(){Bytes b{1,11,0,1};fixture::word(b,60990740);b.insert(b.end(),{0,2,1,0,0,0,0,0,0,1,0});return b;}
std::uint32_t decision(const std::vector<BotOutput>& output){for(auto i=output.rbegin();i!=output.rend();++i)if(i->packet.size()==5 && i->packet[0]==1)return i->packet[1]|std::uint32_t(i->packet[2])<<8|std::uint32_t(i->packet[3])<<16|std::uint32_t(i->packet[4])<<24;throw std::runtime_error("No bot response");}
bool alive(DWORD pid){HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!process)return false;bool running=WaitForSingleObject(process,0)==WAIT_TIMEOUT;CloseHandle(process);return running;}
TxKey tx(SessionId session,std::uint64_t epoch=7,std::uint64_t request=1){TxKey k{};k.session=session;k.epoch=epoch;k.request=request;k.targetDigest[0]=55;return k;}
}
int main(int argc,char** argv){
 try {
  CHECK(argc==3);auto executable=std::filesystem::absolute(std::filesystem::u8path(argv[1])).wstring();const std::string runtime=argv[2];
  auto fixtureRoot=(std::filesystem::current_path()/"w2-bot-fixture").u8string();fixture::database(fixtureRoot);
  fixture::sql(fixtureRoot+"/cards.cdb","UPDATE datas SET atk=2468; UPDATE texts SET name='Frozen merged expansion',str16='sixteenth description';");
  std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
  ygo::DataManager manager;manager.IrrFileSystem=files.get();CHECK(manager.LoadDB((std::filesystem::u8path(runtime).parent_path()/"cards.cdb").u8string().c_str()));CHECK(manager.LoadDB((fixtureRoot+"/cards.cdb").c_str()));
  auto resources=ResourceView::Capture(fixtureRoot,manager,false);CHECK(resources->Card(900000001).attack==2468);
  BotLaunchData init;init.runtimeRoot=runtime;init.executor="ChainBurn";init.deckFile="AI_ChainBurn";init.dialog="kiwi.zh-TW";init.seed=31871;init.engine[0]=17;init.resources=resources->Fingerprint();init.cardView=CaptureBotCardView(*resources,manager,init.engine);
  auto frozen=init.cardView;
  // Change only the test expansion AFTER capture; both core and bot snapshots
  // must continue using the exact previously normalized view and text bytes.
  fixture::sql(fixtureRoot+"/cards.cdb","UPDATE datas SET atk=9999; UPDATE texts SET name='Changed after capture';");
  CHECK(resources->Card(900000001).attack==2468);CHECK(init.cardView==frozen);
  CHECK(manager.LoadDB((fixtureRoot+"/cards.cdb").c_str()));bool mismatch=false;try{CaptureBotCardView(*resources,manager,init.engine);}catch(const std::exception&){mismatch=true;}CHECK(mismatch);
  auto session=NewSessionId();std::uint32_t finalPid{};std::unique_ptr<void,decltype(&CloseHandle)> finalProcess(nullptr,&CloseHandle);
  {
   BotController bot(executable,init,session,7);CHECK(bot.State()==BotState::Running && bot.ActivePid()!=0);auto original=bot.ActivePid();CHECK(alive(original));
   bot.Dispatch(session,7,1,start());bot.Dispatch(session,7,2,draw());CHECK(decision(bot.Dispatch(session,7,3,activate()))==5);auto cursor=bot.Cursor();
   auto stale=bot.Dispatch(session,7,4,Bytes{3});CHECK(stale.size()==1);auto key=tx(session);
   CHECK(bot.Prepare(key,cursor));auto candidate=bot.CandidatePid();CHECK(candidate && candidate!=original && alive(candidate) && alive(original));CHECK(bot.State()==BotState::Ready);
   unsigned delivered=0;CHECK(!bot.Deliver(stale[0],4,[&](const Bytes&){++delivered;}));CHECK(bot.Dispatch(session,7,5,Bytes{3}).empty());
   CHECK(bot.Commit(key));CHECK(bot.Commit(key));CHECK(bot.CommitCount()==1 && bot.Epoch()==8 && bot.RetainedPid()==original && alive(original));CHECK(bot.State()==BotState::Committed);
   CHECK(bot.Dispatch(session,8,6,Bytes{3}).empty());CHECK(!bot.Deliver(stale[0],4,[&](const Bytes&){++delivered;}));CHECK(!bot.Resume(key,7));CHECK(bot.Resume(key,8));CHECK(!alive(original));
   CHECK(bot.ActivePid()==candidate && bot.RetainedPid()==0);auto output=bot.Dispatch(session,8,7,summon());CHECK(decision(output)==7);
   CHECK(!bot.Deliver(stale[0],4,[&](const Bytes&){++delivered;}));CHECK(!bot.Deliver(output.back(),8,[&](const Bytes&){++delivered;}));CHECK(bot.Deliver(output.back(),7,[&](const Bytes&){++delivered;}));CHECK(delivered==1);
   auto unbound=tx(session,8,2);unbound.targetDigest={};CHECK(!bot.Prepare(unbound,bot.Cursor()));CHECK(bot.State()==BotState::Running);
   auto bad=tx(session,8,3);CHECK(!bot.Prepare(bad,bot.Cursor()+1));CHECK(bot.State()==BotState::Frozen && bot.Epoch()==8 && bot.ActivePid()==candidate && alive(candidate));bot.Abort(bad);CHECK(bot.State()==BotState::Running);
   auto lostAck=tx(session,8,4);CHECK(bot.Prepare(lostAck,bot.Cursor()));CHECK(bot.Commit(lostAck));CHECK(bot.RetainedPid()==candidate && alive(candidate));finalPid=bot.ActivePid();bot.Pause();CHECK(bot.State()==BotState::Failed && !bot.Resume(lostAck,9));CHECK(alive(candidate) && alive(finalPid));
   // Capture the exact worker before destruction: reopening a PID afterward
   // can observe a reused identity and a zero-time wait assumes synchronous exit.
   finalProcess.reset(OpenProcess(SYNCHRONIZE,FALSE,finalPid));CHECK(finalProcess && WaitForSingleObject(finalProcess.get(),0)==WAIT_TIMEOUT);
  }
  // Kill-on-close starts termination of the job tree. The controller wait does
  // not guarantee that a different worker has finished its kernel teardown.
  const auto returned=std::chrono::steady_clock::now();
  const auto atReturn=WaitForSingleObject(finalProcess.get(),0);
  const auto exited=WaitForSingleObject(finalProcess.get(),5000);
  std::cout<<"Final worker "<<finalPid<<" destructor-return state="<<atReturn<<" exit state="<<exited
           <<" exit wait ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-returned).count()<<'\n';
  CHECK(exited==WAIT_OBJECT_0);
  // A real control child failure must become paused at the native last boundary.
  {
   BotController bot(executable,init,session,7);auto pid=bot.ActivePid();HANDLE process=OpenProcess(PROCESS_TERMINATE,FALSE,pid);CHECK(process);CHECK(TerminateProcess(process,5));CloseHandle(process);
   CHECK(bot.Dispatch(session,7,1,Bytes{3}).empty());CHECK(bot.State()==BotState::Failed);CHECK(!bot.Resume(tx(session),8));
  }
  auto invalid=init;invalid.cardView.back()^=1;bool rejected=false;try{BotController bot(executable,invalid,session,7);}catch(const std::exception&){rejected=true;}CHECK(rejected);
  std::cout<<"PASS native W2: actual ACL pipes/process jobs, real merged DataManager bridge, ChainBurn continuation, duplicate commit, retained old PID, Resume gating, stale output rejection, failed prepare/child/binding, RAII cleanup\n";
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}