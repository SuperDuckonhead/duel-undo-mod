#include "room_client.h"
#include "duelclient.h"
#include "game.h"
#include "undo/player_restore.h"
#include "undo/room_restore.h"
#include "undo_prompt.h"
#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>
namespace ygo {
namespace {
void need(bool b, const char *why) {
  if (!b)
    throw std::runtime_error(why);
}
uint64_t number(const undo::Bytes &b, size_t at, unsigned n) {
  need(at <= b.size() && n <= b.size() - at, "Truncated control");
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= uint64_t(b[at + i]) << (i * 8);
  return v;
}
undo::Bytes epochBytes(uint64_t epoch) {
  undo::Bytes b;
  for (unsigned i = 0; i < 8; ++i)
    b.push_back(uint8_t(epoch >> (8 * i)));
  return b;
}
} // namespace
struct RoomClient::Impl {
  struct Saved {
    undo::Digest digest{};
    undo::Bytes prompt;
    DuelPromptContext context;
    std::shared_ptr<const PromptSnapshot> widgets;
  };
  struct Prepared {
    undo::RoomRestore descriptor;
    std::vector<undo::Bytes> journal;
    std::map<uint64_t, Saved> snapshots;
    std::unique_ptr<PreparedPrompt> widgets;
    std::unique_ptr<undo::GamePacketStream> stream;
    DuelPromptContext context;
  };
  struct Command {
    enum Kind { Response, Legacy, Request, Consent } kind;
    undo::InputSubmission input;
    undo::Bytes packet;
    bool approve{};
    undo::TxKey key;
    bool resumesOnAbort() const {
      return kind == Response ||
             (kind == Legacy && !packet.empty() &&
              packet[0] == CTOS_TIME_CONFIRM);
    }
  };
  Game &g;
  Send send;
  Apply apply;
  Deferred deferred;
  const undo::SessionId session;
  uint64_t epoch{}, prompt{}, outboundSequence{}, highestRequest{};
  uint8_t recipient{2};
  undo::RoomStatus status;
  std::unique_ptr<undo::GamePacketStream> stream;
  std::vector<undo::Bytes> journal;
  std::map<uint64_t, Saved> snapshots;
  undo::PlayerViewState view;
  std::unique_ptr<undo::ClientRestore> restore;
  std::optional<undo::TxKey> active;
  std::unique_ptr<undo::FragmentAssembler> fragments;
  std::unique_ptr<Prepared> prepared;
  bool committed{}, failed{}, capturing{}, requested{}, terminal{}, closed{};
  uint64_t requestedId{};
  std::optional<undo::InputSubmission> automatic;
  mutable std::mutex mutex;
  undo::InputToken published;
  bool paused{true}, canUndo{}, consent{}, responseQueued{}, responseAllowed{},
      capturePublished{}, freezePresentation{}, requestPending{};
  uint64_t requestPublished{1};
  undo::TxKey consentKey;
  bool boundaryDirty{};
  std::wstring text{L"正在等待对战"};
  std::wstring requestNotice;
  std::deque<Command> commands;
  Impl(Game &game, undo::SessionId s, Send a, Apply b, Deferred c)
      : g(game), send(std::move(a)), apply(std::move(b)),
        deferred(std::move(c)), session(s),
        stream(std::make_unique<undo::GamePacketStream>(s, 0)) {
    published = {s, 0, 0};
  }
  ~Impl() {
    if (!closed)
      close();
  }
  void close() {
    if (closed)
      return;
    fail();
    std::lock_guard<std::mutex> lock(g.gMutex);
    prepared.reset();
    restore.reset();
    snapshots.clear();
    closed = true;
  }
  void publish(bool freeze) {
    std::lock_guard<std::mutex> lock(mutex);
    if (published.epoch != epoch || published.prompt != prompt) {
      responseQueued = false;
      requestNotice.clear();
    }
    published = {session, epoch, prompt};
    paused = freeze || requestPending;
    capturePublished = capturing;
    freezePresentation = bool(active) || requested || requestPending ||
                         status.state != undo::TxState::Running;
    requestPublished = status.nextRequest;
    responseAllowed = !paused && prompt && status.promptPlayer == recipient;
    canUndo = !paused && !responseQueued && recipient < 2 &&
              (status.eligibleMask & (1u << recipient));
  }
  void fail() {
    failed = true;
    std::lock_guard<std::mutex> lock(mutex);
    paused = true;
    freezePresentation = true;
    canUndo = false;
    consent = false;
    requestNotice.clear();
    text = L"恢复状态不确定，已暂停对战";
    commands.clear();
  }
  void receiptGame(const undo::Envelope &e) {
    auto packet = stream->Add(e);
    if (!packet)
      return;
    const auto &bytes = packet->packet;
    const bool ending =
        bytes[0] == STOC_DUEL_END ||
        (bytes.size() > 1 && bytes[0] == STOC_GAME_MSG && bytes[1] == MSG_WIN);
    if (ending || (terminal && bytes[0] == STOC_REPLAY)) {
      // Terminal display is authorized even after uncertain commit. It
      // ends the room; it never resumes input or claims an abort rollback.
      {
        std::lock_guard<std::mutex> lock(g.gMutex);
        prepared.reset();
        restore.reset();
      }
      active.reset();
      fragments.reset();
      failed = true;
      {
        std::lock_guard<std::mutex> lock(mutex);
        terminal = true;
        paused = true;
        canUndo = false;
        consent = false;
        requestNotice.clear();
        freezePresentation = false;
        commands.clear();
        automatic.reset();
        requestPending = false;
      }
      apply(bytes);
      return;
    }
    need(!active && !failed, "Gameplay arrived during transaction");
    const bool game = bytes[0] == STOC_GAME_MSG;
    if (bytes[0] == STOC_TIME_LIMIT) {
      // WaitforResponse sends this packet before the selecting player's game
      // message. Bind its automatic acknowledgement to the packet's prompt,
      // while keeping the old visible prompt closed to gameplay submissions.
      prompt = packet->prompt;
      publish(true);
    }
    if (game) {
      need(bytes.size() > 1, "Empty game message");
      if (bytes[1] == MSG_START) {
        need(bytes.size() > 2, "Truncated start");
        recipient = bytes[2];
        need(recipient < 2, "撤回房间仅支持两名对战玩家");
        restore = std::make_unique<undo::ClientRestore>(
            g.dField, view, recipient, session, epoch);
        journal.clear();
        snapshots.clear();
      }
      prompt = packet->prompt;
      capturing = true;
      if (bytes[1] == MSG_RETRY)
        boundaryDirty = true;
      publish(true);
    }
    apply(bytes); // real live legacy handler, never called by Prepare/Commit
    if (game) {
      capturing = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        capturePublished = false;
      }
      if (bytes[1] != MSG_RETRY)
        journal.emplace_back(bytes.begin() + 1, bytes.end());
    }
  }
  bool pendingRequest() const {
    std::lock_guard<std::mutex> lock(mutex);
    return requestPending;
  }
  void receiptStatus(const undo::Envelope &e) {
    if (!undo::IsCurrent(e.key, session, epoch))
      return;
    need(!e.key.request && !e.key.targetIndex &&
             e.key.targetDigest == undo::Digest{},
         "Invalid status identity");
    status = undo::DecodeRoomStatus(e.payload);
    if (status.state == undo::TxState::PausedFailed) {
      fail();
      return;
    }
    if (status.state != undo::TxState::Running || active || requested ||
        pendingRequest()) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (status.state == undo::TxState::WaitBoundary)
          text = L"等待安全撤回边界";
        else if (status.state == undo::TxState::Preparing)
          text = L"正在恢复";
        else if (status.state == undo::TxState::Committing)
          text = L"等待双方确认恢复";
      }
      publish(true);
      return;
    }
    prompt = status.prompt;
    if (prompt && recipient < 2 && !snapshots.count(prompt)) {
      need(!journal.empty(), "Boundary has no visible prompt");
      auto frames = journal;
      auto target = std::move(frames.back());
      frames.pop_back();
      auto visible = undo::BuildPlayerRestore(recipient, frames, target);
      std::lock_guard<std::mutex> lock(g.gMutex);
      Saved saved{visible.visibleDigest, target,
                  DuelClient::CapturePromptContext(), CaptureUndoPrompt(g)};
      snapshots.emplace(prompt, std::move(saved));
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (boundaryDirty)
        responseQueued = false;
      boundaryDirty = false;
      text = (recipient < 2 && (status.eligibleMask & (1u << recipient)))
                 ? L"撤回上次有效选择"
                 : L"没有可撤回的选择";
    }
    {
      std::lock_guard<std::mutex> lock(g.gMutex);
      g.dInfo.time_player =
          status.timePlayer < 2 ? g.LocalPlayer(status.timePlayer) : 2;
      for (int i = 0; i < 2; ++i)
        g.dInfo.time_left[g.LocalPlayer(i)] = uint16_t(
            std::min<int64_t>(65535, status.clock.remainingMs[i] / 1000));
    }
    publish(prompt == 0);
    std::optional<undo::InputSubmission> queued;
    {
      std::lock_guard<std::mutex> lock(mutex);
      queued.swap(automatic);
    }
    if (queued) {
      std::lock_guard<std::mutex> lock(g.gMutex);
      deferred(*queued);
    }
  }
  bool bind(const undo::TxKey &k) {
    if (!undo::IsCurrent(k, session, epoch) || !k.request)
      return false;
    if (active)
      return undo::SameKey(*active, k);
    if (k.request <= highestRequest)
      return false;
    active = k;
    highestRequest = k.request;
    publish(true);
    return true;
  }
  void receiptPrepare(const undo::Envelope &e) {
    if (!bind(e.key))
      return;
    if (prepared) {
      send({undo::WireKind::Ready, e.key, {1}});
      return;
    }
    try {
      if (!fragments)
        fragments = std::make_unique<undo::FragmentAssembler>();
      fragments->Add(e.payload);
      if (number(e.payload, 0, 4) + 1 != number(e.payload, 4, 4))
        return;
      auto descriptor = undo::DecodeRoomRestore(fragments->Finish());
      auto visible = undo::DecodePlayerRestore(descriptor.visible);
      auto saved = snapshots.find(descriptor.prompt);
      need(saved != snapshots.end() && visible.player == recipient &&
               saved->second.digest == visible.visibleDigest &&
               saved->second.prompt == visible.prompt,
           "Historical visible snapshot mismatch");
      auto candidate = std::make_unique<Prepared>();
      candidate->descriptor = std::move(descriptor);
      candidate->context = saved->second.context;
      candidate->journal = visible.frames;
      candidate->journal.push_back(visible.prompt);
      for (auto i = snapshots.begin();
           i != snapshots.end() && i->first <= candidate->descriptor.prompt;
           ++i)
        candidate->snapshots.emplace(*i);
      need(epoch != UINT64_MAX, "Room epoch exhausted");
      candidate->stream =
          std::make_unique<undo::GamePacketStream>(session, epoch + 1);
      std::lock_guard<std::mutex> lock(g.gMutex);
      need(restore && restore->Prepare(e.key, visible, saved->second.prompt),
           "Player model preparation failed");
      try {
        candidate->widgets = PrepareUndoPrompt(g, *saved->second.widgets,
                                               restore->PreparedField());
      } catch (...) {
        restore->Abort(e.key);
        throw;
      }
      prepared = std::move(candidate);
      send({undo::WireKind::Ready, e.key, {1}});
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(g.gMutex);
        prepared.reset();
        if (restore)
          restore->Abort(e.key);
      }
      fragments.reset();
      send({undo::WireKind::Ready, e.key, {0}});
    }
  }
  void receiptCommit(const undo::Envelope &e) {
    if (!active || !undo::SameKey(*active, e.key))
      return;
    need(e.payload.size() == 8, "Invalid commit payload");
    auto next = number(e.payload, 0, 8);
    if (committed) {
      need(next == epoch, "Conflicting repeated commit");
      send({undo::WireKind::CommitAck, e.key, e.payload});
      return;
    }
    need(prepared && next == epoch + 1, "Unprepared commit");
    {
      std::lock_guard<std::mutex> lock(g.gMutex);
      need(restore->Commit(e.key, next), "Invalid model commit");
      // Everything below was allocated/validated before Ready. Old field
      // and widget bank stay owned and frozen until the host's Resume.
      prepared->widgets->Install();
      journal.swap(prepared->journal);
      snapshots.swap(prepared->snapshots);
      stream.swap(prepared->stream);
      epoch = next;
      prompt = prepared->descriptor.prompt;
      outboundSequence = 0;
      status.clock = prepared->descriptor.clock;
      status.timePlayer = prepared->descriptor.timePlayer;
      status.prompt = prompt;
      status.promptPlayer =
          view.prompt[0] == MSG_WAITING ? 1 - recipient : recipient;
      status.eligibleMask = 0;
      for (auto &f : g.fadingList)
        f.guiFading->setVisible(false);
      g.fadingList.clear();
      g.showcard = 0;
      g.is_attacking = 0;
      g.waitFrame = -1;
      g.lpframe = 0;
      g.lpcstring.clear();
      g.dInfo.turn = view.turn;
      g.dInfo.duel_rule = view.duelRule;
      g.dInfo.curMsg = view.prompt[0];
      g.dInfo.isFinished = false;
      g.dInfo.time_player =
          status.timePlayer < 2 ? g.LocalPlayer(status.timePlayer) : 2;
      for (int i = 0; i < 2; ++i) {
        g.dInfo.lp[i] = view.lp[i];
        myswprintf(g.dInfo.strLP[i], L"%d", view.lp[i]);
        g.dInfo.time_left[g.LocalPlayer(i)] = uint16_t(
            std::min<int64_t>(65535, status.clock.remainingMs[i] / 1000));
      }
      DuelClient::RestorePromptContext(prepared->context);
      DuelClient::ClearPendingResponse();
      committed = true;
    }
    automatic.reset();
    publish(true);
    send({undo::WireKind::CommitAck, e.key, e.payload});
  }
  void receiptResume(const undo::Envelope &e) {
    if (!active || !undo::SameKey(*active, e.key))
      return;
    need(committed && e.payload.size() == 8 && number(e.payload, 0, 8) == epoch,
         "Invalid resume");
    {
      std::lock_guard<std::mutex> lock(g.gMutex);
      need(restore->Resume(e.key), "Invalid model resume");
      prepared.reset();
    }
    active.reset();
    fragments.reset();
    committed = false;
    requested = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      responseQueued = false;
      commands.clear();
      consent = false;
      requestPending = false;
      requestNotice.clear();
    }
    publish(false);
  }
  void rejectOwnRequest(bool busy) {
    requested = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      requestPending = false;
      requestNotice = busy ? L"已有撤回请求，请先处理当前请求。"
                           : L"当前没有可撤回的操作。";
    }
    publish(bool(active) || status.state != undo::TxState::Running || !prompt);
  }
  void receiptRejected(const undo::Envelope &e) {
    if (!undo::IsCurrent(e.key, session, epoch) || !requested ||
        e.key.request != requestedId || !e.key.request || e.key.targetIndex ||
        e.key.targetDigest != undo::Digest{})
      return;
    need(e.payload.size() == 1 && (e.payload[0] == 1 || e.payload[0] == 2),
         "Invalid rejected-request reason");
    rejectOwnRequest(e.payload[0] == 1);
  }
  void receiptAbort(const undo::Envelope &e) {
    if (!undo::IsCurrent(e.key, session, epoch) || failed)
      return;
    if (active && !undo::SameKey(*active, e.key))
      return;
    if (!active && (!requested || e.key.request != requestedId))
      return;
    need(e.payload.size() == 2 && e.payload[1] <= 1, "Invalid abort payload");
    need(!committed, "Cannot abort installed branch");
    {
      std::lock_guard<std::mutex> lock(g.gMutex);
      prepared.reset();
      if (restore)
        restore->Abort(e.key);
    }
    fragments.reset();
    automatic.reset();
    if (!e.payload[1]) {
      send({undo::WireKind::AbortAck, e.key, {}});
      publish(true);
      return;
    }
    active.reset();
    requested = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      consent = false;
      requestPending = false;
      // Preserve unsent selections and their native timer acknowledgement in
      // order. The unchanged branch still needs confirmation before response.
      commands.erase(std::remove_if(commands.begin(), commands.end(),
                                    [](const Command &c) {
                                      return !c.resumesOnAbort();
                                    }),
                     commands.end());
      text = L"撤回已取消，原局可继续";
    }
    publish(false);
  }
};
RoomClient::RoomClient(Game &g, undo::SessionId s, Send send, Apply apply,
                       Deferred deferred)
    : impl_(std::make_unique<Impl>(g, s, std::move(send), std::move(apply),
                                   std::move(deferred))) {}
RoomClient::~RoomClient() = default;
void RoomClient::Close() { impl_->close(); }
void RoomClient::Receive(const undo::Envelope &e) {
  auto &r = *impl_;
  if (r.failed && e.kind != undo::WireKind::Game)
    return;
  try {
    switch (e.kind) {
    case undo::WireKind::Game:
      r.receiptGame(e);
      break;
    case undo::WireKind::Status:
      r.receiptStatus(e);
      break;
    case undo::WireKind::Prepare:
      r.receiptPrepare(e);
      break;
    case undo::WireKind::Commit:
      r.receiptCommit(e);
      break;
    case undo::WireKind::Resume:
      r.receiptResume(e);
      break;
    case undo::WireKind::RequestRejected:
      r.receiptRejected(e);
      break;
    case undo::WireKind::Abort:
      r.receiptAbort(e);
      break;
    case undo::WireKind::Consent:
      if (r.bind(e.key)) {
        need(e.payload.size() == 1 && e.payload[0] < 2,
             "Invalid consent requester");
        need(e.key.targetIndex < UINT64_MAX, "Invalid public target index");
        std::lock_guard<std::mutex> lock(r.mutex);
        r.consentKey = e.key;
        r.consent = e.payload[0] != r.recipient;
        r.text = L"玩家" + std::to_wstring(unsigned(e.payload[0]) + 1) +
                 L"请求撤回第" + std::to_wstring(e.key.targetIndex + 1) +
                 L"次操作。" + (r.consent ? L"是否同意？" : L"等待对方同意。");
      }
      break;
    default:
      throw std::runtime_error("Unexpected room control");
    }
  } catch (...) {
    r.fail();
  }
}
undo::InputToken RoomClient::Token() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->published;
}
bool RoomClient::InputPaused() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->paused;
}
bool RoomClient::PresentationFrozen() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->freezePresentation;
}
bool RoomClient::DuelEnded() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->terminal;
}
bool RoomClient::CanUndo() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->canUndo && !impl_->responseQueued;
}
bool RoomClient::NeedsConsent() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->consent;
}
std::optional<undo::TxKey> RoomClient::ConsentTarget(std::wstring *description) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->consent)
    return std::nullopt;
  if (description)
    *description = impl_->text;
  return impl_->consentKey;
}
std::wstring RoomClient::StatusText() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->requestNotice.empty()
             ? impl_->text
             : impl_->requestNotice + L"\n" + impl_->text;
}
bool RoomClient::Submit(const undo::InputSubmission &i) {
  auto &r = *impl_;
  std::lock_guard<std::mutex> lock(r.mutex);
  if (!(i.token == r.published) || i.response.empty() ||
      i.response.size() > 256 || i.origin == undo::Origin::Bot ||
      r.responseQueued)
    return false;
  if (r.paused) {
    if (r.capturePublished && i.origin == undo::Origin::Automatic &&
        !r.automatic)
      r.automatic = i;
    return false;
  }
  if (!r.responseAllowed)
    return false;
  r.commands.push_back({Impl::Command::Response, i, {}, {}, {}});
  r.responseQueued = true;
  r.canUndo = false;
  return true;
}
bool RoomClient::QueueLegacy(const undo::Bytes &b) {
  auto &r = *impl_;
  std::lock_guard<std::mutex> lock(r.mutex);
  if (b.empty() ||
      (r.paused && (b[0] != CTOS_TIME_CONFIRM || r.freezePresentation)))
    return false;
  if (b[0] != CTOS_TIME_CONFIRM && b[0] != CTOS_SURRENDER)
    return false;
  Impl::Command c;
  c.kind = Impl::Command::Legacy;
  c.packet = b;
  c.input.token = r.published;
  r.commands.push_back(std::move(c));
  return true;
}
bool RoomClient::RequestUndo() {
  auto &r = *impl_;
  std::lock_guard<std::mutex> lock(r.mutex);
  if (!r.canUndo || r.paused || r.responseQueued)
    return false;
  Impl::Command c;
  c.kind = Impl::Command::Request;
  c.key = {r.published.session, r.published.epoch, r.requestPublished, 0, {}};
  r.commands.push_back(c);
  r.paused = true;
  r.requestPending = true;
  r.requestNotice.clear();
  r.freezePresentation = true;
  r.canUndo = false;
  return true;
}
bool RoomClient::Consent(bool yes, const undo::TxKey &expected) {
  auto &r = *impl_;
  std::lock_guard<std::mutex> lock(r.mutex);
  if (!r.consent || !undo::SameKey(expected, r.consentKey))
    return false;
  Impl::Command c;
  c.kind = Impl::Command::Consent;
  c.approve = yes;
  c.key = r.consentKey;
  r.commands.push_back(c);
  r.consent = false;
  return true;
}
void RoomClient::Poll() {
  auto &r = *impl_;
  std::deque<Impl::Command> commands;
  {
    std::lock_guard<std::mutex> lock(r.mutex);
    commands.swap(r.commands);
  }
  try {
    for (auto &c : commands) {
      if (r.failed)
        break;
      if (c.kind == Impl::Command::Request) {
        if (r.active) {
          r.rejectOwnRequest(true);
          continue;
        }
        if (!r.active && !r.requested &&
            undo::IsCurrent(c.key, r.session, r.epoch)) {
          r.requested = true;
          r.requestedId = c.key.request;
          r.send({undo::WireKind::Request, c.key, {}});
        }
        continue;
      }
      if (c.kind == Impl::Command::Consent) {
        if (r.active && undo::SameKey(c.key, *r.active))
          r.send({undo::WireKind::Consent, c.key, {uint8_t(c.approve)}});
        continue;
      }
      if (!(c.input.token == Token()))
        continue;
      if (InputPaused() &&
          !(c.kind == Impl::Command::Legacy &&
            (!c.input.token.prompt ||
             (!c.packet.empty() && c.packet[0] == CTOS_TIME_CONFIRM)) &&
            !PresentationFrozen())) {
        if (c.resumesOnAbort()) {
          std::lock_guard<std::mutex> lock(r.mutex);
          r.commands.push_back(std::move(c));
        }
        continue;
      }
      if (c.kind == Impl::Command::Response) {
        {
          std::lock_guard<std::mutex> lock(r.mutex);
          if (!r.responseAllowed)
            continue;
        }
        r.send(undo::EncodeResponse({r.session, r.epoch, r.prompt, 0, {}},
                                    c.input.origin, c.input.response));
      } else
        for (auto &e : undo::EncodeGamePacket(r.session, r.epoch, r.prompt,
                                              ++r.outboundSequence, c.packet))
          r.send(e);
    }
  } catch (...) {
    r.fail();
  }
}
} // namespace ygo
