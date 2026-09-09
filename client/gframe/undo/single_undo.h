#pragma once
#include "protocol.h"
#include "rebuilder.h"
#include <functional>
#include <mutex>
namespace undo {
enum class LocalUndoState { Running, WaitBoundary, Preparing, Failed };
struct InputToken {
  SessionId session{};
  uint64_t epoch{}, prompt{};
};
inline bool operator==(const InputToken &a, const InputToken &b) {
  return a.session == b.session && a.epoch == b.epoch && a.prompt == b.prompt;
}
struct InputSubmission {
  Bytes response;
  Origin origin{Origin::Manual};
  InputToken token;
};
// Every fallible model/widget preparation finishes before this object is
// returned. The owner holds any required rendering lock through the no-fail
// install.
struct PreparedUndo {
  virtual ~PreparedUndo() = default;
  virtual void Commit() noexcept = 0;
};
using RebuildFn = std::function<std::unique_ptr<CoreDriver>(
    const InitialState &, std::shared_ptr<const ResourceView>,
    const std::vector<ResponseRecord> &, size_t, const Checkpoint &)>;
using PrepareUndoFn = std::function<std::unique_ptr<PreparedUndo>(
    const CoreDriver &, const Checkpoint &, uint64_t)>;
class SingleUndo {
public:
  SingleUndo(std::unique_ptr<CoreDriver>, PrepareUndoFn, RebuildFn = Rebuild);
  bool Request(uint8_t player);
  void AtBoundary(bool finished);
  bool CanUndo(uint8_t player) const;
  bool QueueResponse(InputSubmission, std::optional<ClockState> clock = {});
  bool HasPendingResponse() const;
  Boundary Advance(const CoreDriver::LiveOutput &output = {});
  Boundary Current() const;
  InputToken Token() const;
  std::vector<ResponseRecord> History() const;
  ClockState Clock() const;
  void SetClock(ClockState);
  LocalUndoState State() const;
  std::string Error() const;
  PrepareUndoFn SetPrepare(PrepareUndoFn); // owner thread, before a request
  const CoreDriver &Live() const {
    return *live_;
  } // owner thread; scoped queries only
private:
  mutable std::mutex mutex_;
  std::unique_ptr<CoreDriver> live_;
  DuelHistory history_;
  ClockState clock_{};
  InputToken token_{};
  LocalUndoState state_{LocalUndoState::Running};
  PrepareUndoFn prepare_;
  RebuildFn rebuild_;
  std::optional<uint8_t> requester_;
  std::optional<InputSubmission> pending_;
  bool processing_{}, finished_{};
  std::string error_;
  bool CanUndoLocked(uint8_t) const;
};
} // namespace undo
