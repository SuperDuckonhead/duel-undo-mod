#include "single_undo.h"
#include <stdexcept>
namespace undo {
SingleUndo::SingleUndo(std::unique_ptr<CoreDriver> live, PrepareUndoFn prepare,
                       RebuildFn rebuild)
    : live_(std::move(live)), prepare_(std::move(prepare)),
      rebuild_(std::move(rebuild)) {
  if (!live_ || !prepare_ || !rebuild_)
    throw std::invalid_argument("SingleUndo requires core and preparation");
  token_.session = NewSessionId();
}
bool SingleUndo::CanUndoLocked(uint8_t p) const {
  return p < 2 && !finished_ && state_ != LocalUndoState::Preparing &&
         state_ != LocalUndoState::WaitBoundary &&
         history_.Target(p).has_value();
}
bool SingleUndo::CanUndo(uint8_t p) const {
  std::lock_guard<std::mutex> l(mutex_);
  return CanUndoLocked(p);
}
bool SingleUndo::Request(uint8_t p) {
  std::lock_guard<std::mutex> l(mutex_);
  if (!CanUndoLocked(p))
    return false;
  requester_ = p;
  state_ = LocalUndoState::WaitBoundary;
  pending_.reset();
  return true;
}
bool SingleUndo::QueueResponse(InputSubmission input,
                               std::optional<ClockState> clock) {
  std::lock_guard<std::mutex> l(mutex_);
  if (finished_ || processing_ || requester_ || pending_ ||
      !(input.token == token_) || input.response.empty() ||
      input.response.size() > 256 ||
      unsigned(input.origin) > unsigned(Origin::Bot))
    return false;
  if (live_->Current().kind != BoundaryKind::AwaitResponse)
    return false;
  pending_ = std::move(input);
  if (clock)
    clock_ = *clock;
  if (state_ == LocalUndoState::Failed) {
    state_ = LocalUndoState::Running;
    error_.clear();
  }
  return true;
}
bool SingleUndo::HasPendingResponse() const {
  std::lock_guard<std::mutex> l(mutex_);
  return pending_.has_value();
}
Boundary SingleUndo::Advance(const CoreDriver::LiveOutput &output) {
  std::optional<InputSubmission> input;
  DuelHistory next;
  {
    std::lock_guard<std::mutex> l(mutex_);
    if (processing_ || requester_)
      throw std::logic_error("Advance requires running owner");
    input = std::move(pending_);
    pending_.reset();
    if (input) {
      auto before = live_->Current().checkpoint;
      before.clock = clock_;
      next = history_;
      next.Accept(
          {before.player, input->origin, input->response, std::move(before)});
    }
    processing_ = true;
  }
  Boundary b;
  try {
    if (input)
      live_->Submit(input->response);
    b = live_->Advance(output);
  } catch (...) {
    std::lock_guard<std::mutex> l(mutex_);
    processing_ = false;
    throw;
  }
  {
    std::lock_guard<std::mutex> l(mutex_);
    processing_ = false;
    if (input && !b.rejectedResponse && b.kind != BoundaryKind::Failed)
      history_ = std::move(next);
    if (!b.rejectedResponse && (input || token_.prompt == 0))
      ++token_.prompt;
    finished_ = b.kind != BoundaryKind::AwaitResponse;
  }
  return b;
}
void SingleUndo::AtBoundary(bool finished) {
  uint8_t requester;
  Checkpoint target;
  DuelHistory next;
  {
    std::lock_guard<std::mutex> l(mutex_);
    if (finished) {
      finished_ = true;
      requester_.reset();
      pending_.reset();
      state_ = LocalUndoState::Running;
      return;
    }
    if (processing_ || !requester_)
      return;
    if (live_->Current().kind != BoundaryKind::AwaitResponse) {
      requester_.reset();
      return;
    }
    requester = *requester_;
    auto keep = history_.Target(requester);
    if (!keep) {
      requester_.reset();
      state_ = LocalUndoState::Running;
      return;
    }
    target = history_.Records().at(*keep).before;
    state_ = LocalUndoState::Preparing;
  }
  try {
    size_t keep;
    std::vector<ResponseRecord> records;
    uint64_t epoch;
    {
      std::lock_guard<std::mutex> l(mutex_);
      keep = *history_.Target(requester);
      records = history_.Records();
      next = history_;
      next.Truncate(keep);
      epoch = token_.epoch + 1;
    }
    auto candidate =
        rebuild_(live_->Initial(), live_->Resources(), records, keep, target);
    auto prepared = prepare_(*candidate, target, epoch);
    if (!prepared)
      throw std::runtime_error("UI preparation returned no candidate");
    {
      std::lock_guard<std::mutex> l(mutex_);
      live_.swap(candidate);
      history_ = std::move(next);
      clock_ = target.clock;
      token_.epoch = epoch;
      ++token_.prompt;
      pending_.reset();
      prepared->Commit();
      requester_.reset();
      state_ = LocalUndoState::Running;
      error_.clear();
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> l(mutex_);
    requester_.reset();
    state_ = LocalUndoState::Failed;
    error_ = e.what();
  }
}
Boundary SingleUndo::Current() const {
  std::lock_guard<std::mutex> l(mutex_);
  return live_->Current();
}
InputToken SingleUndo::Token() const {
  std::lock_guard<std::mutex> l(mutex_);
  return token_;
}
std::vector<ResponseRecord> SingleUndo::History() const {
  std::lock_guard<std::mutex> l(mutex_);
  return history_.Records();
}
ClockState SingleUndo::Clock() const {
  std::lock_guard<std::mutex> l(mutex_);
  return clock_;
}
void SingleUndo::SetClock(ClockState c) {
  std::lock_guard<std::mutex> l(mutex_);
  if (state_ == LocalUndoState::Running || state_ == LocalUndoState::Failed)
    clock_ = c;
}
LocalUndoState SingleUndo::State() const {
  std::lock_guard<std::mutex> l(mutex_);
  return state_;
}
std::string SingleUndo::Error() const {
  std::lock_guard<std::mutex> l(mutex_);
  return error_;
}
PrepareUndoFn SingleUndo::SetPrepare(PrepareUndoFn p) {
  std::lock_guard<std::mutex> l(mutex_);
  if (requester_ || processing_ || !p)
    throw std::logic_error("Preparation can only change while idle");
  prepare_.swap(p);
  return p;
}
} // namespace undo
