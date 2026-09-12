#include "test_support.h"
#include "undo/protocol.h"
#include <limits>
#include <stdexcept>
template<class F> bool Rejects(F f) { try { f(); } catch(const std::exception&) { return true; } return false; }
int main() {
 using namespace undo;
 TxKey key{}; key.epoch=0x0807060504030201ULL; key.request=UINT64_MAX; key.targetIndex=9;
 key.session[0]=17; key.targetDigest[31]=42;
 Envelope e{WireKind::Request,key,{1,2,3}};
 auto bytes=Encode(e);
 CHECK(bytes.size()==82);
 CHECK(bytes[0]==1 && bytes[1]==0 && bytes[2]==3 && bytes[3]==17);
 CHECK(bytes[19]==1 && bytes[26]==8 && bytes[27]==255 && bytes[74]==42);
 CHECK(bytes[75]==3 && bytes[76]==0 && bytes[79]==1);
 auto decoded=Decode(bytes);
 CHECK(SameKey(decoded.key,key) && decoded.payload==e.payload);
 for(std::size_t n=0;n<bytes.size();++n) CHECK(Rejects([&]{ Decode(Bytes(bytes.begin(),bytes.begin()+n)); }));
 auto bad=bytes; bad.push_back(0); CHECK(Rejects([&]{Decode(bad);}));
 for(auto offset : {0,1,2,75,78}) { bad=bytes; bad[offset]=255; CHECK(Rejects([&]{Decode(bad);})); }
 bad=bytes; bad[2]=0; CHECK(Rejects([&]{Decode(bad);}));
 for(int k=1;k<=14;++k) for(auto value : {std::uint64_t(0),UINT64_MAX}) {
  e.kind=static_cast<WireKind>(k); e.key.epoch=value; e.key.request=value; e.key.targetIndex=value; e.payload.clear();
  CHECK(SameKey(Decode(Encode(e)).key,e.key));
 }
 e.payload.assign(MaxPayload,7); CHECK(Decode(Encode(e)).payload.size()==MaxPayload);
 e.payload.push_back(1); CHECK(Rejects([&]{Encode(e);}));
 e.payload.clear(); e.kind=static_cast<WireKind>(15); CHECK(Rejects([&]{Encode(e);}));
 CHECK(IsCurrent(key,key.session,key.epoch)); CHECK(!IsCurrent(key,key.session,0));
 auto other=key; ++other.session[1]; CHECK(!IsCurrent(other,key.session,key.epoch));
 other=key; ++other.targetIndex; CHECK(!SameKey(other,key));
 other=key; ++other.targetDigest[0]; CHECK(!SameKey(other,key));
 CHECK(NewSessionId()!=SessionId{}); CHECK(NewSessionId()!=NewSessionId());
 Hello hello; hello.engine[2]=9; hello.resources[31]=7;
 CHECK(hello.version==2); // v1 meant executable/rules/resource file equality.
 CHECK(EncodeHello(hello).size()==99);
 CHECK(Compatibility(hello,DecodeHello(EncodeHello(hello))).empty());
 bad=EncodeHello(hello); bad[0]=1;
 CHECK(Rejects([&]{DecodeHello(bad);}));
 auto legacy=hello;legacy.version=1;
 CHECK(!Compatibility(hello,legacy).empty());
 CHECK(Rejects([&]{EncodeHello(legacy);}));
 CHECK(!Compatibility(hello,std::nullopt).empty());
 for(int field=0;field<5;++field) {
  auto peer=hello;
  if(field==0) ++peer.version; if(field==1) ++peer.engine[0]; if(field==2) ++peer.rules[0];
  if(field==3) ++peer.resources[0]; if(field==4) peer.mode=RoomMode::LoopbackFree;
  CHECK(!Compatibility(hello,peer).empty());
 }
 bad=EncodeHello(hello); bad.back()=0; CHECK(Rejects([&]{DecodeHello(bad);}));
 bad=EncodeHello(hello); bad.push_back(0); CHECK(Rejects([&]{DecodeHello(bad);}));
 for(auto size : {std::size_t(0),std::size_t(1),MaxPayload,MaxRestoreBytes}) {
  Bytes stream(size,23); auto parts=Fragment(stream); FragmentAssembler a;
  CHECK(!parts.empty()); for(const auto& part:parts) { CHECK(part.size()<=MaxPayload); a.Add(part); }
  CHECK(a.Finish()==stream); CHECK(Rejects([&]{a.Add(parts.back());}));
  CHECK(Rejects([&]{a.Finish();}));
 }
 CHECK(Rejects([&]{Fragment(Bytes(MaxRestoreBytes+1));}));
 auto parts=Fragment(Bytes(MaxPayload,4)); CHECK(parts.size()==2);
 FragmentAssembler missing; missing.Add(parts[0]); CHECK(Rejects([&]{missing.Finish();}));
 FragmentAssembler order; CHECK(Rejects([&]{order.Add(parts[1]);}));
 FragmentAssembler forged; bad=parts[0]; bad[4]=255; CHECK(Rejects([&]{forged.Add(bad);}));
 FragmentAssembler truncated; bad=parts[0]; bad.pop_back(); CHECK(Rejects([&]{truncated.Add(bad);}));
}
