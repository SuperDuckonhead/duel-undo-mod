#include "protocol.h"
#include <algorithm>
#include <cerrno>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif
namespace undo {
namespace {
void Put(Bytes& b, std::uint64_t n, unsigned width) {
 for(unsigned i=0;i<width;++i) b.push_back(static_cast<std::uint8_t>(n>>(8*i)));
}
template<std::size_t N> void Put(Bytes& b, const std::array<std::uint8_t,N>& a) { b.insert(b.end(),a.begin(),a.end()); }
class Reader {
public:
 explicit Reader(const Bytes& b): b_(b) {}
 std::uint64_t Get(unsigned width) {
  Require(width); std::uint64_t n=0;
  for(unsigned i=0;i<width;++i) n|=std::uint64_t(b_[at_++])<<(8*i);
  return n;
 }
 template<std::size_t N> void Get(std::array<std::uint8_t,N>& a) {
  Require(N); std::copy_n(b_.begin()+at_,N,a.begin()); at_+=N;
 }
 void Require(std::size_t n) const { if(n>b_.size()-at_) throw std::invalid_argument("truncated undo frame"); }
private:
 const Bytes& b_; std::size_t at_{};
};
bool Valid(WireKind kind) { return kind>=WireKind::Hello && kind<=WireKind::Status; }
bool Valid(RoomMode mode) { return mode==RoomMode::ConsentLan || mode==RoomMode::LoopbackFree; }
constexpr std::size_t FragmentHeader=12, Chunk=MaxPayload-FragmentHeader;
}
Bytes Encode(const Envelope& e) {
 if(!Valid(e.kind) || e.payload.size()>MaxPayload) throw std::invalid_argument("invalid undo kind or payload length");
 Bytes b; b.reserve(WireHeaderSize+e.payload.size());
 Put(b,1,2); Put(b,static_cast<std::uint8_t>(e.kind),1); Put(b,e.key.session);
 Put(b,e.key.epoch,8); Put(b,e.key.request,8); Put(b,e.key.targetIndex,8); Put(b,e.key.targetDigest);
 Put(b,e.payload.size(),4); b.insert(b.end(),e.payload.begin(),e.payload.end()); return b;
}
Envelope Decode(const Bytes& b) {
 Reader r(b); if(r.Get(2)!=1) throw std::invalid_argument("unsupported undo version");
 Envelope e{}; e.kind=static_cast<WireKind>(r.Get(1));
 if(!Valid(e.kind)) throw std::invalid_argument("unknown undo kind");
 r.Get(e.key.session); e.key.epoch=r.Get(8); e.key.request=r.Get(8); e.key.targetIndex=r.Get(8); r.Get(e.key.targetDigest);
 const auto length=r.Get(4);
 if(length>MaxPayload || b.size()-WireHeaderSize!=length) throw std::invalid_argument("invalid undo payload length");
 e.payload.assign(b.begin()+WireHeaderSize,b.end()); return e;
}
bool SameKey(const TxKey& a, const TxKey& b) {
 return a.session==b.session && a.epoch==b.epoch && a.request==b.request && a.targetIndex==b.targetIndex && a.targetDigest==b.targetDigest;
}
bool IsCurrent(const TxKey& k, const SessionId& s, std::uint64_t epoch) { return k.session==s && k.epoch==epoch; }
SessionId NewSessionId() {
 SessionId id{};
#ifdef _WIN32
 if(BCryptGenRandom(nullptr,id.data(),static_cast<ULONG>(id.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
  throw std::runtime_error("system session randomness unavailable");
#else
 std::size_t offset=0;
 while(offset<id.size()) {
  auto n=getrandom(id.data()+offset,id.size()-offset,0);
  if(n<0 && errno==EINTR) continue;
  if(n<=0) throw std::runtime_error("system session randomness unavailable");
  offset+=static_cast<std::size_t>(n);
 }
#endif
 return id;
}
Bytes EncodeHello(const Hello& h) {
 if(h.version!=1 || !Valid(h.mode)) throw std::invalid_argument("unsupported undo hello");
 Bytes b; Put(b,h.version,2); Put(b,h.engine); Put(b,h.rules); Put(b,h.resources); Put(b,static_cast<std::uint8_t>(h.mode),1); return b;
}
Hello DecodeHello(const Bytes& b) {
 if(b.size()!=99) throw std::invalid_argument("invalid hello length");
 Reader r(b); Hello h; h.version=static_cast<std::uint16_t>(r.Get(2)); r.Get(h.engine); r.Get(h.rules); r.Get(h.resources); h.mode=static_cast<RoomMode>(r.Get(1));
 if(h.version!=1 || !Valid(h.mode)) throw std::invalid_argument("unsupported undo hello");
 return h;
}
std::string Compatibility(const Hello& local, const std::optional<Hello>& peer) {
 if(!peer) return "undo capability missing";
 if(local.version!=1 || peer->version!=1) return "protocol version";
 if(!Valid(local.mode) || !Valid(peer->mode) || local.mode!=peer->mode) return "room mode";
 if(local.engine!=peer->engine) return "engine fingerprint";
 if(local.rules!=peer->rules) return "rules fingerprint";
 if(local.resources!=peer->resources) return "logical resources fingerprint";
 return {};
}
std::vector<Bytes> Fragment(const Bytes& stream) {
 if(stream.size()>MaxRestoreBytes) throw std::invalid_argument("restore stream too large");
 const auto count=std::max<std::size_t>(1,(stream.size()+Chunk-1)/Chunk);
 std::vector<Bytes> parts;
 for(std::size_t i=0;i<count;++i) {
  Bytes b; Put(b,i,4); Put(b,count,4); Put(b,stream.size(),4);
  const auto start=i*Chunk, end=std::min(stream.size(),start+Chunk);
  b.insert(b.end(),stream.begin()+start,stream.begin()+end); parts.push_back(std::move(b));
 }
 return parts;
}
void FragmentAssembler::Add(const Bytes& b) {
 try {
  if(failed_ || b.size()<FragmentHeader || b.size()>MaxPayload) throw std::invalid_argument("invalid restore fragment");
  Reader r(b); auto index=r.Get(4), count=r.Get(4), total=r.Get(4);
  if(total>MaxRestoreBytes || count!=std::max<std::uint64_t>(1,(total+Chunk-1)/Chunk) || index!=next_ || index>=count)
   throw std::invalid_argument("restore fragment sequence or limit");
  if(next_ && (count!=count_ || total!=total_)) throw std::invalid_argument("restore fragment metadata changed");
  const auto expected=std::min<std::uint64_t>(Chunk,total-index*Chunk);
  if(b.size()-FragmentHeader!=expected) throw std::invalid_argument("restore fragment size mismatch");
  count_=static_cast<std::uint32_t>(count); total_=static_cast<std::uint32_t>(total);
  data_.insert(data_.end(),b.begin()+FragmentHeader,b.end()); ++next_;
 } catch(...) { failed_=true; throw; }
}
Bytes FragmentAssembler::Finish() const {
 if(failed_ || !count_ || next_!=count_ || data_.size()!=total_) throw std::invalid_argument("incomplete restore stream");
 return data_;
}
}
