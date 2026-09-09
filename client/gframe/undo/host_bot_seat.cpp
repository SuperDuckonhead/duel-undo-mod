#include "host_bot_seat.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace undo {
struct HostBotSeat::Impl {
  struct Job {
    std::uint64_t id{}, epoch{}, prompt{};
    BotOperation operation;
    TxKey key{};
    std::size_t cursor{};
    Bytes packet;
  };
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Job> jobs;
  std::deque<BotCompletion> completed;
  BotCancellation cancel{std::make_shared<std::atomic<bool>>(false)};
  std::thread worker;
  SessionId session;
  std::uint64_t generation, next{1}, barrier{}, installedEpoch;
  bool stopped{}, admission{};
  std::size_t completedBytes{};
  Impl(std::wstring executable, BotLaunchData launch, SessionId sid,
       std::uint64_t epoch, std::uint64_t gen)
      : session(sid), generation(gen), installedEpoch(epoch) {
    jobs.push_back({1, epoch, 0, BotOperation::Initialize});
    worker =
        std::thread([this, executable = std::move(executable),
                     launch = std::move(launch)] { run(executable, launch); });
  }
  std::uint64_t push(BotOperation operation, TxKey key = {},
                     std::uint64_t epoch = 0, std::uint64_t prompt = 0,
                     Bytes packet = {}, std::size_t cursor = 0) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopped)
      return 0;
    if (operation == BotOperation::Dispatch &&
        (!admission || epoch != installedEpoch))
      return 0;
    if (operation != BotOperation::Dispatch && operation != BotOperation::Fence)
      admission = false;
    if (jobs.size() + completed.size() >= 1024)
      throw std::runtime_error("Bot job queue limit reached; host must pause");
    if (next == UINT64_MAX)
      throw std::runtime_error("Bot job identity exhausted");
    const auto id = ++next;
    if (operation != BotOperation::Dispatch &&
        operation != BotOperation::Fence) {
      admission = false;
      barrier = id;
    }
    jobs.push_back(
        {id, epoch, prompt, operation, key, cursor, std::move(packet)});
    wake.notify_one();
    return id;
  }
  void run(const std::wstring &executable, const BotLaunchData &launch) {
    std::unique_ptr<BotController> bot;
    bool queueFailed = false;
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [&] { return stopped || !jobs.empty(); });
        if (stopped)
          break;
        job = std::move(jobs.front());
        jobs.pop_front();
      }
      BotCompletion result;
      result.job = job.id;
      result.generation = generation;
      result.operation = job.operation;
      result.identity = {session, job.epoch, BotState::Failed, 0};
      try {
        if (queueFailed)
          throw std::runtime_error(
              "Bot completion queue failed; participant paused");
        if (job.operation == BotOperation::Initialize)
          bot = std::make_unique<BotController>(executable, launch, session,
                                                job.epoch, cancel);
        if (!bot)
          throw std::runtime_error("Bot initialization failed");
        result.accepted = true;
        switch (job.operation) {
        case BotOperation::Initialize:
          break;
        case BotOperation::Dispatch:
          result.outputs =
              bot->Dispatch(session, job.epoch, job.prompt, job.packet);
          result.accepted = bot->State() == BotState::Running;
          break;
        case BotOperation::Fence:
          result.accepted = bot->State() != BotState::Failed;
          break;
        case BotOperation::Prepare:
          result.accepted = bot->Prepare(job.key, job.cursor);
          break;
        case BotOperation::Commit:
          result.accepted = bot->Commit(job.key);
          break;
        case BotOperation::Abort:
          bot->Abort(job.key);
          result.accepted = bot->State() == BotState::Running;
          break;
        case BotOperation::Resume:
          result.accepted = bot->Resume(job.key, job.epoch);
          break;
        case BotOperation::Pause:
          bot->Pause();
          break;
        }
        result.identity = bot->Identity();
        result.cursor = bot->Cursor();
        result.candidatePid = bot->CandidatePid();
        result.retainedPid = bot->RetainedPid();
        result.commitCount = bot->CommitCount();
        result.failure = bot->Failure();
        result.selection = bot->Selection();
      } catch (const std::exception &error) {
        result.accepted = false;
        result.identity.state = BotState::Failed;
        result.failure = error.what();
        result.outputs.clear();
      }
      std::size_t bytes{};
      for (const auto &output : result.outputs)
        bytes += output.packet.size();
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopped)
          break;
        if (bytes > 64 * 1024 * 1024 - completedBytes) {
          result.accepted = false;
          result.identity.state = BotState::Failed;
          result.failure = "Bot completion queue exceeded limit";
          result.outputs.clear();
          bytes = 0;
          admission = false;
          queueFailed = true;
          cancel->store(true);
        }
        completedBytes += bytes;
        completed.push_back(std::move(result));
      }
    }
    // BotController is destroyed here, exclusively by its worker owner.
  }
  void stop() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopped = true;
      admission = false;
      cancel->store(true);
      jobs.clear();
      wake.notify_all();
    }
    if (worker.joinable())
      worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    completed.clear();
    completedBytes = 0;
  }
};
HostBotSeat::HostBotSeat(std::wstring executable, BotLaunchData launch,
                         SessionId session, std::uint64_t epoch,
                         std::uint64_t generation)
    : impl_(std::make_unique<Impl>(std::move(executable), std::move(launch),
                                   session, epoch, generation)) {}
HostBotSeat::~HostBotSeat() { Stop(); }
std::uint64_t HostBotSeat::Dispatch(std::uint64_t epoch, std::uint64_t prompt,
                                    Bytes packet) {
  if (packet.empty() || packet.size() > 65535)
    throw std::invalid_argument("Invalid visible bot packet");
  return impl_->push(BotOperation::Dispatch, {}, epoch, prompt,
                     std::move(packet));
}
std::uint64_t HostBotSeat::Fence() { return impl_->push(BotOperation::Fence); }
std::uint64_t HostBotSeat::Prepare(TxKey key, std::size_t cursor) {
  return impl_->push(BotOperation::Prepare, key, 0, 0, {}, cursor);
}
std::uint64_t HostBotSeat::Commit(TxKey key) {
  return impl_->push(BotOperation::Commit, key);
}
std::uint64_t HostBotSeat::Abort(TxKey key) {
  return impl_->push(BotOperation::Abort, key);
}
std::uint64_t HostBotSeat::Resume(TxKey key, std::uint64_t epoch) {
  return impl_->push(BotOperation::Resume, key, epoch);
}
std::uint64_t HostBotSeat::Pause() { return impl_->push(BotOperation::Pause); }
std::vector<BotCompletion> HostBotSeat::Poll() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<BotCompletion> out;
  out.reserve(impl_->completed.size());
  while (!impl_->completed.empty()) {
    auto result = std::move(impl_->completed.front());
    impl_->completed.pop_front();
    if (result.identity.state == BotState::Failed)
      impl_->admission = false;
    if (result.accepted && result.job >= impl_->barrier &&
        result.identity.state == BotState::Running &&
        (result.operation == BotOperation::Initialize ||
         result.operation == BotOperation::Abort ||
         result.operation == BotOperation::Resume)) {
      impl_->installedEpoch = result.identity.epoch;
      impl_->admission = true;
    }
    out.push_back(std::move(result));
  }
  impl_->completedBytes = 0;
  return out;
}
void HostBotSeat::Stop() noexcept {
  if (impl_)
    impl_->stop();
}
} // namespace undo