#include "core_driver.h"
#include "../../ocgcore/ocgapi.h"
#include "../../ocgcore/duel.h"
#include "../../ocgcore/field.h"
#include "../../ocgcore/card.h"
#include "../../ocgcore/interpreter.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace undo {
namespace {
thread_local CoreDriver* active{};
std::map<intptr_t,CoreDriver*> handles;
void word(Bytes& b,uint64_t value,unsigned n=4) { for(unsigned i=0;i<n;++i)b.push_back(static_cast<uint8_t>(value>>(8*i))); }
void blob(Bytes& b,const Bytes& v) { word(b,v.size()); b.insert(b.end(),v.begin(),v.end()); }
struct Reader {
 const Bytes& b; size_t at{};
 void skip(size_t n) { if(n>b.size()-at)throw std::runtime_error("Truncated core message"); at+=n; }
 uint32_t get(unsigned n=1) { if(n>b.size()-at)throw std::runtime_error("Truncated core message"); uint32_t v=0; for(unsigned i=0;i<n;++i)v|=uint32_t(b[at++])<<(8*i); return v; }
 void counted(unsigned width) { auto n=get(); skip(n*width); }
};
struct Message { Bytes bytes; bool prompt{}; uint8_t player{}; };
// Fixed-baseline protocol decoder. Unknown messages fail closed; no byte scanning.
std::vector<Message> decode(const Bytes& bytes) {
 Reader r{bytes}; std::vector<Message> result;
 while(r.at<bytes.size()) {
  auto start=r.at; auto id=r.get(); bool prompt=false; uint8_t player=0;
  auto select=[&](){prompt=true;player=static_cast<uint8_t>(r.get());if(player>1)throw std::runtime_error("Invalid prompt player");};
  switch(id) {
   case MSG_RETRY: case MSG_REVERSE_DECK: case MSG_SUMMONED: case MSG_SPSUMMONED: case MSG_FLIPSUMMONED:
   case MSG_CHAIN_END: case MSG_ATTACK_DISABLED: case MSG_DAMAGE_STEP_START: case MSG_DAMAGE_STEP_END: break;
   case MSG_HINT: r.skip(6); break;
   case MSG_WIN: r.skip(2); break;
   case MSG_SELECT_BATTLECMD: select(); r.counted(11); r.counted(8); r.skip(2); break;
   case MSG_SELECT_IDLECMD: select(); for(int i=0;i<5;++i)r.counted(7); r.counted(11); r.skip(3); break;
   case MSG_SELECT_EFFECTYN: select(); r.skip(12); break;
   case MSG_SELECT_YESNO: select(); r.skip(4); break;
   case MSG_SELECT_OPTION: select(); r.counted(4); break;
   case MSG_SELECT_CARD: case MSG_SELECT_TRIBUTE: select(); r.skip(3); r.counted(8); break;
   case MSG_SELECT_UNSELECT_CARD: select(); r.skip(4); r.counted(8); r.counted(8); break;
   case MSG_SELECT_CHAIN: {select();auto n=r.get(); r.skip(9+n*14);break;}
   case MSG_SELECT_PLACE: case MSG_SELECT_DISFIELD: case MSG_SELECT_POSITION: select(); r.skip(5); break;
   case MSG_SELECT_COUNTER: select(); r.skip(4); r.counted(9); break;
   case MSG_SELECT_SUM: r.skip(1); select(); r.skip(6); r.counted(11); r.counted(11); break;
   case MSG_SORT_CARD: select(); r.counted(7); break;
   case MSG_CONFIRM_DECKTOP: case MSG_CONFIRM_EXTRATOP: r.skip(1); r.counted(7); break;
   case MSG_CONFIRM_CARDS: r.skip(2); r.counted(7); break;
   case MSG_SHUFFLE_DECK: case MSG_SWAP_GRAVE_DECK: case MSG_NEW_TURN: case MSG_CHAINED: case MSG_CHAIN_SOLVING:
   case MSG_CHAIN_SOLVED: case MSG_CHAIN_NEGATED: case MSG_CHAIN_DISABLED: case MSG_HAND_RES: r.skip(1); break;
   case MSG_SHUFFLE_HAND: case MSG_SHUFFLE_EXTRA: case MSG_RANDOM_SELECTED: case MSG_DRAW: r.skip(1); r.counted(4); break;
   case MSG_DECK_TOP: case MSG_PLAYER_HINT: r.skip(6); break;
   case MSG_SHUFFLE_SET_CARD: r.skip(1); r.counted(8); break;
   case MSG_NEW_PHASE: r.skip(2); break;
   case MSG_MOVE: case MSG_SWAP: case MSG_CHAINING: r.skip(16); break;
   case MSG_POS_CHANGE: case MSG_CARD_HINT: r.skip(9); break;
   case MSG_SET: case MSG_SUMMONING: case MSG_SPSUMMONING: case MSG_FLIPSUMMONING: case MSG_EQUIP:
   case MSG_CARD_TARGET: case MSG_CANCEL_TARGET: case MSG_ATTACK: case MSG_MISSED_EFFECT: r.skip(8); break;
   case MSG_FIELD_DISABLED: case MSG_MATCH_KILL: r.skip(4); break;
   case MSG_BECOME_TARGET: r.counted(4); break;
   case MSG_DAMAGE: case MSG_RECOVER: case MSG_LPUPDATE: case MSG_PAY_LPCOST: r.skip(5); break;
   case MSG_ADD_COUNTER: case MSG_REMOVE_COUNTER: r.skip(7); break;
   case MSG_BATTLE: r.skip(26); break;
   case MSG_TOSS_COIN: case MSG_TOSS_DICE: r.skip(1); r.counted(1); break;
   case MSG_ROCK_PAPER_SCISSORS: select(); break;
   case MSG_ANNOUNCE_RACE: case MSG_ANNOUNCE_ATTRIB: select(); r.skip(5); break;
   case MSG_ANNOUNCE_CARD: case MSG_ANNOUNCE_NUMBER: select(); r.counted(4); break;
   case MSG_RELOAD_FIELD:
    r.skip(1); for(int p=0;p<2;++p) { r.skip(4); for(int i=0;i<7;++i)if(r.get())r.skip(2); for(int i=0;i<8;++i)if(r.get())r.skip(1); r.skip(6); } r.counted(15); break;
   case MSG_AI_NAME: case MSG_SHOW_HINT: {auto n=r.get(2);r.skip(n+1);break;}
   default: throw std::runtime_error("Unsupported core message: "+std::to_string(id));
  }
  result.push_back({Bytes(bytes.begin()+start,bytes.begin()+r.at),prompt,player});
 }
 return result;
}
Bytes normalizeQuery(const Bytes& raw) {
 Reader r{raw}; Bytes out;
 while(r.at<raw.size()) {
  auto start=r.at; auto size=r.get(4); if(size<4 || size>raw.size()-start)throw std::runtime_error("Invalid core query length");
  if(size==4) {word(out,4);continue;}
  auto flags=r.get(4); word(out,size); word(out,flags);
  for(unsigned bit=0;bit<24;++bit) if(flags&(1u<<bit)) {
   if(bit==15 || bit==16 || bit==17) {
    auto count=r.get(4); if(count>(size/4))throw std::runtime_error("Invalid core query count"); std::vector<uint32_t> values;
    for(uint32_t i=0;i<count;++i)values.push_back(r.get(4));
    if(bit!=16)std::sort(values.begin(),values.end()); // target/counter sets; overlays retain order
    word(out,count); for(auto v:values)word(out,v);
   } else {word(out,r.get(4)); if(bit==23)word(out,r.get(4));}
  }
  if(r.at!=start+size)throw std::runtime_error("Unknown core query flags");
 }
 return out;
}
}
class CoreDriver::Binding {
 std::unique_lock<std::recursive_mutex> lock_{ocgapi_mutex()};
 CoreDriver* previous_; script_reader script_; card_reader card_; message_handler log_;
public:
 explicit Binding(CoreDriver* driver):previous_(active),script_(get_script_reader()),card_(get_card_reader()),log_(get_message_handler()) {
  active=driver; set_script_reader(Script); set_card_reader(Card); set_message_handler(Log);
 }
 ~Binding() { set_script_reader(script_); set_card_reader(card_); set_message_handler(log_); active=previous_; }
};
unsigned char* CoreDriver::Script(const char* path,int* len) {
 try {auto& bytes=active->resources_->Read(path); *len=static_cast<int>(bytes.size()); static byte empty{}; return bytes.empty()?&empty:const_cast<byte*>(bytes.data());}
 catch(const std::exception& e) {if(active->callbackFailure_.empty())active->callbackFailure_=e.what(); *len=0; return nullptr;}
}
uint32_t CoreDriver::Card(uint32_t code,card_data* data) {
 try {*data=active->resources_->Card(code);} catch(const std::exception& e) { data->clear(); if(active->callbackFailure_.empty())active->callbackFailure_=e.what(); } return 0;
}
uint32_t CoreDriver::Log(intptr_t handle,uint32_t type) {
 auto found=handles.find(handle); auto* owner=found==handles.end()?active:found->second;
 if(!owner)return 0; char text[256]{}; get_log_message(handle,text); owner->logs_.emplace_back(text);
 if(type==1 && owner->callbackFailure_.empty())owner->callbackFailure_=text; return 0;
}
void CoreDriver::CheckFailure() const {if(!callbackFailure_.empty())throw std::runtime_error(callbackFailure_);}
CoreDriver::CoreDriver(const InitialState& initial,std::shared_ptr<const ResourceView> resources):initial_(initial),resources_(std::move(resources)) {}
std::unique_ptr<CoreDriver> CoreDriver::Create(const InitialState& initial,std::shared_ptr<const ResourceView> resources) {
 if(!resources || initial.resourceDigest!=resources->Fingerprint())throw std::runtime_error("Initial resource digest mismatch");
 if(initial.seed.size()!=SEED_COUNT)throw std::runtime_error("Initial seed must contain SEED_COUNT words");
 if(initial.duelOptions&DUEL_TAG_MODE)throw std::runtime_error("TAG is not supported by undo sessions");
 auto driver=std::unique_ptr<CoreDriver>(new CoreDriver(initial,std::move(resources))); Binding binding(driver.get());
 driver->handle_=create_duel_undo(driver->initial_.seed.data()); handles.emplace(driver->handle_,driver.get()); driver->CheckFailure();
 for(int p=0;p<2;++p) {auto info=initial.players[p];set_player_info(driver->handle_,p,info.lp,info.startCount,info.drawCount);}
 for(auto& c:initial.cards) {
  if(c.owner>1 || c.controller>1)throw std::runtime_error("Invalid initial card player");
  new_card(driver->handle_,c.code,c.owner,c.controller,c.location,c.sequence,c.position); driver->CheckFailure();
 }
 auto* L=reinterpret_cast<duel*>(driver->handle_)->lua->lua_state;
 lua_pushlstring(L,reinterpret_cast<const char*>(initial.scenarioParameters.data()),initial.scenarioParameters.size()); lua_setglobal(L,"UNDO_SCENARIO_PARAMETERS");
 if(!initial.scenarioName.empty() && !preload_script(driver->handle_,initial.scenarioName.c_str())) {
  driver->CheckFailure(); throw std::runtime_error("Scenario preload failed: "+initial.scenarioName);
 }
 driver->CheckFailure(); driver->boundary_.failure="Not advanced"; return driver;
}
CoreDriver::~CoreDriver() {
 Binding binding(this); if(handle_) {handles.erase(handle_);end_duel(handle_);}
}
Bytes CoreDriver::Canonical() {
 Bytes result; word(result,1); auto* pd=reinterpret_cast<duel*>(handle_);
 word(result,pd->game_field->infos.turn_id); word(result,pd->game_field->infos.phase); word(result,pd->game_field->infos.turn_player);
 Bytes buffer(16*1024*1024);
 auto n=query_field_info(handle_,buffer.data()); blob(result,Bytes(buffer.begin(),buffer.begin()+n));
 for(uint8_t player=0;player<2;++player)for(uint8_t loc:{LOCATION_DECK,LOCATION_HAND,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE,LOCATION_REMOVED,LOCATION_EXTRA}) {
  word(result,player,1);word(result,loc,1);
  n=query_field_card(handle_,player,loc,0xefffff,buffer.data(),0);
  blob(result,normalizeQuery(Bytes(buffer.begin(),buffer.begin()+n)));
 }
 // Per-duel creation serials provide stable identities, including overlays and
 // currently detached instances. Never serialize pointer addresses.
 std::vector<card*> cards(pd->cards.begin(),pd->cards.end());
 std::sort(cards.begin(),cards.end(),[](auto* a,auto* b){return a->cardid<b->cardid;}); word(result,cards.size());
 for(auto* c:cards) { word(result,c->cardid,8);word(result,c->data.code);word(result,c->get_info_location());word(result,c->owner,1);word(result,c->xyz_materials.size());for(auto* x:c->xyz_materials)word(result,x->cardid,8); }
 CheckFailure(); return result;
}
Boundary CoreDriver::Advance(const LiveOutput& output) {
 Boundary old;
 { Binding binding(this);
  if(waiting_ && !submitted_)return boundary_;
  if(started_ && (boundary_.kind==BoundaryKind::Finished || boundary_.kind==BoundaryKind::Failed))return boundary_;
  old=boundary_;waiting_=false;boundary_.rejectedResponse=false;
 }
 Bytes accepted;
 try {
  if(!started_) {
   // Scenario preload messages belong before start_duel, as in legacy SingleMode.
   Bytes preload(16*1024*1024);
   {Binding binding(this);auto n=get_message(handle_,preload.data());preload.resize(n);CheckFailure();}
   for(auto& m:decode(preload)){if(m.prompt)throw std::runtime_error("Prompt during scenario preload");accepted.insert(accepted.end(),m.bytes.begin(),m.bytes.end());if(output)output(m.bytes);}
   {Binding binding(this);start_duel(handle_,initial_.duelOptions);started_=true;}
  }
  for(size_t step=0;step<100000;++step) {
   std::vector<Message> messages;bool prompt=false,finished=false;uint32_t status{};
   {
    Binding binding(this);status=process(handle_);Bytes message((status&PROCESSOR_BUFFER_LEN)+4096);auto n=get_message(handle_,message.data());message.resize(n);CheckFailure();messages=decode(message);
    bool retry=false;
    for(auto& m:messages){
     if(m.bytes[0]==MSG_RETRY){retry=true;continue;}
     accepted.insert(accepted.end(),m.bytes.begin(),m.bytes.end());
     if(m.prompt){boundary_.checkpoint.prompt=m.bytes;boundary_.checkpoint.player=m.player;prompt=true;}
     if(m.bytes[0]==MSG_WIN)finished=true;
    }
    if(retry){
     if(!submitted_)throw std::runtime_error("Core retry without submitted response");
     if(Canonical()!=old.checkpoint.canonicalState)throw std::runtime_error("Rejected response changed canonical core state");
     boundary_=old;boundary_.rejectedResponse=true;waiting_=true;submitted_=false;return boundary_;
    }
    if(prompt || finished || (status&PROCESSOR_END)){
     transcript_.insert(transcript_.end(),accepted.begin(),accepted.end());
     boundary_.kind=finished || (status&PROCESSOR_END)?BoundaryKind::Finished:BoundaryKind::AwaitResponse;
     if(boundary_.kind==BoundaryKind::Finished)boundary_.checkpoint.prompt.clear();
     boundary_.checkpoint.canonicalState=Canonical();boundary_.checkpoint.transcriptDigest=Sha256(transcript_);
     boundary_.failure.clear();waiting_=boundary_.kind==BoundaryKind::AwaitResponse;submitted_=false;
    }
   }
   // No Binding or API mutex remains while a live client animates or waits.
   if(output)for(const auto& m:messages)if(!m.prompt)output(m.bytes);
   if(prompt || finished || (status&PROCESSOR_END))return boundary_;
   if(status&PROCESSOR_WAITING)throw std::runtime_error("Core waiting without decoded prompt");
  }
  throw std::runtime_error("Core boundary processing limit exceeded");
 }catch(const std::exception& e){Binding binding(this);boundary_.kind=BoundaryKind::Failed;boundary_.failure=e.what();waiting_=false;submitted_=false;return boundary_;}
}
Bytes CoreDriver::QueryInfo() const {
 Binding binding(const_cast<CoreDriver*>(this));Bytes b(16*1024*1024);auto n=query_field_info(handle_,b.data());b.resize(n);CheckFailure();return b;
}
Bytes CoreDriver::QueryField(uint8_t player,uint8_t location,uint32_t flags) const {
 if(player>1)throw std::invalid_argument("Invalid query player");Binding binding(const_cast<CoreDriver*>(this));Bytes b(16*1024*1024);auto n=query_field_card(handle_,player,location,flags,b.data(),0);b.resize(n);CheckFailure();return b;
}
Bytes CoreDriver::QueryCard(uint8_t player,uint8_t location,uint8_t sequence,uint32_t flags) const {
 if(player>1)throw std::invalid_argument("Invalid query player");Binding binding(const_cast<CoreDriver*>(this));Bytes b(16*1024*1024);auto n=query_card(handle_,player,location,sequence,flags,b.data(),0);b.resize(n);CheckFailure();return b;
}
void CoreDriver::Submit(const Bytes& response) {
 Binding binding(this);
 if(!waiting_ || submitted_ || boundary_.kind!=BoundaryKind::AwaitResponse)throw std::logic_error("Response requires current waiting prompt");
 if(response.empty() || response.size()>SIZE_RETURN_VALUE)throw std::invalid_argument("Response exceeds fixed core response buffer");
 byte buffer[SIZE_RETURN_VALUE]{};std::copy(response.begin(),response.end(),buffer);set_responseb(handle_,buffer);submitted_=true;
}
Boundary CoreDriver::Current() const {
 // Query the actual handle: interleaving checks must not compare cached state.
 Binding binding(const_cast<CoreDriver*>(this)); auto current=boundary_;
 if(started_ && current.kind!=BoundaryKind::Failed)
  current.checkpoint.canonicalState=const_cast<CoreDriver*>(this)->Canonical();
 return current;
}
Bytes CoreDriver::Transcript() const {std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());return transcript_;}
std::vector<std::string> CoreDriver::Logs() const {std::lock_guard<std::recursive_mutex> lock(ocgapi_mutex());return logs_;}
}