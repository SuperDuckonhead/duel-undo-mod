#pragma once
#include "protocol.h"
namespace undo {
struct RoomPeer {
    // Set only from the accepted socket's real peer address by NetServer.
    bool loopback{};
    std::optional<Hello> offered;
    bool challenged{}, ready{};
};
enum class HandshakeResult { Offered, Confirmed };
// This admission handshake does not replace the host's response/transaction
// gate. A peer is Ready only after echoing the host's exact session and mode.
class RoomAdmission {
public:
    RoomAdmission(Hello expected, bool actualListenerLoopback);
    HandshakeResult Receive(RoomPeer&, const Envelope&) const;
    Envelope Challenge(RoomPeer&) const;
    Envelope Confirmation() const;
    const SessionId& Session() const { return session_; }
    const Hello& Capability() const { return expected_; }
private:
    Hello expected_;
    SessionId session_;
};
class ClientRoomHandshake {
public:
    // loopback means the actual connected server address, not a user flag.
    ClientRoomHandshake(Hello local, bool connectedToLoopback);
    Envelope Offer() const;
    std::optional<Envelope> Receive(const Envelope&);
    bool Ready() const { return ready_; }
    const SessionId& Session() const { return session_; }
    const std::optional<Hello>& Negotiated() const { return negotiated_; }
private:
    Hello local_;
    bool loopback_{}, ready_{};
    SessionId session_{};
    std::optional<Hello> negotiated_;
};
} // namespace undo
