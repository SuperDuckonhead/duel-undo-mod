#include "test_support.h"
#include "undo/room_wire.h"
#include <iostream>
template<class F> bool rejects(F f) {try {f();} catch(const std::exception&) {return true;} return false;}
int main() {try {
 using namespace undo;
 SessionId session{};session[0]=42;
 Bytes packet(150000,0x35);packet.front()=1;packet.back()=0xab;
 auto frames=EncodeGamePacket(session,7,19,1,packet);
 CHECK(frames.size()>1);
 GamePacketStream stream(session,7);
 for(std::size_t i=0;i<frames.size();++i) {
  auto got=stream.Add(Decode(Encode(frames[i])));
  if(i+1<frames.size())CHECK(!got);
  else CHECK(got && got->packet==packet && got->prompt==19 && got->sequence==1);
 }
 CHECK(!stream.Add(frames.back())); // A completed packet is not applied twice.
 auto next=EncodeGamePacket(session,7,20,2,{1,40,2});
 CHECK(stream.Add(next.front())->packet==Bytes({1,40,2}));
 auto stale=EncodeGamePacket(session,6,999,3,{1,99});
 CHECK(!stream.Add(stale.front()));CHECK(stream.LastSequence()==2);
 auto other=stale.front();other.key.session[1]=9;CHECK(!stream.Add(other));
 stream.Reset(session,8);
 CHECK(!stream.Add(next.front()));
 CHECK(stream.Add(EncodeGamePacket(session,8,21,1,{1,2}).front())->prompt==21);
 for(int fault=0;fault<4;++fault) {
  GamePacketStream broken(session,7);auto first=frames.front();
  if(fault==0)first=frames[1];
  if(fault==1)first.key.targetDigest[0]=1;
  if(fault==2)first.payload.pop_back();
  if(fault==3)first.key.targetIndex=2;
  CHECK(rejects([&]{broken.Add(first);}));
  CHECK(rejects([&]{broken.Add(frames.front());}));
 }
 GamePacketStream mixed(session,7);CHECK(!mixed.Add(frames.front()));
 auto changed=frames[1];++changed.key.request;
 CHECK(rejects([&]{mixed.Add(changed);}));
 CHECK(rejects([&]{EncodeGamePacket(session,0,0,0,{1});}));
 CHECK(rejects([&]{EncodeGamePacket(session,0,0,1,{});}));
 CHECK(rejects([&]{EncodeGamePacket(session,0,0,1,Bytes(MaxGamePacket+1));}));
 TxKey input{session,8,21,0,{}};
 Bytes response(256,0x41);
 auto e=EncodeResponse(input,Origin::Automatic,response);
 auto submitted=DecodeResponse(Decode(Encode(e)));
 CHECK(submitted.origin==Origin::Automatic && submitted.response==response && SameKey(submitted.key,input));
 CHECK(rejects([&]{EncodeResponse(input,Origin::Bot,response);}));
 CHECK(rejects([&]{EncodeResponse(input,Origin::Manual,Bytes(257));}));
 CHECK(rejects([&]{EncodeResponse(input,Origin::Manual,{});}));
 for(std::size_t n=0;n<e.payload.size();++n){auto cut=e;cut.payload.resize(n);CHECK(rejects([&]{DecodeResponse(cut);}));}
 auto bad=e;bad.payload[0]=2;CHECK(rejects([&]{DecodeResponse(bad);}));
 bad=e;bad.key.targetIndex=1;CHECK(rejects([&]{DecodeResponse(bad);}));
 bad=e;bad.key.targetDigest[0]=1;CHECK(rejects([&]{DecodeResponse(bad);}));
 RoomStatus status;status.state=TxState::Consent;status.eligibleMask=3;status.promptPlayer=1;status.timePlayer=1;status.nextRequest=UINT64_MAX;status.prompt=21;status.clock.remainingMs={123456,234567};
 auto bytes=EncodeRoomStatus(status);auto decoded=DecodeRoomStatus(bytes);
 CHECK(decoded.state==status.state && decoded.nextRequest==UINT64_MAX && decoded.prompt==21 && decoded.clock.remainingMs==status.clock.remainingMs);
 for(std::size_t n=0;n<bytes.size();++n)CHECK(rejects([&]{DecodeRoomStatus(Bytes(bytes.begin(),bytes.begin()+n));}));
 bytes.push_back(0);CHECK(rejects([&]{DecodeRoomStatus(bytes);}));
 status.clock.remainingMs[0]=-1;CHECK(rejects([&]{EncodeRoomStatus(status);}));
 std::cout<<"epoch game fragments, duplicate/stale/reordered fail-closed packets, exact256 responses and public status passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
