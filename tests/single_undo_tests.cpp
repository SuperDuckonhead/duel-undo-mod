#include "common.h"
#include "test_support.h"
#include "undo/single_undo.h"
#include <future>
#include <iostream>
using namespace undo;
static Bytes response(uint32_t n) {
  return {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
}
struct Prepared final : PreparedUndo {
  bool &committed;
  explicit Prepared(bool &c) : committed(c) {}
  void Commit() noexcept override { committed = true; }
};
int main(int argc, char **argv) {
  try {
    CHECK(argc == 2);
    auto r = ResourceView::Capture(argv[1]);
    InitialState initial;
    initial.seed.resize(SEED_COUNT, 42);
    initial.duelOptions = 5u << 16;
    initial.resourceDigest = r->Fingerprint();
    for (uint8_t p = 0; p < 2; ++p)
      for (int i = 0; i < 20; ++i)
        initial.cards.push_back(
            {89631139, p, p, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE});
    bool committed = false;
    SingleUndo s(CoreDriver::Create(initial, r),
                 [&](const CoreDriver &, const Checkpoint &, uint64_t) {
                   return std::make_unique<Prepared>(committed);
                 });
    bool observed = false;
    auto start = s.Advance([&](const Bytes &msg) {
      CHECK(!msg.empty());
      if (!observed) {
        observed = true;
        auto other = std::async(std::launch::async, [&] {
          return CoreDriver::Create(initial, r)->Advance();
        });
        CHECK(other.wait_for(std::chrono::seconds(2)) ==
              std::future_status::ready);
        CHECK(other.get().kind == BoundaryKind::AwaitResponse);
      }
    });
    CHECK(observed);
    CHECK(!s.Live().QueryInfo().empty());
    CHECK(!s.Live().QueryField(0, LOCATION_DECK, QUERY_CODE).empty());
    CHECK(start.kind == BoundaryKind::AwaitResponse);
    CHECK(!s.CanUndo(0));
    s.SetClock({{900, 800}});
    auto token = s.Token();
    CHECK(s.QueueResponse({response(99), Origin::Manual, token}));
    CHECK(s.History().empty());
    CHECK(s.Advance().rejectedResponse);
    CHECK(s.History().empty());
    CHECK(s.QueueResponse({response(7), Origin::Manual, token}));
    s.Advance();
    CHECK(s.History().size() == 1);
    s.SetClock({{700, 600}});
    auto live = s.Current();
    CHECK(s.Request(0));
    CHECK(s.State() == LocalUndoState::WaitBoundary);
    CHECK(SamePosition(s.Current().checkpoint, live.checkpoint));
    CHECK(!committed);
    s.AtBoundary(false);
    CHECK(committed);
    CHECK(s.History().empty());
    CHECK((s.Clock().remainingMs == std::array<int64_t, 2>({900, 800})));
    CHECK(SamePosition(s.Current().checkpoint, start.checkpoint));
    CHECK(s.Token().epoch == token.epoch + 1);
    CHECK(!s.QueueResponse({response(7), Origin::Manual, token}));
    CHECK(s.QueueResponse({response(7), Origin::Manual, s.Token()}));
    s.Advance();
    auto before = s.Current();
    auto records = s.History();
    auto clock = s.Clock();
    s.SetPrepare([](const CoreDriver &, const Checkpoint &,
                    uint64_t) -> std::unique_ptr<PreparedUndo> {
      throw std::runtime_error("injected UI prepare failure");
    });
    CHECK(s.Request(0));
    s.AtBoundary(false);
    CHECK(s.State() == LocalUndoState::Failed);
    CHECK(SamePosition(s.Current().checkpoint, before.checkpoint));
    CHECK(s.History().size() == records.size());
    CHECK(s.Clock().remainingMs == clock.remainingMs);
    auto automatic = response(0xffffffff);
    CHECK(s.QueueResponse({automatic, Origin::Automatic, s.Token()}));
    s.Advance();
    CHECK(s.History().size() == records.size() + 1);
    CHECK(s.History().back().origin == Origin::Automatic);
    CHECK(s.Request(0));
    s.AtBoundary(true);
    CHECK(!s.CanUndo(0));
    CHECK(s.History().size() == records.size() + 1);
    // A request raised by a live output callback queues until process reaches
    // its accepted boundary. UI failure does not run a second process on the
    // old core.
    bool didCommit = false;
    SingleUndo queued(CoreDriver::Create(initial, r),
                      [&](const CoreDriver &, const Checkpoint &, uint64_t) {
                        return std::make_unique<Prepared>(didCommit);
                      });
    queued.Advance();
    CHECK(queued.QueueResponse({response(7), Origin::Manual, queued.Token()}));
    queued.Advance();
    CHECK(queued.QueueResponse(
        {response(0xffffffff), Origin::Automatic, queued.Token()}));
    bool requested = false;
    queued.Advance([&](const Bytes &) {
      if (!requested) {
        requested = true;
        CHECK(queued.Request(0));
        queued.AtBoundary(false);
        CHECK(!didCommit);
        CHECK(queued.Token().epoch == 0);
      }
    });
    CHECK(requested);
    CHECK(queued.State() == LocalUndoState::WaitBoundary);
    queued.AtBoundary(false);
    CHECK(didCommit);
    CHECK(queued.History().empty());
    CHECK(queued.Token().epoch == 1);
    // Rebuild failure preserves the original core and accepted branch, and that
    // same handle accepts the next legal input after failure.
    SingleUndo broken(
        CoreDriver::Create(initial, r),
        [&](const CoreDriver &, const Checkpoint &, uint64_t) {
          return std::make_unique<Prepared>(didCommit);
        },
        [](const InitialState &, std::shared_ptr<const ResourceView>,
           const std::vector<ResponseRecord> &, size_t,
           const Checkpoint &) -> std::unique_ptr<CoreDriver> {
          throw std::runtime_error("injected rebuild failure");
        });
    broken.Advance();
    CHECK(broken.QueueResponse({response(7), Origin::Manual, broken.Token()}));
    broken.Advance();
    auto failedTarget = broken.Current();
    auto failedToken = broken.Token();
    broken.SetClock({{321, 654}});
    CHECK(broken.Request(0));
    broken.AtBoundary(false);
    CHECK(broken.State() == LocalUndoState::Failed);
    CHECK(broken.Token() == failedToken);
    CHECK(broken.History().size() == 1);
    CHECK(broken.Clock().remainingMs[0] == 321);
    CHECK(SamePosition(broken.Current().checkpoint, failedTarget.checkpoint));
    CHECK(broken.QueueResponse(
        {response(0xffffffff), Origin::Automatic, broken.Token()}));
    CHECK(broken.Advance().kind == BoundaryKind::AwaitResponse);
    // Initial policy metadata is frozen for all four combinations; candidate
    // construction never reshuffles the already fixed initial card sequence.
    for (bool noCheck : {false, true})
      for (bool noShuffle : {false, true}) {
        auto policy = initial;
        policy.noCheckDeck = noCheck;
        policy.noShuffleDeck = noShuffle;
        SingleUndo options(
            CoreDriver::Create(policy, r),
            [&](const CoreDriver &, const Checkpoint &, uint64_t) {
              return std::make_unique<Prepared>(didCommit);
            });
        auto first = options.Advance();
        CHECK(options.QueueResponse(
            {response(7), Origin::Manual, options.Token()}));
        options.Advance();
        CHECK(options.Request(0));
        options.AtBoundary(false);
        CHECK(SamePosition(options.Current().checkpoint, first.checkpoint));
        CHECK(options.Live().Initial().noCheckDeck == noCheck);
        CHECK(options.Live().Initial().noShuffleDeck == noShuffle);
      }
    std::cout << "safe explicit boundary, accepted-only history, stale input, "
                 "prepare failure and finished cancellation passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
