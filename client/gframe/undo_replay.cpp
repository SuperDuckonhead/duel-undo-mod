#include "replay.h"
#include "config.h"
#include <algorithm>
#include <stdexcept>
namespace ygo {
namespace {
using undo::Bytes;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void word(Bytes& b,std::uint32_t n,unsigned width=4){for(unsigned i=0;i<width;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
void blob(Bytes& b,const Bytes& data){require(data.size()<=MAX_REPLAY_SIZE,"Undo replay field exceeds size limit");word(b,data.size());b.insert(b.end(),data.begin(),data.end());}
struct Reader {
 const unsigned char* data;std::size_t at{},end;
 std::uint32_t word(unsigned n=4){require(n<=end-at,"Truncated undo replay");std::uint32_t value{};for(unsigned i=0;i<n;++i)value|=std::uint32_t(data[at++])<<(8*i);return value;}
 Bytes bytes(std::size_t n){require(n<=end-at,"Truncated undo replay");Bytes b(data+at,data+at+n);at+=n;return b;}
 Bytes blob(){return bytes(word());}
 undo::Digest digest(){auto b=bytes(32);undo::Digest d;std::copy(b.begin(),b.end(),d.begin());return d;}
};
void validate(const undo::InitialState& initial) {
 require(initial.seed.size()==SEED_COUNT,"Invalid undo replay seed");
 require(!(initial.duelOptions&DUEL_TAG_MODE),"TAG undo replay unsupported");
 require(initial.scenarioName.size()<256,"Undo replay scenario name too long");
 require(initial.scenarioName.find('\0')==std::string::npos && initial.scenarioName.find('\\')==std::string::npos && initial.scenarioName.find("..")==std::string::npos,"Unsafe undo replay scenario path");
 if(!initial.scenarioName.empty())require(initial.scenarioName[0]!='/'&&initial.scenarioName.find(':')==std::string::npos,"Unsafe undo replay scenario root");
 for(auto& p:initial.players)require(p.lp>=0 && p.startCount>=0 && p.startCount<=255 && p.drawCount>=0 && p.drawCount<=255,"Invalid undo replay player initialization");
 require(initial.cards.size()<=4096,"Undo replay card count exceeds limit");
 for(auto& c:initial.cards){
  require(c.code && c.owner<2&&c.controller<2,"Invalid undo replay card owner");
  require(c.location==LOCATION_DECK||c.location==LOCATION_HAND||c.location==LOCATION_MZONE||c.location==LOCATION_SZONE||c.location==LOCATION_GRAVE||c.location==LOCATION_REMOVED||c.location==LOCATION_EXTRA,"Invalid undo replay card location");
  require((c.location!=LOCATION_MZONE||c.sequence<7)&&(c.location!=LOCATION_SZONE||c.sequence<8),"Invalid undo replay field sequence");
  require(c.position && !(c.position&~15u),"Invalid undo replay card position");
 }
}
}
void Replay::RecordUndoSingle(const undo::InitialState& initial,const std::vector<undo::ResponseRecord>& records,const wchar_t* host,const wchar_t* peer) {
 RecordUndo(initial,records,host,peer,true);
}
void Replay::RecordUndoDuel(const undo::InitialState& initial,const std::vector<undo::ResponseRecord>& records,const wchar_t* host,const wchar_t* peer) {
 RecordUndo(initial,records,host,peer,false);
}
void Replay::RecordUndo(const undo::InitialState& initial,const std::vector<undo::ResponseRecord>& records,const wchar_t* host,const wchar_t* peer,bool single) {
 validate(initial);require(!is_recording,"Cannot replace active legacy recording");require(host&&peer,"Missing replay player names");
 Bytes bytes;std::vector<std::wstring> nextPlayers;
 for(const auto* name:{host,peer}){uint16_t value[20]{};BufferIO::CopyCharArray(name,value);for(auto c:value)word(bytes,c,2);wchar_t wide[20]{};BufferIO::CopyCharArray(value,wide);nextPlayers.emplace_back(wide);}
 const DuelParameters nextParams{initial.players[0].lp,initial.players[0].startCount,initial.players[0].drawCount,initial.duelOptions};
 word(bytes,nextParams.start_lp);word(bytes,nextParams.start_hand);word(bytes,nextParams.draw_count);word(bytes,nextParams.duel_flag);
 word(bytes,0x31444e55);word(bytes,2,2);word(bytes,single?1:0,1);
 for(auto s:initial.seed)word(bytes,s);
 word(bytes,initial.duelOptions);word(bytes,initial.noCheckDeck,1);word(bytes,initial.noShuffleDeck,1);
 for(auto p:initial.players){word(bytes,p.lp);word(bytes,p.startCount);word(bytes,p.drawCount);}
 word(bytes,initial.cards.size());
 for(auto c:initial.cards){word(bytes,c.code);for(auto v:{c.owner,c.controller,c.location,c.sequence,c.position})word(bytes,v,1);}
 blob(bytes,Bytes(initial.scenarioName.begin(),initial.scenarioName.end()));blob(bytes,initial.scenarioParameters);
 bytes.insert(bytes.end(),initial.resourceDigest.begin(),initial.resourceDigest.end());
 require(records.size()<=MAX_REPLAY_SIZE/68,"Undo replay has too many responses");word(bytes,records.size());
 std::vector<UndoResponse> nextResponses;nextResponses.reserve(records.size());
 for(const auto& record:records){
  require(record.player<2&&record.before.player==record.player&&static_cast<unsigned>(record.origin)<=2,"Invalid accepted replay response metadata");
  require(!record.response.empty()&&record.response.size()<=256,"Invalid undo replay response size");
  UndoResponse item{record.player,record.origin,record.response,undo::Sha256(record.before.prompt),record.before.transcriptDigest};
  word(bytes,item.player,1);word(bytes,static_cast<unsigned>(item.origin),1);word(bytes,item.response.size(),2);
  bytes.insert(bytes.end(),item.response.begin(),item.response.end());bytes.insert(bytes.end(),item.promptDigest.begin(),item.promptDigest.end());bytes.insert(bytes.end(),item.transcriptDigest.begin(),item.transcriptDigest.end());
  require(bytes.size()+32<=MAX_REPLAY_SIZE,"Undo replay exceeds size limit; no history was truncated");nextResponses.push_back(std::move(item));
 }
 auto checksum=undo::Sha256(bytes);bytes.insert(bytes.end(),checksum.begin(),checksum.end());require(bytes.size()<=MAX_REPLAY_SIZE,"Undo replay exceeds size limit");
 auto nextInitial=initial;ExtendedReplayHeader header{};header.base.id=REPLAY_ID_YRP2;header.base.version=PRO_VERSION;
 header.base.flag=REPLAY_UNIFORM|REPLAY_UNDO_CORE|(single?REPLAY_SINGLE_MODE:0);header.base.datasize=bytes.size();header.header_version=2;
 std::copy(initial.seed.begin(),initial.seed.end(),header.seed_sequence);
 // All fallible preparation finishes before replacing the memory record.
 Reset();pheader=header;params=nextParams;players.swap(nextPlayers);undo_initial_=std::move(nextInitial);undo_responses_.swap(nextResponses);
 std::memcpy(replay_data,bytes.data(),bytes.size());replay_size=bytes.size();comp_size=0;
}
bool Replay::ReadUndoInfo() {
 try {
  require(pheader.base.id==REPLAY_ID_YRP2&&pheader.base.version==PRO_VERSION&&pheader.header_version==2,"Unsupported undo replay header");
  require(pheader.base.flag==(REPLAY_UNIFORM|REPLAY_UNDO_CORE) || pheader.base.flag==(REPLAY_UNIFORM|REPLAY_UNDO_CORE|REPLAY_SINGLE_MODE),"Unsupported undo replay flags");
  require(pheader.base.seed==0&&pheader.base.start_time==0&&pheader.value1==0&&pheader.value2==0&&pheader.value3==0&&std::all_of(std::begin(pheader.base.props),std::end(pheader.base.props),[](auto b){return b==0;}),"Invalid reserved undo replay header fields");
  require(replay_size==pheader.base.datasize&&replay_size>=data_position+32,"Invalid undo replay size");
  Reader r{replay_data,data_position,replay_size-32};
  const auto actual=undo::Sha256(Bytes(replay_data,replay_data+r.end));require(std::equal(actual.begin(),actual.end(),replay_data+r.end),"Undo replay checksum mismatch");
  require(r.word()==0x31444e55,"Unsupported undo replay body");
  const auto version=r.word(2);
  if(version==1) require((pheader.base.flag&REPLAY_SINGLE_MODE)!=0,"Legacy undo body is Single only");
  else { require(version==2,"Unsupported undo replay body version");const auto mode=r.word(1);require(mode<=1&&bool(mode)==bool(pheader.base.flag&REPLAY_SINGLE_MODE),"Undo replay mode mismatch"); }
  undo::InitialState initial;for(unsigned i=0;i<SEED_COUNT;++i){auto seed=r.word();require(seed==pheader.seed_sequence[i],"Undo replay seed mismatch");initial.seed.push_back(seed);}
  initial.duelOptions=r.word();auto noCheck=r.word(1),noShuffle=r.word(1);require(noCheck<=1&&noShuffle<=1,"Invalid undo replay options");initial.noCheckDeck=noCheck;initial.noShuffleDeck=noShuffle;
  for(auto& p:initial.players){p.lp=static_cast<int32_t>(r.word());p.startCount=static_cast<int32_t>(r.word());p.drawCount=static_cast<int32_t>(r.word());}
  auto cards=r.word();require(cards<=4096,"Invalid undo replay card count");for(unsigned i=0;i<cards;++i){undo::InitialCard c;c.code=r.word();c.owner=r.word(1);c.controller=r.word(1);c.location=r.word(1);c.sequence=r.word(1);c.position=r.word(1);initial.cards.push_back(c);}
  auto name=r.blob();initial.scenarioName.assign(name.begin(),name.end());initial.scenarioParameters=r.blob();initial.resourceDigest=r.digest();validate(initial);
  require(params.duel_flag==initial.duelOptions&&params.start_lp==initial.players[0].lp&&params.start_hand==initial.players[0].startCount&&params.draw_count==initial.players[0].drawCount,"Undo replay initialization mismatch");
  auto count=r.word();require(count<=MAX_REPLAY_SIZE/68,"Invalid undo replay response count");std::vector<UndoResponse> responses;responses.reserve(count);
  for(unsigned i=0;i<count;++i){UndoResponse item;item.player=r.word(1);auto origin=r.word(1);auto size=r.word(2);require(item.player<2&&origin<=2&&size>0&&size<=256,"Invalid undo replay response");item.origin=static_cast<undo::Origin>(origin);item.response=r.bytes(size);item.promptDigest=r.digest();item.transcriptDigest=r.digest();responses.push_back(std::move(item));}
  require(r.at==r.end,"Trailing undo replay body");undo_initial_=std::move(initial);undo_responses_.swap(responses);undo_response_index_=0;data_position=replay_size;script_name=undo_initial_.scenarioName;return true;
 }catch(const std::exception&){return false;}
}
void Replay::SwapUndoRecord(Replay& other) noexcept {
 using std::swap;
 swap(pheader,other.pheader);swap(params,other.params);
 players.swap(other.players);decks.swap(other.decks);script_name.swap(other.script_name);
 swap(undo_initial_,other.undo_initial_);undo_responses_.swap(other.undo_responses_);swap(undo_response_index_,other.undo_response_index_);
 swap(replay_data,other.replay_data);swap(comp_data,other.comp_data);
 swap(replay_size,other.replay_size);swap(comp_size,other.comp_size);swap(data_position,other.data_position);swap(info_offset,other.info_offset);
 swap(is_recording,other.is_recording);swap(is_replaying,other.is_replaying);swap(can_read,other.can_read);
}
bool Replay::LoadUndoReplay(const undo::Bytes& bytes) {
 try {
  if(is_recording || bytes.size()<sizeof(ExtendedReplayHeader) || bytes.size()>sizeof(ExtendedReplayHeader)+MAX_REPLAY_SIZE)return false;
  Replay next;std::memcpy(&next.pheader,bytes.data(),sizeof(next.pheader));
  if(next.pheader.base.id!=REPLAY_ID_YRP2 || !(next.pheader.base.flag&REPLAY_UNDO_CORE) || next.pheader.base.datasize!=bytes.size()-sizeof(next.pheader))return false;
  next.replay_size=bytes.size()-sizeof(next.pheader);std::memcpy(next.replay_data,bytes.data()+sizeof(next.pheader),next.replay_size);
  next.is_replaying=true;next.can_read=true;
  if(!next.ReadInfo())return false;
  next.info_offset=next.data_position;next.data_position=0;
  SwapUndoRecord(next);return true;
 }catch(const std::exception&){return false;}
}
undo::Bytes Replay::ExportUndoReplay() const {
 require(!is_recording && (pheader.base.flag&REPLAY_UNDO_CORE) && replay_size<=MAX_REPLAY_SIZE,"No complete undo replay to export");
 Bytes bytes(sizeof(pheader)+replay_size);std::memcpy(bytes.data(),&pheader,sizeof(pheader));std::memcpy(bytes.data()+sizeof(pheader),replay_data,replay_size);
 Replay checked;require(checked.LoadUndoReplay(bytes),"Invalid undo replay to export");return bytes;
}
const undo::InitialState& Replay::UndoInitial() const {
 require((pheader.base.flag&REPLAY_UNDO_CORE)!=0,"Legacy replay has no undo initialization");return undo_initial_;
}
std::unique_ptr<undo::CoreDriver> Replay::CreateUndoDriver(std::shared_ptr<const undo::ResourceView> resources) const {
 return undo::CoreDriver::Create(UndoInitial(),std::move(resources));
}
bool Replay::ReadUndoResponse(const undo::Checkpoint& before,undo::Bytes& response) {
 require((pheader.base.flag&REPLAY_UNDO_CORE)!=0,"Expected undo replay");
 if(undo_response_index_>=undo_responses_.size())return false;
 const auto& item=undo_responses_[undo_response_index_];
 require(before.player==item.player&&undo::Sha256(before.prompt)==item.promptDigest&&before.transcriptDigest==item.transcriptDigest,"Undo replay input boundary diverged");
 response=item.response;++undo_response_index_;return true;
}
}
