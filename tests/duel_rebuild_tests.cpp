#include "core_fixture.h"
#include "undo/rebuilder.h"
#include "common.h"
#include "mtrandom.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace undo;
namespace fs=std::filesystem;
static uint32_t u32(const Bytes& b,size_t at) {CHECK(at+4<=b.size());return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);}
static Bytes integer(uint32_t v) {return {uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};}
static std::string hex(const Bytes& b) {std::ostringstream s;for(auto c:b)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(c);return s.str();}
static std::string hex(const Digest& d) {return hex(Bytes(d.begin(),d.end()));}
static void waiting(const Boundary& b) {if(b.kind!=BoundaryKind::AwaitResponse)throw std::runtime_error("Expected prompt: "+b.failure);CHECK(!b.rejectedResponse);}
static void rejects(const std::function<void()>& action) {bool threw=false;try{action();}catch(const std::exception&){threw=true;}CHECK(threw);}
// Read the public core query format inside the host-only canonical envelope.
static std::vector<uint32_t> zone(const Checkpoint& c,uint8_t player,uint8_t location) {
 const auto& b=c.canonicalState;size_t at=20+u32(b,16);
 for(unsigned i=0;i<14;++i) {auto p=b.at(at++),l=b.at(at++);auto n=u32(b,at);at+=4;auto end=at+n;CHECK(end<=b.size());std::vector<uint32_t> codes;
  while(at<end){auto len=u32(b,at);CHECK(len>=4 && at+len<=end);if(len>8){CHECK(u32(b,at+4)&QUERY_CODE);codes.push_back(u32(b,at+8));}at+=len;}
  if(p==player && l==location)return codes;
 }throw std::runtime_error("Missing canonical zone");
}
static uint32_t lp0(const Checkpoint& c){return u32(c.canonicalState,22);}
static std::vector<uint32_t> activations(const Checkpoint& c) {
 const auto& p=c.prompt;CHECK(p.at(0)==MSG_SELECT_IDLECMD);size_t at=2;
 for(int i=0;i<5;++i){auto n=p.at(at++);at+=7*n;}auto n=p.at(at++);std::vector<uint32_t> out;
 for(int i=0;i<n;++i){out.push_back(u32(p,at));at+=11;}return out;
}
static bool available(const Checkpoint& c,uint32_t code) {auto a=activations(c);return std::find(a.begin(),a.end(),code)!=a.end();}
static Bytes generic(const Checkpoint& c,uint32_t prefer=0,bool chain=false) {
 const auto& p=c.prompt;
 switch(p.at(0)) {
 case MSG_SELECT_IDLECMD:return integer(7);
 case MSG_SELECT_CHAIN:return integer(chain && p.at(2)>0 ? 0:0xffffffff);
 case MSG_SELECT_YESNO:case MSG_SELECT_EFFECTYN:return integer(1);
 case MSG_SELECT_CARD: {
  CHECK(p.at(3)==1);unsigned index=0;
  for(unsigned i=0;i<p.at(5);++i)if((prefer && u32(p,6+8*i)==prefer)||(!prefer && p.at(10+8*i)!=c.player)){index=i;break;}
  return {1,uint8_t(index)};
 }
 case MSG_SELECT_PLACE: {
  CHECK(p.at(2)==1);auto mask=u32(p,3);
  for(unsigned i=0;i<32;++i)if(!(mask&(1u<<i)))return {uint8_t(c.player^(i>=16)),uint8_t((i%16)<8?LOCATION_MZONE:LOCATION_SZONE),uint8_t(i%8)};
  break;
 }
 case MSG_SELECT_POSITION: {auto mask=p.at(6);for(unsigned i=1;i<=8;i*=2)if(mask&i)return integer(i);break;}
 case MSG_SELECT_OPTION:return integer(0);
 }
 throw std::runtime_error("Unhandled scenario prompt "+hex(p));
}
struct Run {
 InitialState initial;std::shared_ptr<const ResourceView> resources;std::unique_ptr<CoreDriver> live;std::vector<ResponseRecord> history;Boundary now;
 Run(InitialState i,std::shared_ptr<const ResourceView> r):initial(i),resources(r),live(CoreDriver::Create(i,r)),now(live->Advance()){waiting(now);}
 void submit(Bytes r,Origin origin=Origin::Manual) {auto before=now.checkpoint;live->Submit(r);now=live->Advance();waiting(now);history.push_back({before.player,origin,std::move(r),std::move(before)});}
 void idle(uint32_t prefer=0) {for(unsigned n=0;now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n){CHECK(n<30);submit(generic(now.checkpoint,prefer),now.checkpoint.prompt[0]==MSG_SELECT_CHAIN?Origin::Automatic:Origin::Manual);}}
 void activate(uint32_t code) {idle();auto a=activations(now.checkpoint);auto it=std::find(a.begin(),a.end(),code);CHECK(it!=a.end());submit(integer(5+(uint32_t(it-a.begin())<<16)));}
};
static InitialState initial(std::shared_ptr<const ResourceView> r,unsigned seed=42,bool shuffled=false) {
 InitialState s;s.seed.resize(SEED_COUNT,seed);s.noCheckDeck=true;s.noShuffleDeck=!shuffled;s.duelOptions=(5u<<16)|(shuffled?0:DUEL_PSEUDO_SHUFFLE);s.resourceDigest=r->Fingerprint();
 mtrandom host(s.seed.data(),s.seed.size());
 for(uint8_t p=0;p<2;++p){s.players[p]={8000,0,1};std::vector<uint32_t> deck;for(int n=0;n<20;++n)deck.push_back(n%2?46986414:89631139);if(shuffled)host.shuffle_vector(deck);for(auto it=deck.rbegin();it!=deck.rend();++it)s.cards.push_back({*it,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});}
 return s;
}
static void add(InitialState& s,uint32_t code,uint8_t p,uint8_t location,uint8_t sequence=0,uint8_t pos=POS_FACEUP_ATTACK){s.cards.push_back({code,p,p,location,sequence,pos});}
struct Case {std::string name;InitialState initial;std::vector<ResponseRecord> history;size_t keep{};Checkpoint target;Checkpoint end;};
static Case result(const std::string& name,Run& r,size_t keep) {CHECK(keep<r.history.size());return {name,r.initial,r.history,keep,r.history[keep].before,r.now.checkpoint};}
static std::vector<Case> record(std::shared_ptr<const ResourceView> resources) {
 std::vector<Case> out;
 for(bool shuffled:{false,true}) {
  auto s=initial(resources,42,shuffled);s.players[0].startCount=s.players[1].startCount=5;Run r(s,resources);r.idle();
  auto hand=zone(r.now.checkpoint,0,LOCATION_HAND);CHECK(hand.size()==5);if(!shuffled)CHECK(hand==std::vector<uint32_t>({89631139,46986414,89631139,46986414,89631139}));
  auto keep=r.history.size();r.submit(integer(7));r.idle();r.submit(integer(7));r.idle();out.push_back(result(shuffled?"initial-shuffle":"no-shuffle",r,keep));
 }
 {
  auto s=initial(resources,73);add(s,37812118,0,LOCATION_HAND);Run r(s,resources);r.idle();auto keep=r.history.size();r.activate(37812118);r.idle();
  CHECK(zone(r.now.checkpoint,0,LOCATION_GRAVE)==std::vector<uint32_t>({37812118}));CHECK(zone(r.now.checkpoint,0,LOCATION_HAND).size()+zone(r.now.checkpoint,1,LOCATION_HAND).size()==2);
  r.submit(integer(7));r.idle();out.push_back(result("draw-random",r,keep));
 }
 {
  auto s=initial(resources,81);add(s,8267140,0,LOCATION_HAND);add(s,79759861,0,LOCATION_HAND);add(s,46986414,0,LOCATION_HAND);
  add(s,83968380,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE);add(s,89631139,1,LOCATION_MZONE);
  Run r(s,resources);r.idle();r.activate(8267140);r.submit(generic(r.now.checkpoint));auto keep=r.history.size();CHECK(lp0(r.now.checkpoint)==7000);r.idle(83968380);
  CHECK(zone(r.now.checkpoint,1,LOCATION_REMOVED)==std::vector<uint32_t>({83968380}));r.activate(79759861);r.idle(46986414);
  CHECK(lp0(r.now.checkpoint)==7000);CHECK(zone(r.now.checkpoint,0,LOCATION_HAND).empty());CHECK(zone(r.now.checkpoint,1,LOCATION_MZONE).empty());CHECK(zone(r.now.checkpoint,1,LOCATION_GRAVE)==std::vector<uint32_t>({89631139}));
  out.push_back(result("lp-discard-cost",r,keep));
 }
 {
  auto s=initial(resources,93);add(s,83968380,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE);add(s,83968380,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE);Run r(s,resources);
  unsigned chosen=0;size_t keep=0;
  for(unsigned n=0;r.now.checkpoint.prompt[0]!=MSG_SELECT_IDLECMD;++n){CHECK(n<30);auto& p=r.now.checkpoint.prompt;bool choose=p[0]==MSG_SELECT_CHAIN && p[2]>0;
   if(choose){++chosen;if(chosen==2)keep=r.history.size();}r.submit(generic(r.now.checkpoint,0,choose));}
  CHECK(chosen==2);CHECK(zone(r.now.checkpoint,0,LOCATION_HAND).size()==1);CHECK(zone(r.now.checkpoint,1,LOCATION_HAND).size()==1);
  CHECK(zone(r.now.checkpoint,0,LOCATION_GRAVE)==std::vector<uint32_t>({83968380}));CHECK(zone(r.now.checkpoint,1,LOCATION_GRAVE)==std::vector<uint32_t>({83968380}));
  out.push_back(result("multiple-chain-choices",r,keep));
 }
 {
  auto s=initial(resources,117);add(s,88264978,0,LOCATION_MZONE);add(s,89631139,0,LOCATION_HAND);add(s,89631139,0,LOCATION_HAND);Run r(s,resources);r.idle();CHECK(available(r.now.checkpoint,88264978));auto keep=r.history.size();r.activate(88264978);r.idle(89631139);
  CHECK(zone(r.now.checkpoint,0,LOCATION_MZONE).size()==2);CHECK(zone(r.now.checkpoint,0,LOCATION_HAND).size()==1);CHECK(!available(r.now.checkpoint,88264978));
  // A second valid dragon and spare zones remain: absence is the real script's
  // once-per-turn effect count, not loss of legal summon resources.
  r.submit(integer(7));r.idle();r.submit(integer(7));r.idle();CHECK(available(r.now.checkpoint,88264978));
  out.push_back(result("once-per-turn",r,keep));
 }
 return out;
}
static void behavior(const Case& f,const Checkpoint& c) {
 if(f.name=="once-per-turn" && c.prompt.at(0)==MSG_SELECT_IDLECMD && c.player==0){
  auto monsters=zone(c,0,LOCATION_MZONE);auto turn=u32(c.canonicalState,4);
  if(monsters.size()==1)CHECK(available(c,88264978));
  if(monsters.size()==2 && turn==1){CHECK(zone(c,0,LOCATION_HAND).size()==1);CHECK(!available(c,88264978));}
  if(monsters.size()==2 && turn==3)CHECK(available(c,88264978));
 }
 if(f.name=="lp-discard-cost" && SamePosition(c,f.target))CHECK(lp0(c)==7000);
}
static void deterministic(const Case& f,std::shared_ptr<const ResourceView> resources) {
 // Also restore *after* the real once-per-turn effect was consumed. There is
 // still another dragon to summon: the private effect count must suppress it
 // until the turn changes, then the same following inputs reset it normally.
 if(f.name=="once-per-turn"){
  bool checked=false;
  for(size_t i=0;i<f.history.size();++i){const auto& c=f.history[i].before;
   if(c.prompt.at(0)==MSG_SELECT_IDLECMD && c.player==0 && u32(c.canonicalState,4)==1 && zone(c,0,LOCATION_MZONE).size()==2){
    for(int repeat=0;repeat<3;++repeat){auto used=Rebuild(f.initial,resources,f.history,i,c);behavior(f,used->Current().checkpoint);CHECK(!available(used->Current().checkpoint,88264978));
     for(size_t j=i;j<f.history.size();++j){CHECK(SamePosition(used->Current().checkpoint,f.history[j].before));used->Submit(f.history[j].response);waiting(used->Advance());}
     CHECK(SamePosition(used->Current().checkpoint,f.end));CHECK(available(used->Current().checkpoint,88264978));}
    std::cout<<"once-per-turn consumed-prefix="<<i<<" repeats=3 unavailable-until-next-turn passed\n";checked=true;break;
   }
  }CHECK(checked);
 }

 for(unsigned repeat=0;repeat<3;++repeat){auto start=std::chrono::steady_clock::now();auto candidate=Rebuild(f.initial,resources,f.history,f.keep,f.target);CHECK(SamePosition(candidate->Current().checkpoint,f.target));behavior(f,candidate->Current().checkpoint);
  for(size_t i=f.keep;i<f.history.size();++i){CHECK(SamePosition(candidate->Current().checkpoint,f.history[i].before));behavior(f,candidate->Current().checkpoint);candidate->Submit(f.history[i].response);waiting(candidate->Advance());}
  CHECK(SamePosition(candidate->Current().checkpoint,f.end));behavior(f,candidate->Current().checkpoint);
  std::cout<<f.name<<" repeat="<<repeat+1<<" responses="<<f.history.size()<<" keep="<<f.keep<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" target="<<hex(f.target.transcriptDigest)<<"\n";
 }
}

// Versioned length-prefixed host-internal fixtures. No player-wire encoder is
// offered. All tracked inputs are authored public card placements, never decks
// or logs captured from a user's game.
struct Writer {
 Bytes b;
 void number(uint64_t n,unsigned width=4){for(unsigned i=0;i<width;++i)b.push_back(uint8_t(n>>(8*i)));}
 void blob(const Bytes& v){number(v.size());b.insert(b.end(),v.begin(),v.end());}
 void string(const std::string& v){blob(Bytes(v.begin(),v.end()));}
 void digest(const Digest& d){b.insert(b.end(),d.begin(),d.end());}
 void checkpoint(const Checkpoint& c){number(c.player,1);blob(c.prompt);blob(c.canonicalState);digest(c.transcriptDigest);for(auto x:c.clock.remainingMs)number(uint64_t(x),8);number(c.aiLogCursor,8);}
};
struct Reader {
 const Bytes& b;size_t at{};
 uint64_t number(unsigned width=4){if(width>b.size()-at)throw std::runtime_error("Truncated fixture integer");uint64_t v=0;for(unsigned i=0;i<width;++i)v|=uint64_t(b[at++])<<(8*i);return v;}
 size_t count(size_t limit){auto n=number();if(n>limit)throw std::runtime_error("Fixture count exceeds limit");return size_t(n);}
 Bytes blob(size_t limit=16*1024*1024){auto n=count(limit);if(n>b.size()-at)throw std::runtime_error("Truncated fixture blob");Bytes v(b.begin()+at,b.begin()+at+n);at+=n;return v;}
 std::string string(){auto v=blob(4096);return std::string(v.begin(),v.end());}
 uint8_t player(){auto p=number(1);if(p>1)throw std::runtime_error("Invalid fixture player");return uint8_t(p);}
 bool boolean(){return player()!=0;}
 Digest digest(){Digest d{};for(auto& x:d)x=uint8_t(number(1));return d;}
 Checkpoint checkpoint(){Checkpoint c;c.player=player();c.prompt=blob(65536);c.canonicalState=blob();c.transcriptDigest=digest();for(auto& x:c.clock.remainingMs)x=int64_t(number(8));c.aiLogCursor=size_t(number(8));return c;}
 void end(){if(at!=b.size())throw std::runtime_error("Trailing fixture data");}
};
static Bytes encode(const Case& c) {
 Writer w;w.string(c.name);const auto& s=c.initial;w.number(s.seed.size());for(auto n:s.seed)w.number(n);w.number(s.duelOptions);w.number(s.noCheckDeck,1);w.number(s.noShuffleDeck,1);
 for(auto p:s.players){w.number(uint32_t(p.lp));w.number(uint32_t(p.startCount));w.number(uint32_t(p.drawCount));}w.number(s.cards.size());for(auto x:s.cards){w.number(x.code);w.number(x.owner,1);w.number(x.controller,1);w.number(x.location,1);w.number(x.sequence,1);w.number(x.position,1);}
 w.string(s.scenarioName);w.blob(s.scenarioParameters);w.digest(s.resourceDigest);w.number(c.history.size());
 for(auto& r:c.history){w.number(r.player,1);w.number(uint8_t(r.origin),1);w.blob(r.response);w.checkpoint(r.before);}w.number(c.keep);w.checkpoint(c.target);w.checkpoint(c.end);
 Writer file;file.string("YGOUndoCase");file.number(1);file.blob(w.b);file.digest(Sha256(w.b));return file.b;
}
static Case decode(const Bytes& bytes) {
 if(bytes.size()>64*1024*1024)throw std::runtime_error("Fixture file exceeds limit");Reader file{bytes};if(file.string()!="YGOUndoCase" || file.number()!=1)throw std::runtime_error("Unsupported fixture version");auto payload=file.blob(64*1024*1024);if(file.digest()!=Sha256(payload))throw std::runtime_error("Fixture checksum mismatch");file.end();Reader r{payload};Case c;c.name=r.string();auto& s=c.initial;
 auto seeds=r.count(SEED_COUNT);if(seeds!=SEED_COUNT)throw std::runtime_error("Wrong fixture seed length");for(size_t i=0;i<seeds;++i)s.seed.push_back(uint32_t(r.number()));s.duelOptions=uint32_t(r.number());s.noCheckDeck=r.boolean();s.noShuffleDeck=r.boolean();
 for(auto& p:s.players){p.lp=int32_t(r.number());p.startCount=int32_t(r.number());p.drawCount=int32_t(r.number());}auto cards=r.count(512);for(size_t i=0;i<cards;++i){InitialCard x;x.code=uint32_t(r.number());x.owner=r.player();x.controller=r.player();x.location=uint8_t(r.number(1));x.sequence=uint8_t(r.number(1));x.position=uint8_t(r.number(1));s.cards.push_back(x);}
 s.scenarioName=r.string();s.scenarioParameters=r.blob();s.resourceDigest=r.digest();auto responses=r.count(10000);for(size_t i=0;i<responses;++i){ResponseRecord x;x.player=r.player();auto origin=r.number(1);if(origin>uint8_t(Origin::Bot))throw std::runtime_error("Invalid fixture origin");x.origin=Origin(origin);x.response=r.blob(256);if(x.response.empty())throw std::runtime_error("Empty fixture response");x.before=r.checkpoint();if(x.player!=x.before.player)throw std::runtime_error("Fixture response owner mismatch");c.history.push_back(std::move(x));}
 c.keep=r.count(c.history.size());c.target=r.checkpoint();c.end=r.checkpoint();r.end();return c;
}
static void write(const fs::path& path,const Bytes& b){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());CHECK(bool(f));}
static Bytes read(const fs::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Missing public fixture: "+path.string());auto n=fs::file_size(path);if(n>64*1024*1024)throw std::runtime_error("Fixture file exceeds limit");Bytes b(size_t(n),0);f.read(reinterpret_cast<char*>(b.data()),b.size());CHECK(bool(f));return b;}
static void unchanged(CoreDriver& live,const Checkpoint& before,const Bytes& transcript,const std::vector<std::string>& logs,const Case& f,const Bytes& saved) {
 CHECK(SamePosition(live.Current().checkpoint,before));CHECK(live.Transcript()==transcript);CHECK(live.Logs()==logs);CHECK(encode(f)==saved);
}
static void faults(const Case& f,std::shared_ptr<const ResourceView> resources) {
 Run active(f.initial,resources);const auto before=active.now.checkpoint;auto transcript=active.live->Transcript();auto logs=active.live->Logs();auto saved=encode(f);
 auto check=[&](const std::function<void()>& action){rejects(action);unchanged(*active.live,before,transcript,logs,f,saved);};
 auto target=f.history[1].before;
 check([&]{Rebuild(f.initial,resources,f.history,f.history.size()+1,target);});
 for(int field=0;field<4;++field){auto bad=target;if(field==0)bad.player^=1;if(field==1)bad.prompt.push_back(255);if(field==2)bad.canonicalState.push_back(255);if(field==3)bad.transcriptDigest[0]^=1;check([&]{Rebuild(f.initial,resources,f.history,1,bad);});}
 for(int field=0;field<4;++field){auto bad=f.history;auto& c=bad[0].before;if(field==0)c.player^=1;if(field==1)c.prompt.push_back(255);if(field==2)c.canonicalState.push_back(255);if(field==3)c.transcriptDigest[0]^=1;check([&]{Rebuild(f.initial,resources,bad,1,target);});}
 auto bad=f.history;bad[0].response=integer(99);check([&]{Rebuild(f.initial,resources,bad,1,target);});
 bad=f.history;bad[0].player^=1;check([&]{Rebuild(f.initial,resources,bad,1,target);});
 bad=f.history;bad[0].origin=Origin(255);check([&]{Rebuild(f.initial,resources,bad,1,target);});
 auto wrong=f.initial;wrong.resourceDigest[0]^=1;check([&]{Rebuild(wrong,resources,f.history,1,target);});check([&]{Rebuild(f.initial,nullptr,f.history,1,target);});
 wrong=f.initial;wrong.scenarioName="single/C3-intentionally-missing.lua";check([&]{Rebuild(wrong,resources,{},0,before);});
 wrong=f.initial;wrong.players[0].startCount=100;{auto finished=CoreDriver::Create(wrong,resources);CHECK(finished->Advance().kind==BoundaryKind::Finished);}check([&]{Rebuild(wrong,resources,{},0,before);});
 // A deliberately failing real Lua callback reaches BoundaryKind::Failed during
 // processing. This controlled fault scenario is separate from the six installed
 // card cases; its tiny framework has no gameplay substitute or resource copy.
 const auto faultRoot=(fs::u8path(UNDO_REBUILD_RESULTS)/"processing-fault").string();
 fixture::database(faultRoot);
 for(auto name:{"constant.lua","utility.lua","procedure.lua"})fixture::WriteFixtureFile(faultRoot+"/script/"+name,{});
 const auto scenario=fixture::bytes(R"lua(
local e=Effect.GlobalEffect()
e:SetType(0x802)
e:SetCode(1040)
e:SetOperation(function(e)
 e:Reset()
 local fail=Duel.SelectYesNo(0,123)
 if fail then error('C3 deliberate callback failure') end
 Duel.SelectYesNo(1,456)
end)
Duel.RegisterEffect(e,0)
)lua");
 fixture::WriteFixtureFile(faultRoot+"/single/failure.lua",scenario);
 auto pinned=ResourceView::Capture(faultRoot);InitialState faultInit;faultInit.seed.resize(SEED_COUNT,42);faultInit.duelOptions=5u<<16;faultInit.resourceDigest=pinned->Fingerprint();faultInit.scenarioName="single/failure.lua";
 for(uint8_t p=0;p<2;++p)for(int i=0;i<5;++i)faultInit.cards.push_back({900000001,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 auto probe=CoreDriver::Create(faultInit,pinned);auto prompt=probe->Advance();waiting(prompt);std::vector<ResponseRecord> failing{{0,Origin::Manual,integer(1),prompt.checkpoint}};
 probe->Submit(integer(1));auto failed=probe->Advance();CHECK(failed.kind==BoundaryKind::Failed);CHECK(failed.failure.find("C3 deliberate callback failure")!=std::string::npos);probe.reset();
 check([&]{Rebuild(faultInit,pinned,failing,1,prompt.checkpoint);});
 // A changed source cannot enter an already captured view, including scenario
 // bytes first requested by a newly created candidate. A fresh changed view is
 // rejected by the old initial digest. The original installed view stays live.
 fixture::WriteFixtureFile(faultRoot+"/single/failure.lua",fixture::bytes("error('changed disk')"));
 auto frozen=Rebuild(faultInit,pinned,{},0,prompt.checkpoint);CHECK(SamePosition(frozen->Current().checkpoint,prompt.checkpoint));
 frozen->Submit(integer(0));waiting(frozen->Advance());CHECK(frozen->Current().checkpoint.player==1);
 auto changedResources=ResourceView::Capture(faultRoot);check([&]{Rebuild(faultInit,changedResources,{},0,prompt.checkpoint);});

 auto external=target;external.clock.remainingMs={123,456};external.aiLogCursor=987;CHECK(SamePosition(target,external));CHECK(SamePosition(Rebuild(f.initial,resources,f.history,1,external)->Current().checkpoint,target));
 // Only the retained prefix is consumed; corrupt discarded choices are irrelevant.
 bad=f.history;bad[1].response=integer(99);CHECK(SamePosition(Rebuild(f.initial,resources,bad,1,target)->Current().checkpoint,target));
 active.submit(f.history[0].response);CHECK(SamePosition(active.now.checkpoint,target));
 auto encoded=encode(f);CHECK(encode(decode(encoded))==encoded);
 for(auto n:{size_t(0),size_t(5),encoded.size()-1}){auto cut=encoded;cut.resize(n);rejects([&]{decode(cut);});}
 auto corrupt=encoded;corrupt.push_back(0);rejects([&]{decode(corrupt);});corrupt=encoded;corrupt[14]=2;rejects([&]{decode(corrupt);});corrupt=encoded;corrupt[18]=255;corrupt[19]=255;corrupt[20]=255;corrupt[21]=127;rejects([&]{decode(corrupt);});
 corrupt=encoded;corrupt[40]^=1;rejects([&]{decode(corrupt);});
 auto invalid=f;invalid.history[0].response.clear();rejects([&]{decode(encode(invalid));});invalid=f;invalid.history[0].origin=Origin(255);rejects([&]{decode(encode(invalid));});invalid=f;invalid.initial.noCheckDeck=true;invalid.history[0].player=2;rejects([&]{decode(encode(invalid));});invalid=f;invalid.keep=f.history.size()+1;rejects([&]{decode(encode(invalid));});
 std::cout<<"faults: prefix/target/metadata/retry/resources/frozen-script/missing-script/failed/finished/strict-fixture; live continued\n";
}
static void isolation(const Case& f,std::shared_ptr<const ResourceView> resources) {
 Run active(f.initial,resources);for(auto& r:f.history)active.submit(r.response,r.origin);auto before=active.now.checkpoint;auto transcript=active.live->Transcript();auto logs=active.live->Logs();auto saved=encode(f);
 for(int repeat=0;repeat<3;++repeat){auto candidate=Rebuild(f.initial,resources,f.history,f.keep,f.target);unchanged(*active.live,before,transcript,logs,f,saved);candidate.reset();unchanged(*active.live,before,transcript,logs,f,saved);}
 auto control=Rebuild(f.initial,resources,f.history,f.history.size(),f.end);auto response=generic(before);active.submit(response);control->Submit(response);auto next=control->Advance();waiting(next);CHECK(SamePosition(active.now.checkpoint,next.checkpoint));
 std::cout<<"isolation "<<f.name<<": original core state/transcript/log/history preserved; original and control continue equally\n";
}
int main(int argc,char** argv) {
 try {
  std::string root,suite="deterministic";bool recording=false;for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--runtime-root" && i+1<argc)root=argv[++i];else if(a=="--suite" && i+1<argc)suite=argv[++i];else if(a=="--record-fixtures")recording=true;else throw std::runtime_error("Unknown/missing argument: "+a);}CHECK(!root.empty());CHECK(suite=="deterministic"||suite=="isolation"||suite=="faults");
  std::cout<<std::unitbuf;auto resources=ResourceView::Capture(root);std::cout<<"resourceDigest="<<hex(resources->Fingerprint())<<" scripts="<<resources->ScriptCount()<<" cards="<<resources->Cards().size()<<"\n";
  for(auto id:{89631139u,46986414u,37812118u,79759861u,88264978u,8267140u,83968380u}){CHECK(resources->Card(id).code==id);if(resources->Card(id).type&TYPE_EFFECT || !(resources->Card(id).type&TYPE_MONSTER))CHECK(!resources->Read("script/c"+std::to_string(id)+".lua").empty());}
  std::vector<Case> cases;
  if(recording){cases=record(resources);for(auto& f:cases){auto b=encode(f);CHECK(encode(decode(b))==b);write(fs::u8path(UNDO_REBUILD_RESULTS)/(f.name+".duel"),b);std::cout<<"recorded "<<f.name<<" bytes="<<b.size()<<" target="<<hex(f.target.transcriptDigest)<<"\n";}return 0;}
  for(auto name:{"no-shuffle","initial-shuffle","draw-random","lp-discard-cost","multiple-chain-choices","once-per-turn"}){auto f=decode(read(fs::u8path(UNDO_REBUILD_FIXTURES)/(std::string(name)+".duel")));CHECK(f.name==name);cases.push_back(std::move(f));}
  if(suite=="deterministic")for(auto& f:cases)deterministic(f,resources);
  if(suite=="isolation")for(auto& f:cases)isolation(f,resources);
  if(suite=="faults")faults(cases[0],resources);
  CHECK(ResourceView::Capture(root)->Fingerprint()==resources->Fingerprint());
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
