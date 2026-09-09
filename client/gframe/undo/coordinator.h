#pragma once
#include "protocol.h"
#include <deque>
namespace undo {
enum class TxState { Running, WaitBoundary, Consent, Preparing, Committing, Aborting, PausedFailed };
// Created ONLY by host history adapter at the safe boundary, never deserialized.
// digest hashes a public target description + session/epoch/request/index, NOT
// Checkpoint.canonicalState, prompt bytes, transcript, or hidden card identities.
struct AuthoritativeTarget {
 std::uint8_t requester{};
 std::uint64_t index{};
 Digest publicDigest{};
 ClockState clock{};
};
class Coordinator {
public:
 Coordinator(SessionId session, std::uint64_t epoch, bool consentRequired);
 // Unbound request: targetIndex and targetDigest MUST be zero. Seat comes from
 // authenticated connection, not payload. Every accepted request waits for Boundary.
 bool Request(TxKey key, std::uint8_t requester, std::int64_t nowMs);
 void Boundary(std::int64_t nowMs, bool finished,
               std::optional<AuthoritativeTarget> target, ClockState frozenClock);
 bool Consent(TxKey key, std::uint8_t voter, bool approve, std::int64_t nowMs);
 void Tick(std::int64_t nowMs);
 // Host participant readiness includes retained original core, validated candidate,
 // prepared no-fail ownership switch, local display and AI readiness (if present).
 void Ready(TxKey key, std::uint8_t participant);
 void CommitAck(TxKey key, std::uint8_t participant, std::uint64_t installedEpoch);
 void Fail(TxKey key, bool commitMayHaveEscaped);
 void AbortAck(TxKey key, std::uint8_t participant);
 TxState State() const { return state_; }
 std::uint64_t Epoch() const { return epoch_; }
 const TxKey& ActiveKey() const { return active_; }
 // After a transition to Running, apply this clock before enabling inputs.
 // Consent/prepare/abort retain frozen clock; success selects target checkpoint.
 ClockState Clock() const { return clock_; }
 bool AcceptsGameInput(const TxKey& key) const;
 bool AcceptsControl(const TxKey& key) const;
 std::vector<Envelope> TakeOutgoing();
private:
 struct Result { TxKey submitted, bound; std::uint8_t requester; Envelope terminal; };
 void Emit(WireKind kind, Bytes payload={});
 void Complete(bool success, std::uint8_t reason=0);
 void Abort(std::uint8_t reason);
 bool ObserveTime(std::int64_t nowMs);
 bool Expired() const;
 SessionId session_;
 std::uint64_t epoch_{}, highestRequest_{};
 bool consentRequired_{};
 TxState state_{TxState::Running};
 TxKey submitted_{}, active_{};
 std::uint8_t requester_{}, readyMask_{}, ackMask_{}, abortMask_{};
 std::int64_t now_{}, phaseStart_{};
 ClockState clock_{}, targetClock_{};
 std::vector<Envelope> outgoing_;
 std::optional<Envelope> lastEvent_;
 std::deque<Result> finished_;
};
}
