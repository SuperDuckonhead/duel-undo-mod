#include "coordinator.h"
#include <limits>
namespace undo {
namespace {
Bytes EpochPayload(std::uint64_t epoch) {
 Bytes b; for(unsigned i=0;i<8;++i) b.push_back(static_cast<std::uint8_t>(epoch>>(8*i))); return b;
}
}
Coordinator::Coordinator(SessionId s,std::uint64_t e,bool c):session_(s),epoch_(e),consentRequired_(c) {}
bool Coordinator::ObserveTime(std::int64_t nowMs) {
 if(nowMs<0 || nowMs<now_) return false;
 now_=nowMs; return true;
}
bool Coordinator::Expired() const {
 // Both operands nonnegative, monotonically observed; subtraction cannot overflow.
 return now_-phaseStart_>=30000;
}
void Coordinator::Emit(WireKind kind,Bytes payload) {
 lastEvent_=Envelope{kind,active_,std::move(payload)}; outgoing_.push_back(*lastEvent_);
}
bool Coordinator::Request(TxKey key,std::uint8_t requester,std::int64_t nowMs) {
 if(requester>1 || !ObserveTime(nowMs) || key.session!=session_ || !key.request ||
    key.targetIndex!=0 || key.targetDigest!=Digest{}) return false;
 if(state_!=TxState::Running) {
  if(state_==TxState::PausedFailed || requester!=requester_ || !SameKey(key,submitted_)) return false;
  if(lastEvent_) outgoing_.push_back(*lastEvent_);
  return true;
 }
 for(const auto& result:finished_) {
  if(SameKey(key,result.submitted) && requester==result.requester) {
   outgoing_.push_back(result.terminal); return true;
  }
 }
 if(!IsCurrent(key,session_,epoch_) || epoch_==std::numeric_limits<std::uint64_t>::max() ||
    key.request<=highestRequest_) return false;
 submitted_=active_=key; requester_=requester; highestRequest_=key.request;
 readyMask_=ackMask_=abortMask_=0; lastEvent_.reset(); state_=TxState::WaitBoundary;
 return true;
}
void Coordinator::Boundary(std::int64_t nowMs,bool finished,
                           std::optional<AuthoritativeTarget> target,ClockState frozenClock) {
 if(state_!=TxState::WaitBoundary || !ObserveTime(nowMs)) return;
 clock_=frozenClock;
 if(finished || !target || target->requester!=requester_) { Complete(false,1); return; }
 active_.targetIndex=target->index; active_.targetDigest=target->publicDigest; targetClock_=target->clock;
 phaseStart_=now_;
 if(consentRequired_) { state_=TxState::Consent; Emit(WireKind::Consent); }
 else { state_=TxState::Preparing; Emit(WireKind::Prepare); }
}
bool Coordinator::Consent(TxKey key,std::uint8_t voter,bool approve,std::int64_t nowMs) {
 if(!ObserveTime(nowMs)) return false;
 if(state_!=TxState::Consent || !SameKey(key,active_) || voter>1 || voter==requester_) return false;
 if(Expired()) { Complete(false,2); return false; }
 if(!approve) { Complete(false,3); return true; }
 state_=TxState::Preparing; phaseStart_=now_; Emit(WireKind::Prepare); return true;
}
void Coordinator::Tick(std::int64_t nowMs) {
 if(!ObserveTime(nowMs) || !Expired()) return;
 if(state_==TxState::Consent) Complete(false,2);
 else if(state_==TxState::Preparing) Abort(4);
 else if(state_==TxState::Committing || state_==TxState::Aborting) state_=TxState::PausedFailed;
}
void Coordinator::Ready(TxKey key,std::uint8_t participant) {
 if(state_!=TxState::Preparing || !SameKey(key,active_) || participant>1) return;
 readyMask_|=static_cast<std::uint8_t>(1u<<participant);
 if(readyMask_==3) {
  state_=TxState::Committing; phaseStart_=now_; Emit(WireKind::Commit,EpochPayload(epoch_+1));
 }
}
void Coordinator::CommitAck(TxKey key,std::uint8_t participant,std::uint64_t installedEpoch) {
 if(participant>1) return;
 if(state_==TxState::Running) {
  for(const auto& result:finished_) {
   if(result.terminal.kind==WireKind::Resume && SameKey(key,result.bound) &&
      installedEpoch==result.bound.epoch+1) { outgoing_.push_back(result.terminal); return; }
  }
  return;
 }
 if(state_!=TxState::Committing || !SameKey(key,active_) || installedEpoch!=epoch_+1) return;
 ackMask_|=static_cast<std::uint8_t>(1u<<participant);
 if(ackMask_==3) Complete(true);
}
void Coordinator::Abort(std::uint8_t reason) {
 state_=TxState::Aborting; phaseStart_=now_; abortMask_=0; Emit(WireKind::Abort,{reason});
}
void Coordinator::Fail(TxKey key,bool commitMayHaveEscaped) {
 if(!AcceptsControl(key)) return;
 if(commitMayHaveEscaped || state_==TxState::Committing) { state_=TxState::PausedFailed; return; }
 if(state_==TxState::Preparing) Abort(4);
 else if(state_==TxState::Consent || state_==TxState::WaitBoundary) Complete(false,4);
}
void Coordinator::AbortAck(TxKey key,std::uint8_t participant) {
 if(state_!=TxState::Aborting || !SameKey(key,active_) || participant>1) return;
 abortMask_|=static_cast<std::uint8_t>(1u<<participant);
 if(abortMask_==3) Complete(false,4);
}
void Coordinator::Complete(bool success,std::uint8_t reason) {
 if(success) { ++epoch_; clock_=targetClock_; Emit(WireKind::Resume,EpochPayload(epoch_)); }
 else Emit(WireKind::Abort,{reason});
 finished_.push_back({submitted_,active_,requester_,*lastEvent_});
 // Old evicted request IDs remain rejected by highestRequest_, never reexecuted.
 if(finished_.size()>64) finished_.pop_front();
 state_=TxState::Running;
}
bool Coordinator::AcceptsGameInput(const TxKey& key) const {
 return state_==TxState::Running && IsCurrent(key,session_,epoch_);
}
bool Coordinator::AcceptsControl(const TxKey& key) const {
 return state_!=TxState::Running && state_!=TxState::PausedFailed && SameKey(key,active_);
}
std::vector<Envelope> Coordinator::TakeOutgoing() {
 std::vector<Envelope> result; result.swap(outgoing_); return result;
}
}
