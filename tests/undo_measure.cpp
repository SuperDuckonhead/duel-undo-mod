#include "test_support.h"
#include "undo/rebuilder.h"
#include "common.h"
#include "measurement_process.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace undo;
using Clock = std::chrono::steady_clock;
static std::string hex(const Digest& d) {
 std::ostringstream s; for(auto b:d) s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b); return s.str();
}
static Bytes integer(std::uint32_t x) { return {std::uint8_t(x),std::uint8_t(x>>8),std::uint8_t(x>>16),std::uint8_t(x>>24)}; }
struct Usage { std::uint64_t workingSet{},privateBytes{}; DWORD handles{},children{}; };
static Usage usage() {
 Usage u; PROCESS_MEMORY_COUNTERS_EX p{}; p.cb=sizeof(p);
 CHECK(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)));
 CHECK(GetProcessHandleCount(GetCurrentProcess(),&u.handles));
 u.workingSet=p.WorkingSetSize;u.privateBytes=p.PrivateUsage;
 u.children=measurement::CountDescendants();return u;
}
static double ms(Clock::time_point from) {return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
int main(int argc,char** argv) {
 try {
  CHECK(argc==5);const std::string root=argv[1];std::vector<std::size_t> cases;std::stringstream list(argv[2]);std::string value;
  while(std::getline(list,value,',')){std::size_t used{};auto n=std::stoul(value,&used);CHECK(used==value.size()&&n>0&&n<=10000);cases.push_back(n);}
  CHECK(!cases.empty());std::sort(cases.begin(),cases.end());CHECK(std::adjacent_find(cases.begin(),cases.end())==cases.end());
  std::size_t used{};auto repeats=std::stoul(argv[4],&used);CHECK(used==std::string(argv[4]).size()&&repeats>0&&repeats<=100);
  auto begin=Clock::now();auto resources=ResourceView::Capture(root);auto captureMs=ms(begin);
  InitialState initial;initial.seed.resize(SEED_COUNT,0x5eed1234);initial.resourceDigest=resources->Fingerprint();
  initial.noCheckDeck=true;initial.noShuffleDeck=true;initial.duelOptions=(5U<<16)|DUEL_PSEUDO_SHUFFLE;
  // A legal long practice duel: no opening hand and no automatic draws.
  // Forty installed normal monsters per side remain in deck. Every record is
  // actually accepted by the fixed engine; no synthetic response duplication.
  for(std::uint8_t p=0;p<2;++p) {
   initial.players[p]={8000,0,0};
   for(unsigned i=0;i<40;++i) initial.cards.push_back({i%2?46986414U:89631139U,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  }
  auto live=CoreDriver::Create(initial,resources);auto boundary=live->Advance();std::vector<ResponseRecord> records;
  Bytes inputs;begin=Clock::now();
  while(records.size()<=cases.back()) {
   CHECK(boundary.kind==BoundaryKind::AwaitResponse&&!boundary.rejectedResponse);
   auto before=boundary.checkpoint;Bytes response;Origin origin;
   if(before.prompt.at(0)==MSG_SELECT_IDLECMD){response=integer(7);origin=Origin::Manual;}
   else if(before.prompt.at(0)==MSG_SELECT_CHAIN){response=integer(0xffffffff);origin=Origin::Automatic;}
   else throw std::runtime_error("Unexpected long-history prompt "+std::to_string(before.prompt.at(0)));
   live->Submit(response);boundary=live->Advance();CHECK(!boundary.rejectedResponse&&boundary.kind==BoundaryKind::AwaitResponse);
   inputs.push_back(before.player);inputs.push_back(static_cast<std::uint8_t>(origin));inputs.insert(inputs.end(),response.begin(),response.end());
   records.push_back({before.player,origin,std::move(response),std::move(before)});
  }
  const auto recordMs=ms(begin);const auto original=live->Current();const auto originalTranscript=live->Transcript();
  std::ofstream out(std::filesystem::u8path(argv[3]),std::ios::binary|std::ios::trunc);CHECK(out);
  out<<std::setprecision(12);
  auto emit=[&](const char* kind,std::size_t count,std::size_t iteration,double elapsed,const Usage& u) {
   out<<"{\"kind\":\""<<kind<<"\",\"resourceDigest\":\""<<hex(resources->Fingerprint())<<"\",\"responses\":"<<count
      <<",\"undoCount\":"<<iteration<<",\"elapsedMs\":"<<elapsed<<",\"workingSetBytes\":"<<u.workingSet
      <<",\"privateBytes\":"<<u.privateBytes<<",\"handleCount\":"<<u.handles<<",\"descendantProcessCount\":"<<u.children<<"}\n";
   out.flush();CHECK(out);
  };
  out<<"{\"kind\":\"fixture\",\"fixture\":\"normal-decks-no-draw-end-turns\",\"resourceDigest\":\""<<hex(resources->Fingerprint())
     <<"\",\"inputDigest\":\""<<hex(Sha256(inputs))<<"\",\"recordedResponses\":"<<records.size()<<",\"scriptCount\":"<<resources->ScriptCount()<<",\"cardCount\":"<<resources->Cards().size()<<",\"captureMs\":"<<captureMs<<",\"recordMs\":"<<recordMs<<"}\n";
  // Warm allocator/core caches before taking each baseline. A candidate is
  // destroyed before every OS resource sample, including deliberate failures.
  for(auto count:cases) {
   const auto target=records.at(count).before;
   {auto warm=Rebuild(initial,resources,records,count,target);CHECK(SamePosition(warm->Current().checkpoint,target));}
   const auto baseline=usage();CHECK(baseline.children==0);emit("baseline",count,0,0,baseline);
   for(const char* kind:{"success","failure"}) for(std::size_t i=1;i<=repeats;++i) {
    begin=Clock::now();
    if(std::string(kind)=="success"){auto candidate=Rebuild(initial,resources,records,count,target);CHECK(SamePosition(candidate->Current().checkpoint,target));}
    else {auto corrupt=target;corrupt.transcriptDigest[0]^=1;bool rejected=false;try{auto candidate=Rebuild(initial,resources,records,count,corrupt);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);}
    const auto elapsed=ms(begin);const auto current=usage();
    CHECK(current.handles==baseline.handles&&current.children==baseline.children);
    CHECK(SamePosition(live->Current().checkpoint,original.checkpoint));CHECK(live->Transcript()==originalTranscript);
    emit(kind,count,i,elapsed,current);
   }
  }
  // Original live session must remain usable after every successful/failed run.
  const auto& p=original.checkpoint.prompt;
  live->Submit(integer(p.at(0)==MSG_SELECT_IDLECMD?7:0xffffffff));
  const auto next=live->Advance();CHECK(next.kind==BoundaryKind::AwaitResponse&&!next.rejectedResponse);
  out.close();CHECK(out);
  std::cout<<"Measured actual core histories; live remained usable; no AI participant was included.\n";
  return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
