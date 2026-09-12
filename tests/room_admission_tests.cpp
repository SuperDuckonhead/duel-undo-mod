#include "test_support.h"
#include "undo/room_admission.h"
#include <iostream>
template<class F> bool rejects(F f) { try {f();} catch(const std::exception&) {return true;} return false;}
int main(){try {
 using namespace undo;
 Hello local;local.engine[0]=4;local.rules[0]=5;local.resources[0]=6;
 RoomAdmission room(local,false);
 RoomPeer peer;peer.loopback=false;
 CHECK(rejects([&]{room.Challenge(peer);}));
 ClientRoomHandshake client(local,false);
 CHECK(room.Receive(peer,client.Offer())==HandshakeResult::Offered);
 auto challenge=room.Challenge(peer);CHECK(!peer.ready);
 auto answer=client.Receive(challenge);CHECK(answer && !client.Ready());
 CHECK(room.Receive(peer,*answer)==HandshakeResult::Confirmed);CHECK(peer.ready);
 CHECK(!client.Receive(room.Confirmation()));CHECK(client.Ready());
 CHECK(client.Session()==room.Session());
 auto legacyOffer=client.Offer();legacyOffer.payload[0]=1;
 RoomPeer legacyPeer;
 CHECK(rejects([&]{room.Receive(legacyPeer,legacyOffer);}));
 CHECK(!legacyPeer.ready && !legacyPeer.offered);
 auto legacyChallenge=challenge;legacyChallenge.payload[0]=1;
 CHECK(rejects([&]{client.Receive(legacyChallenge);}));
 CHECK(room.Receive(peer,*answer)==HandshakeResult::Confirmed); // idempotent ack
 CHECK(!client.Receive(room.Confirmation()));CHECK(client.Ready());
 auto stale=*answer;stale.key.session[1]^=1;CHECK(rejects([&]{room.Receive(peer,stale);}));
 auto wrong=room.Confirmation();wrong.key.epoch=1;CHECK(rejects([&]{client.Receive(wrong);}));
 for(int field=0;field<3;++field) {
  auto differing=local;if(field==0)++differing.engine[1];if(field==1)++differing.rules[1];if(field==2)++differing.resources[1];
  RoomPeer bad;ClientRoomHandshake incompatible(differing,false);
  CHECK(rejects([&]{room.Receive(bad,incompatible.Offer());}));
 }
 auto freeHello=local;freeHello.mode=RoomMode::LoopbackFree;
 CHECK(rejects([&]{RoomAdmission(freeHello,false);}));
 RoomAdmission freeRoom(freeHello,true);
 RoomPeer remote;remote.loopback=false;freeRoom.Receive(remote,client.Offer());
 CHECK(rejects([&]{freeRoom.Challenge(remote);}));
 RoomPeer loop;loop.loopback=true;ClientRoomHandshake loopClient(local,true);
 freeRoom.Receive(loop,loopClient.Offer());auto freeChallenge=freeRoom.Challenge(loop);
 auto echo=loopClient.Receive(freeChallenge);CHECK(echo);
 CHECK(DecodeHello(echo->payload).mode==RoomMode::LoopbackFree);
 CHECK(freeRoom.Receive(loop,*echo)==HandshakeResult::Confirmed);
 loopClient.Receive(freeRoom.Confirmation());CHECK(loopClient.Ready());
 ClientRoomHandshake remoteClient(local,false);
 CHECK(rejects([&]{remoteClient.Receive(freeChallenge);}));
 RoomPeer changed;changed.loopback=true;freeRoom.Receive(changed,loopClient.Offer());
 auto offer=freeRoom.Challenge(changed);auto badAck=offer;badAck.payload=EncodeHello(local);
 CHECK(rejects([&]{freeRoom.Receive(changed,badAck);}));
 CHECK(!changed.ready);
 ClientRoomHandshake unchallenged(local,false);
 CHECK(rejects([&]{unchallenged.Receive(room.Confirmation());}));
 std::cout<<"two-sided capability challenge/confirmation, immutable session and actual loopback prerequisites passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
