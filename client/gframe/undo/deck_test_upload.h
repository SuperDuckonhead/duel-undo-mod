#pragma once
#include "../network.h"
#include "../deck_manager.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace undo {
// Native card locations/sequences and selection counts use one byte. Retain
// the existing 250-card ingress budget for the entire main+extra population,
// including when cards subsequently move into one zone. This is not 60/15
// legality. The explicit wire split never asks the host to infer zones.
constexpr std::size_t TestDeckCapacity = ygo::MAINC_MAX;
constexpr std::uint8_t TestDeckUploadOpcode = 0x7d;
constexpr std::uint8_t TestDeckResultOpcode = 0x7d;
constexpr std::size_t TestDeckIdentitySize = 24; // generation u64 + session[16]
struct TestDuelConfig {
 const std::uint64_t generation;
 const std::vector<std::uint32_t> main, extra;
 const ygo::HostInfo host;
 const std::wstring name;
 TestDuelConfig(std::uint64_t g,std::vector<std::uint32_t> m,std::vector<std::uint32_t> e,
                ygo::HostInfo h,std::wstring n)
   :generation(g),main(std::move(m)),extra(std::move(e)),host(h),name(std::move(n)) {}
};
enum class TestDeckError : std::uint8_t { None, Capacity, Malformed, UnknownCard, Token, WrongZone, Mismatch, Admission, Transport, Duel };
inline const wchar_t* TestDeckErrorText(TestDeckError e) {
 switch(e) {
 case TestDeckError::Capacity:return L"当前测试支持主卡组与额外卡组合计最多 250 张";
 case TestDeckError::Malformed:return L"测试卡组消息长度或格式错误";
 case TestDeckError::UnknownCard:return L"测试卡组包含无法解析的卡片";
 case TestDeckError::Token:return L"测试卡组不能包含衍生物";
 case TestDeckError::WrongZone:return L"测试卡片不支持当前主／额外卡组分区";
 case TestDeckError::Mismatch:return L"测试卡组与本次编辑快照不一致";
 case TestDeckError::Admission:return L"测试房间尚未确认本次主机席位";
 case TestDeckError::Transport:return L"测试连接已关闭或上传失败";
 case TestDeckError::Duel:return L"对局引擎或悠悠王运行失败，已结束本次测试并恢复编辑器";
 default:return L"";
 }
}
struct TestDeckFailure : std::runtime_error {
 TestDeckError error;
 explicit TestDeckFailure(TestDeckError e):std::runtime_error("Test deck upload rejected"),error(e) {}
};
inline void TestDeckPut(Bytes& b,std::uint64_t n,unsigned width) {
 for(unsigned i=0;i<width;++i)b.push_back(std::uint8_t(n>>(i*8)));
}
inline std::uint64_t TestDeckRead(const Bytes& b,std::size_t at,unsigned width) {
 if(at>b.size() || width>b.size()-at)throw TestDeckFailure(TestDeckError::Malformed);
 std::uint64_t n{};for(unsigned i=0;i<width;++i)n|=std::uint64_t(b[at+i])<<(i*8);return n;
}
inline Bytes TestDeckIdentity(std::uint64_t generation,const SessionId& session) {
 Bytes b;TestDeckPut(b,generation,8);b.insert(b.end(),session.begin(),session.end());return b;
}
inline bool MatchesTestDeck(const Bytes& b,std::uint64_t generation,const SessionId& session) {
 return b.size()>=TestDeckIdentitySize && TestDeckRead(b,0,8)==generation && std::equal(session.begin(),session.end(),b.begin()+8);
}
inline Bytes EncodeTestDeck(const TestDuelConfig& config,const SessionId& session) {
 if(config.main.size()>TestDeckCapacity || config.extra.size()>TestDeckCapacity-config.main.size())
  throw TestDeckFailure(TestDeckError::Capacity);
 const auto size=TestDeckIdentitySize+8+4*(config.main.size()+config.extra.size());
 if(size>ygo::MAX_DATA_SIZE)throw TestDeckFailure(TestDeckError::Capacity);
 auto b=TestDeckIdentity(config.generation,session);b.reserve(size);
 TestDeckPut(b,config.main.size(),4);TestDeckPut(b,config.extra.size(),4);
 for(auto code:config.main)TestDeckPut(b,code,4);
 for(auto code:config.extra)TestDeckPut(b,code,4);
 return b;
}
inline Bytes TestDeckResult(std::uint64_t generation,const SessionId& session,TestDeckError error) {
 auto b=TestDeckIdentity(generation,session);b.push_back(static_cast<std::uint8_t>(error));return b;
}
// One instance belongs to one acquired client connection. Every externally
// delivered transition carries that instance's generation; capability supplies
// the authenticated server session, also required on acceptance/error replies.
class DeckTestUpload {
public:
 struct Status {
  bool joined{},hostSeat{},capability{},uploaded{},readySent{},humanReady{},opponentReady{},started{},finished{},cancelled{},closed{};
  TestDeckError error{};
  SessionId session{};
 };
 explicit DeckTestUpload(std::shared_ptr<const TestDuelConfig> config):config_(std::move(config)) {}
 std::shared_ptr<const TestDuelConfig> Config() const {return config_;}
 Status Inspect() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
 void Joined(std::uint64_t g){std::lock_guard<std::mutex> lock(mutex_);if(current(g))state_.joined=true;}
 void Seat(std::uint64_t g,std::uint8_t type){std::lock_guard<std::mutex> lock(mutex_);if(current(g))state_.hostSeat=type==0x10;}
 void Capability(std::uint64_t g,const SessionId& session){
  std::lock_guard<std::mutex> lock(mutex_);if(!current(g))return;
  if(state_.capability && state_.session!=session)return;
  state_.session=session;state_.capability=true;
 }
 std::optional<Bytes> TakeUpload(std::uint64_t g){
  std::lock_guard<std::mutex> lock(mutex_);
  if(!admitted(g)||state_.uploaded)return std::nullopt;
  try{auto b=EncodeTestDeck(*config_,state_.session);state_.uploaded=true;return b;}
  catch(const TestDeckFailure& e){state_.error=e.error;return std::nullopt;}
 }
 bool Accept(std::uint64_t g,const Bytes& result){
  std::lock_guard<std::mutex> lock(mutex_);
  if(!admitted(g)||!state_.uploaded||state_.readySent||result.size()!=25||!MatchesTestDeck(result,g,state_.session))return false;
  if(result[24]>static_cast<std::uint8_t>(TestDeckError::Transport)){state_.error=TestDeckError::Malformed;return false;}
  state_.error=static_cast<TestDeckError>(result[24]);
  if(state_.error!=TestDeckError::None)return false;
  state_.readySent=true;return true;
 }
 void Ready(std::uint64_t g,std::uint8_t status){
  std::lock_guard<std::mutex> lock(mutex_);if(!current(g))return;
  if((status>>4)==0)state_.humanReady=(status&15)==PLAYERCHANGE_READY;
  if((status>>4)==1)state_.opponentReady=(status&15)==PLAYERCHANGE_READY;
 }
 void Fail(std::uint64_t g,TestDeckError e){std::lock_guard<std::mutex> lock(mutex_);if(current(g))state_.error=e;}
 void Started(std::uint64_t g){std::lock_guard<std::mutex> lock(mutex_);if(current(g))state_.started=true;}
 void Finished(std::uint64_t g){std::lock_guard<std::mutex> lock(mutex_);if(current(g))state_.finished=true;}
 void Cancel(std::uint64_t g){std::lock_guard<std::mutex> lock(mutex_);if(g==config_->generation)state_.cancelled=true;}
 void Closed(std::uint64_t g){std::lock_guard<std::mutex> lock(mutex_);if(g==config_->generation){state_.closed=true;state_.cancelled=true;}}
private:
 bool current(std::uint64_t g) const {return g==config_->generation&&!state_.cancelled&&state_.error==TestDeckError::None;}
 bool admitted(std::uint64_t g) const {return current(g)&&state_.joined&&state_.hostSeat&&state_.capability;}
 std::shared_ptr<const TestDuelConfig> config_;
 mutable std::mutex mutex_;
 Status state_;
};
} // namespace undo
