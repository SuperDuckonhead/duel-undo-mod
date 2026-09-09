#include "room_admission.h"
#include <stdexcept>
namespace undo {
namespace {
void require(bool value,const char* reason) {if(!value) throw std::invalid_argument(reason);}
void common(const Hello& expected,Hello peer) {
    // Before the host challenge, a joining client does not know room mode.
    // The confirmed echo below checks the actual host mode exactly.
    peer.mode=expected.mode;
    const auto mismatch=Compatibility(expected,peer);
    if(!mismatch.empty()) throw std::invalid_argument(mismatch);
}
void handshakeKey(const Envelope& e) {
    require(e.kind==WireKind::Hello && !e.key.epoch && !e.key.targetIndex &&
            e.key.targetDigest==Digest{}, "Invalid capability envelope");
}
}
RoomAdmission::RoomAdmission(Hello expected,bool actualListenerLoopback)
    :expected_(std::move(expected)),session_(NewSessionId()) {
    EncodeHello(expected_);
    require(expected_.mode!=RoomMode::LoopbackFree || actualListenerLoopback,
            "Free undo requires an actual loopback-only listener");
}
HandshakeResult RoomAdmission::Receive(RoomPeer& peer,const Envelope& e) const {
    handshakeKey(e);
    auto capability=DecodeHello(e.payload);
    if(e.key.session==SessionId{}) {
        require(!e.key.request && !peer.challenged, "Unexpected capability advertisement");
        common(expected_,capability);
        peer.offered=std::move(capability);
        return HandshakeResult::Offered;
    }
    require(peer.challenged && e.key.session==session_ && !e.key.request,
            "Unknown capability challenge");
    const auto mismatch=Compatibility(expected_,capability);
    if(!mismatch.empty()) throw std::invalid_argument(mismatch);
    require(expected_.mode!=RoomMode::LoopbackFree || peer.loopback,
            "Free undo rejects non-loopback peers");
    peer.ready=true;
    return HandshakeResult::Confirmed;
}
Envelope RoomAdmission::Challenge(RoomPeer& peer) const {
    require(peer.offered.has_value(), "Undo capability missing");
    common(expected_,*peer.offered);
    require(expected_.mode!=RoomMode::LoopbackFree || peer.loopback,
            "Free undo rejects non-loopback peers");
    peer.challenged=true;
    return {WireKind::Hello,{session_,0,0,0,{}},EncodeHello(expected_)};
}
Envelope RoomAdmission::Confirmation() const {
    return {WireKind::Hello,{session_,0,1,0,{}},EncodeHello(expected_)};
}
ClientRoomHandshake::ClientRoomHandshake(Hello local,bool connectedToLoopback)
    :local_(std::move(local)),loopback_(connectedToLoopback) {EncodeHello(local_);}
Envelope ClientRoomHandshake::Offer() const {return {WireKind::Hello,{},EncodeHello(local_)};}
std::optional<Envelope> ClientRoomHandshake::Receive(const Envelope& e) {
    handshakeKey(e);
    require(e.key.session!=SessionId{} && e.key.request<=1, "Invalid host capability identity");
    auto capability=DecodeHello(e.payload);
    common(local_,capability);
    require(capability.mode!=RoomMode::LoopbackFree || loopback_,
            "Remote server cannot advertise loopback-free undo");
    if(negotiated_) {
        require(e.key.session==session_ && Compatibility(*negotiated_,capability).empty(),
                "Host capability changed");
    }
    if(e.key.request==1) {
        require(negotiated_.has_value(), "Unsolicited capability confirmation");
        ready_=true;
        return std::nullopt;
    }
    session_=e.key.session;negotiated_=std::move(capability);
    return Envelope{WireKind::Hello,e.key,EncodeHello(*negotiated_)};
}
} // namespace undo
