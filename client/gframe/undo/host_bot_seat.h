#pragma once
#include "bot_controller.h"
#include <vector>
namespace undo {
enum class BotOperation {
  Initialize,
  Dispatch,
  Fence,
  Prepare,
  Commit,
  Abort,
  Resume,
  Pause
};
struct BotCompletion {
  std::uint64_t job{}, generation{};
  BotOperation operation{BotOperation::Initialize};
  bool accepted{};
  BotIdentity identity;
  std::size_t cursor{};
  std::uint32_t candidatePid{}, retainedPid{}, commitCount{};
  BotSelectionInfo selection;
  std::vector<BotOutput> outputs;
  std::string failure;
};
// All public calls except destruction are made by one room-owner thread. The
// worker owns every BotController access and never calls a host/core/UI
// callback.
class HostBotSeat {
public:
  HostBotSeat(std::wstring executable, BotLaunchData, SessionId,
              std::uint64_t epoch, std::uint64_t generation);
  ~HostBotSeat();
  HostBotSeat(const HostBotSeat &) = delete;
  HostBotSeat &operator=(const HostBotSeat &) = delete;
  // Initialize is job 1. A zero Dispatch job means admission is closed/stale.
  std::uint64_t Dispatch(std::uint64_t epoch, std::uint64_t prompt, Bytes);
  // Prefix fence: every earlier callback has completed and its output is in a
  // preceding completion. Owner must deliver feedback and fence again to prove
  // the entire host input/output boundary quiescent before saving this cursor.
  std::uint64_t Fence();
  std::uint64_t Prepare(TxKey, std::size_t cursor);
  std::uint64_t Commit(TxKey);
  std::uint64_t Abort(TxKey);
  std::uint64_t Resume(TxKey, std::uint64_t epoch);
  std::uint64_t Pause();
  std::vector<BotCompletion> Poll();
  // Cancels both initial connection and later pipe waits, discards queued work,
  // joins the worker, and destroys the kill-on-close child process job.
  void Stop() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace undo