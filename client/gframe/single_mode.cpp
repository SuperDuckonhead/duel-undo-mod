#include "single_mode.h"
#include "data_manager.h"
#include "duelclient.h"
#include "game.h"
#include "undo/player_restore.h"
#include "undo_prompt.h"
#include <random>
#include <thread>
namespace ygo {
namespace {
struct VisibleBoundary {
  size_t cursor{};
  DuelPromptContext context;
  DuelPromptContext presentedContext;
  std::shared_ptr<const PromptSnapshot> presentation;
};
struct SingleRuntime {
  std::shared_ptr<undo::SingleUndo> session;
  undo::PlayerViewState view;
  std::unique_ptr<undo::ClientRestore> client;
  std::vector<undo::Bytes> journal;
  std::vector<VisibleBoundary> boundaries;
  std::atomic<bool> renderingRestore{false};
  std::atomic<bool> capturingPrompt{false};
  std::optional<undo::Boundary> installedBoundary;
};
thread_local bool analyzingLivePrompt{};
std::mutex activeMutex;
std::shared_ptr<SingleRuntime> activeRuntime;
bool launchPending{};
std::shared_ptr<SingleRuntime> Runtime() {
  std::lock_guard<std::mutex> lock(activeMutex);
  return activeRuntime;
}
void RecordVisible(const undo::Bytes &frame) {
  if (auto r = Runtime())
    r->journal.push_back(frame);
}
bool AnalyzeVisible(unsigned char *bytes, size_t n) {
  RecordVisible(undo::Bytes(bytes, bytes + n));
  return DuelClient::ClientAnalyze(bytes, n);
}
struct PreparedSingle final : undo::PreparedUndo {
  SingleRuntime &runtime;
  undo::TxKey key;
  uint64_t epoch;
  undo::Checkpoint target;
  std::vector<undo::Bytes> journal;
  std::vector<VisibleBoundary> boundaries;
  std::unique_ptr<PreparedPrompt> presentation;
  undo::Boundary nextBoundary;
  bool committed{};
  std::unique_lock<std::mutex> renderLock;
  PreparedSingle(SingleRuntime &r, undo::TxKey k, uint64_t e,
                 const undo::Checkpoint &t, std::vector<undo::Bytes> j,
                 std::vector<VisibleBoundary> b)
      : runtime(r), key(k), epoch(e), target(t), journal(std::move(j)),
        boundaries(std::move(b)), renderLock(mainGame->gMutex) {
    if (!boundaries.back().presentation)
      throw std::runtime_error("Missing pristine prompt presentation");
    nextBoundary.kind = undo::BoundaryKind::AwaitResponse;
    nextBoundary.checkpoint = target;
    journal.push_back(target.prompt);
    presentation = PrepareUndoPrompt(*mainGame, *boundaries.back().presentation,
                                     runtime.client->PreparedField());
  }
  ~PreparedSingle() override {
    if (!committed) runtime.client->Abort(key);
    presentation.reset(); // tree removal remains under the render lock
  }
  void Commit() noexcept override {
    // Prepare has checked the exact key and epoch; installation owns no
    // fallible parsing, allocation, engine processing or history edits.
    if (!runtime.client->Commit(key, epoch))
      std::terminate();
    runtime.journal.swap(journal);
    runtime.boundaries.swap(boundaries);
    runtime.renderingRestore = true;
    runtime.installedBoundary = std::move(nextBoundary);
    auto &g = *mainGame;
    for (auto &f : g.fadingList) {
      f.guiFading->setVisible(false);
    }
    g.fadingList.clear();
    g.showcard = 0;
    g.is_attacking = 0;
    g.waitFrame = -1;
    g.dInfo.lp[0] = runtime.view.lp[0];
    g.dInfo.lp[1] = runtime.view.lp[1];
    g.dInfo.turn = runtime.view.turn;
    g.dInfo.duel_rule = runtime.view.duelRule;
    g.dInfo.curMsg = target.prompt[0];
    g.dInfo.isFinished = false;
    g.dInfo.time_player = 2;
    for (int p = 0; p < 2; ++p) {
      myswprintf(g.dInfo.strLP[p], L"%d", g.dInfo.lp[p]);
      g.dInfo.time_left[p] =
          static_cast<unsigned short>(target.clock.remainingMs[p] / 1000);
    }
    DuelClient::RestorePromptContext(runtime.boundaries.back().presentedContext);
    DuelClient::ClearPendingResponse();
    presentation->Install();
    if (!runtime.client->Resume(key)) std::terminate();
    committed = true;
    runtime.renderingRestore = false;
  }
};
std::unique_ptr<undo::PreparedUndo>
PrepareSingle(SingleRuntime &runtime, const undo::CoreDriver &,
              const undo::Checkpoint &target, uint64_t epoch) {
  auto records = runtime.session->History();
  size_t keep = records.size();
  for (size_t i = 0; i < records.size(); ++i)
    if (undo::SamePosition(records[i].before, target)) {
      keep = i;
      break;
    }
  if (keep >= runtime.boundaries.size())
    throw std::runtime_error("Missing visible boundary journal");
  auto cursor = runtime.boundaries[keep].cursor;
  std::vector<undo::Bytes> journal(runtime.journal.begin(),
                                   runtime.journal.begin() + cursor);
  std::vector<VisibleBoundary> boundaries(
      runtime.boundaries.begin(), runtime.boundaries.begin() + keep + 1);
  auto restore = undo::BuildPlayerRestore(0, journal, target.prompt);
  auto token = runtime.session->Token();
  undo::TxKey key{token.session, token.epoch, epoch, keep,
                  restore.visibleDigest};
  if (!runtime.client->Prepare(key, restore, target.prompt))
    throw std::runtime_error(runtime.client->Error());
  try {
    return std::make_unique<PreparedSingle>(
        runtime, key, epoch, target, std::move(journal), std::move(boundaries));
  } catch (...) {
    runtime.client->Abort(key);
    throw;
  }
}
} // namespace
std::atomic<bool> SingleMode::is_closing{false};
std::atomic<bool> SingleMode::is_continuing{false};
Replay SingleMode::last_replay;
std::shared_ptr<undo::SingleUndo> SingleMode::ActiveSession() {
  auto r = Runtime();
  return r ? r->session : nullptr;
}
undo::InputToken SingleMode::CurrentToken() {
  auto s = ActiveSession();
  return s ? s->Token() : undo::InputToken{};
}
bool SingleMode::CanUndo(uint8_t p) {
  auto s = ActiveSession();
  return s && s->CanUndo(p);
}
bool SingleMode::InputPaused() {
  auto r = Runtime();
  if (!r)
    return false;
  auto state = r->session->State();
  return r->renderingRestore || (r->capturingPrompt && !analyzingLivePrompt) || state == undo::LocalUndoState::WaitBoundary ||
         state == undo::LocalUndoState::Preparing;
}
std::string SingleMode::LastUndoError() {
  auto s = ActiveSession();
  return s ? s->Error() : std::string{};
}
bool SingleMode::RequestUndo(uint8_t p) {
  auto s = ActiveSession();
  if (!s || !s->Request(p))
    return false;
  mainGame->singleSignal.Set();
  return true;
}
bool SingleMode::SetResponse(const undo::InputSubmission &input) {
  auto s = ActiveSession();
  if (!s || InputPaused())
    return false;
  return s->QueueResponse(
      input, undo::ClockState{{int64_t(mainGame->dInfo.time_left[0]) * 1000,
                               int64_t(mainGame->dInfo.time_left[1]) * 1000}});
}
bool SingleMode::StartPlay() {
  std::lock_guard<std::mutex> lock(activeMutex);
  if (activeRuntime || launchPending)
    return false;
  launchPending = true;
  try {
    std::thread(SinglePlayThread).detach();
    return true;
  } catch (...) {
    launchPending = false;
    throw;
  }
}
void SingleMode::StopPlay(bool exiting) {
  is_closing = exiting;
  is_continuing = false;
  mainGame->actionSignal.Set();
  mainGame->singleSignal.Set();
}
void SingleMode::SinglePlayThread() {
  is_closing = false;
  is_continuing = true;
  try {
    auto runtime = std::make_shared<SingleRuntime>();
    auto resources = dataManager.CaptureResources(
        mainGame->runtime_root.u8string(),
        mainGame->gameConf.prefer_expansion_script != 0);
    undo::InitialState initial;
    initial.seed.resize(SEED_COUNT);
    std::random_device random;
    for (auto &word : initial.seed)
      word = random();
    initial.resourceDigest = resources->Fingerprint();
    initial.players[0] = initial.players[1] = {8000, 5, 1};
    if (mainGame->chkSinglePlayReturnDeckTop->isChecked())
      initial.duelOptions |= DUEL_RETURN_DECK_TOP;
    wchar_t file[256]{};
    if (mainGame->open_file) {
      mainGame->open_file = false;
      BufferIO::CopyWideString(mainGame->open_file_name, file);
    } else {
      auto selected = mainGame->lstSinglePlayList->getSelected();
      if (selected < 0)
        throw std::runtime_error("No single scenario selected");
      BufferIO::CopyWideString(
          mainGame->lstSinglePlayList->getListItem(selected), file);
    }
    char utf8[1024]{};
    BufferIO::EncodeUTF8(file, utf8);
    std::string selectedPath = utf8;
    std::replace(selectedPath.begin(), selectedPath.end(), '\\', '/');
    auto path = std::filesystem::u8path(selectedPath).lexically_normal();
    if (path.is_absolute()) {
      path = path.lexically_relative(mainGame->runtime_root.lexically_normal());
      if (path.empty())
        throw std::runtime_error("Scenario is outside captured runtime");
    } else if (path.has_root_name() || path.has_root_directory()) {
      throw std::runtime_error("Scenario has an incomplete absolute path");
    }
    for (const auto &part : path)
      if (part == "..")
        throw std::runtime_error("Scenario is outside captured runtime");
    std::string logical = path.generic_u8string();
    if (logical.rfind("single/", 0) != 0)
      logical = "single/" + logical;
    resources->Read(logical);
    initial.scenarioName = logical;
    runtime->session = std::make_shared<undo::SingleUndo>(
        undo::CoreDriver::Create(initial, resources),
        [raw = runtime.get()](const undo::CoreDriver &c,
                              const undo::Checkpoint &t, uint64_t e) {
          return PrepareSingle(*raw, c, t, e);
        });
    auto session = runtime->session;
    runtime->client = std::make_unique<undo::ClientRestore>(
        mainGame->dField, runtime->view, 0, session->Token().session, 0);
    {
      std::lock_guard<std::mutex> lock(activeMutex);
      activeRuntime = runtime;
      launchPending = false;
    }
    DuelClient::ClearPendingResponse();
    DuelClient::RestorePromptContext({});
    {
      std::lock_guard<std::mutex> lock(mainGame->gMutex);
      mainGame->dInfo.Clear();
      mainGame->dInfo.lp[0] = mainGame->dInfo.lp[1] = mainGame->dInfo.start_lp =
          8000;
      for (int p = 0; p < 2; ++p)
        myswprintf(mainGame->dInfo.strLP[p], L"%d", 8000);
      BufferIO::CopyWideString(mainGame->ebNickName->getText(),
                               mainGame->dInfo.hostname);
      mainGame->HideElement(mainGame->wSinglePlay);
      mainGame->ClearCardInfo();
      mainGame->wCardImg->setVisible(true);
      mainGame->wInfos->setVisible(true);
      mainGame->btnLeaveGame->setVisible(true);
      mainGame->btnLeaveGame->setText(dataManager.GetSysString(1210));
      mainGame->wPhase->setVisible(true);
      mainGame->dField.Clear();
      mainGame->dInfo.isFirst = true;
      mainGame->dInfo.isStarted = true;
      mainGame->dInfo.isSingleMode = true;
      mainGame->device->setEventReceiver(&mainGame->dField);
    }
    // A read-only bootstrap gives the journal a base even for scripts that do
    // not emit Debug.ReloadFieldEnd. Later actual reload messages remain in
    // order.
    runtime->journal.push_back(session->Live().QueryInfo());
    for (uint8_t p = 0; p < 2; ++p)
      for (uint8_t loc :
           {LOCATION_DECK, LOCATION_HAND, LOCATION_MZONE, LOCATION_SZONE,
            LOCATION_GRAVE, LOCATION_REMOVED, LOCATION_EXTRA}) {
        auto query = session->Live().QueryField(p, loc, 0xefdfff);
        undo::Bytes frame{MSG_UPDATE_DATA, p, loc};
        frame.insert(frame.end(), query.begin(), query.end());
        runtime->journal.push_back(std::move(frame));
      }
    auto output = [](const undo::Bytes &frame) {
      auto copy = frame;
      if (!SinglePlayAnalyze(copy.data(), copy.size()))
        is_continuing = false;
    };
    auto boundary = session->Advance(output);
    while (is_continuing) {
      if (boundary.kind != undo::BoundaryKind::AwaitResponse) {
        session->AtBoundary(true);
        break;
      }
      // A successful transaction already installed its pristine prompt. No
      // query, history copy, journal append or renderer runs after that switch.
      if (runtime->installedBoundary) {
        boundary = std::move(*runtime->installedBoundary);
        runtime->installedBoundary.reset();
      } else {
        auto count = session->History().size();
        runtime->boundaries.resize(count + 1);
        runtime->boundaries[count].cursor = runtime->journal.size();
        runtime->boundaries[count].context = DuelClient::CapturePromptContext();
        if (session->State() == undo::LocalUndoState::WaitBoundary)
          session->AtBoundary(false);
        if (runtime->installedBoundary) {
          boundary = std::move(*runtime->installedBoundary);
          runtime->installedBoundary.reset();
        } else {
          auto prompt = boundary.checkpoint.prompt;
          if (prompt[0] == MSG_SELECT_IDLECMD || prompt[0] == MSG_SELECT_BATTLECMD)
            SinglePlayRefresh();
          auto &visible = runtime->boundaries[count];
          visible.cursor = runtime->journal.size();
          visible.context = DuelClient::CapturePromptContext();
          // UI admission stays closed until the live prompt's pristine widget
          // snapshot is complete. Its internal Automatic responses keep their
          // original thread/token/provenance and may still queue normally.
          runtime->capturingPrompt = true;
          analyzingLivePrompt = true;
          bool automatic;
          try {
            automatic = AnalyzeVisible(prompt.data(), prompt.size());
            std::lock_guard<std::mutex> lock(mainGame->gMutex);
            visible.presentation = CaptureUndoPrompt(*mainGame);
            visible.presentedContext = DuelClient::CapturePromptContext();
          } catch (...) {
            analyzingLivePrompt = false;
            runtime->capturingPrompt = false;
            throw;
          }
          analyzingLivePrompt = false;
          runtime->capturingPrompt = false;
          if (automatic) DuelClient::SendResponse();
        }
      }
      bool restored = false;
      while (is_continuing) {
        if (session->State() == undo::LocalUndoState::WaitBoundary) {
          auto old = session->Token().epoch;
          session->AtBoundary(false);
          if (session->Token().epoch != old) {
            restored = true;
            break;
          }
        }
        if (session->HasPendingResponse())
          break;
        mainGame->singleSignal.Wait();
      }
      if (!is_continuing)
        break;
      if (!restored) {
        boundary = session->Advance(output);
        if (boundary.rejectedResponse) {
          auto &b = runtime->boundaries[session->History().size()];
          runtime->journal.resize(b.cursor);
          DuelClient::RestorePromptContext(b.context);
        }
      }
    }
    if (boundary.kind == undo::BoundaryKind::Failed)
      mainGame->ErrorLog(boundary.failure.c_str());
    session->AtBoundary(true);
    mainGame->dInfo.isFinished = true;
    last_replay.RecordUndoSingle(session->Live().Initial(), session->History(),
                                 mainGame->dInfo.hostname,
                                 mainGame->dInfo.clientname);
    mainGame->gMutex.lock();
    time_t nowtime = std::time(nullptr);
    wchar_t timetext[40]{};
    std::wcsftime(timetext, 40, L"%Y-%m-%d %H-%M-%S", std::localtime(&nowtime));
    mainGame->ebRSName->setText(timetext);
    if (!mainGame->chkAutoSaveReplay->isChecked()) {
      mainGame->wReplaySave->setText(dataManager.GetSysString(1340));
      mainGame->PopupElement(mainGame->wReplaySave);
      mainGame->gMutex.unlock();
      mainGame->replaySignal.Wait();
    } else {
      mainGame->actionParam = 1;
      mainGame->gMutex.unlock();
    }
    if (mainGame->actionParam)
      last_replay.SaveReplay(mainGame->ebRSName->getText());
    {
      std::lock_guard<std::mutex> lock(activeMutex);
      activeRuntime.reset();
    }
    if (!is_closing) {
      mainGame->gMutex.lock();
      mainGame->dInfo.isStarted = false;
      mainGame->dInfo.isInDuel = false;
      mainGame->dInfo.isFinished = true;
      mainGame->dInfo.isSingleMode = false;
      mainGame->gMutex.unlock();
      mainGame->closeDoneSignal.Reset();
      mainGame->closeSignal.Set();
      mainGame->closeDoneSignal.Wait();
      mainGame->gMutex.lock();
      mainGame->ShowElement(mainGame->wSinglePlay);
      mainGame->stTip->setVisible(false);
      mainGame->device->setEventReceiver(&mainGame->menuHandler);
      mainGame->gMutex.unlock();
      if (mainGame->exit_on_return)
        mainGame->device->closeDevice();
    }
  } catch (const std::exception &e) {
    mainGame->ErrorLog(e.what());
    {
      std::lock_guard<std::mutex> lock(activeMutex);
      activeRuntime.reset();
      launchPending = false;
    }
    is_continuing = false;
    std::lock_guard<std::mutex> lock(mainGame->gMutex);
    mainGame->dInfo.isStarted = false;
    mainGame->dInfo.isSingleMode = false;
    mainGame->dInfo.isFinished = true;
    mainGame->CloseGameWindow();
    mainGame->btnLeaveGame->setVisible(false);
    mainGame->ShowElement(mainGame->wSinglePlay);
    mainGame->device->setEventReceiver(&mainGame->menuHandler);
  }
}
bool SingleMode::SinglePlayAnalyze(unsigned char *msg, unsigned int len) {
  unsigned char *offset, *pbuf = msg;
  int player, count;
  while (pbuf - msg < (int)len) {
    if (is_closing || !is_continuing)
      return false;
    offset = pbuf;
    mainGame->dInfo.curMsg = BufferIO::Read<uint8_t>(pbuf);
    switch (mainGame->dInfo.curMsg) {
    case MSG_RETRY: {
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_HINT: {
      /*int type = */ BufferIO::Read<uint8_t>(pbuf);
      player = BufferIO::Read<uint8_t>(pbuf);
      /*int data = */ BufferIO::Read<int32_t>(pbuf);
      if (player == 0)
        AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_WIN: {
      pbuf += 2;
      AnalyzeVisible(offset, pbuf - offset);
      return false;
    }
    case MSG_SELECT_BATTLECMD: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 11;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 8 + 2;
      SinglePlayRefresh();
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_IDLECMD: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 11 + 3;
      SinglePlayRefresh();
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_EFFECTYN: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 12;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_YESNO: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 4;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_OPTION: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_CARD:
    case MSG_SELECT_TRIBUTE: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 3;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 8;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_UNSELECT_CARD: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 4;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 8;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 8;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_CHAIN: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 9 + count * 14;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_PLACE:
    case MSG_SELECT_DISFIELD: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 5;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_POSITION: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 5;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_COUNTER: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 4;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 9;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SELECT_SUM: {
      pbuf++;
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 6;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 11;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 11;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_SORT_CARD: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_CONFIRM_DECKTOP: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CONFIRM_EXTRATOP: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CONFIRM_CARDS: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 1;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 7;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SHUFFLE_DECK: {
      player = BufferIO::Read<uint8_t>(pbuf);
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefreshDeck(player);
      break;
    }
    case MSG_SHUFFLE_HAND: {
      /*int oplayer = */ BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SHUFFLE_EXTRA: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_REFRESH_DECK: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SWAP_GRAVE_DECK: {
      player = BufferIO::Read<uint8_t>(pbuf);
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefreshGrave(player);
      break;
    }
    case MSG_REVERSE_DECK: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefreshDeck(0);
      SinglePlayRefreshDeck(1);
      break;
    }
    case MSG_DECK_TOP: {
      pbuf += 6;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SHUFFLE_SET_CARD: {
      pbuf++;
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_NEW_TURN: {
      player = BufferIO::Read<uint8_t>(pbuf);
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_NEW_PHASE: {
      pbuf += 2;
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_MOVE: {
      int pc = pbuf[4];
      int pl = pbuf[5];
      /*int ps = pbuf[6];*/
      /*int pp = pbuf[7];*/
      int cc = pbuf[8];
      int cl = pbuf[9];
      int cs = pbuf[10];
      /*int cp = pbuf[11];*/
      pbuf += 16;
      AnalyzeVisible(offset, pbuf - offset);
      if (cl && !(cl & LOCATION_OVERLAY) && (pl != cl || pc != cc))
        SinglePlayRefreshSingle(cc, cl, cs);
      break;
    }
    case MSG_POS_CHANGE: {
      pbuf += 9;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SET: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SWAP: {
      pbuf += 16;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_FIELD_DISABLED: {
      pbuf += 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SUMMONING: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SUMMONED: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_SPSUMMONING: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_SPSUMMONED: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_FLIPSUMMONING: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_FLIPSUMMONED: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_CHAINING: {
      pbuf += 16;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CHAINED: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_CHAIN_SOLVING: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CHAIN_SOLVED: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_CHAIN_END: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      SinglePlayRefreshDeck(0);
      SinglePlayRefreshDeck(1);
      break;
    }
    case MSG_CHAIN_NEGATED: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CHAIN_DISABLED: {
      pbuf++;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CARD_SELECTED:
    case MSG_RANDOM_SELECTED: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_BECOME_TARGET: {
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_DRAW: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_DAMAGE: {
      pbuf += 5;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_RECOVER: {
      pbuf += 5;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_EQUIP: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_LPUPDATE: {
      pbuf += 5;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_UNEQUIP: {
      pbuf += 4;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CARD_TARGET: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_CANCEL_TARGET: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_PAY_LPCOST: {
      pbuf += 5;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_ADD_COUNTER: {
      pbuf += 7;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_REMOVE_COUNTER: {
      pbuf += 7;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_ATTACK: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_BATTLE: {
      pbuf += 26;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_ATTACK_DISABLED: {
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_DAMAGE_STEP_START: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_DAMAGE_STEP_END: {
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefresh();
      break;
    }
    case MSG_MISSED_EFFECT: {
      pbuf += 8;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_TOSS_COIN: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_TOSS_DICE: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_ROCK_PAPER_SCISSORS: {
      player = BufferIO::Read<uint8_t>(pbuf);
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_HAND_RES: {
      pbuf += 1;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_ANNOUNCE_RACE: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 5;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_ANNOUNCE_ATTRIB: {
      player = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 5;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_ANNOUNCE_CARD:
    case MSG_ANNOUNCE_NUMBER: {
      player = BufferIO::Read<uint8_t>(pbuf);
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += 4 * count;
      if (!AnalyzeVisible(offset, pbuf - offset)) {
        // Input waiting is owned by SinglePlayThread at an accepted boundary.
      }
      break;
    }
    case MSG_CARD_HINT: {
      pbuf += 9;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_PLAYER_HINT: {
      pbuf += 6;
      AnalyzeVisible(offset, pbuf - offset);
      break;
    }
    case MSG_TAG_SWAP: {
      player = pbuf[0];
      pbuf += pbuf[2] * 4 + pbuf[4] * 4 + 9;
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayRefreshDeck(player);
      SinglePlayRefreshExtra(player);
      break;
    }
    case MSG_MATCH_KILL: {
      pbuf += 4;
      break;
    }
    case MSG_RELOAD_FIELD: {
      pbuf++;
      for (int p = 0; p < 2; ++p) {
        pbuf += 4;
        for (int seq = 0; seq < 7; ++seq) {
          int val = BufferIO::Read<uint8_t>(pbuf);
          if (val)
            pbuf += 2;
        }
        for (int seq = 0; seq < 8; ++seq) {
          int val = BufferIO::Read<uint8_t>(pbuf);
          if (val)
            pbuf++;
        }
        pbuf += 6;
      }
      count = BufferIO::Read<uint8_t>(pbuf);
      pbuf += count * 15;
      AnalyzeVisible(offset, pbuf - offset);
      SinglePlayReload();
      mainGame->gMutex.lock();
      mainGame->dField.RefreshAllCards();
      mainGame->gMutex.unlock();
      break;
    }
    case MSG_AI_NAME: {
      char namebuf[SIZE_AI_NAME]{};
      wchar_t wname[20]{};
      int name_len = BufferIO::Read<uint16_t>(pbuf);
      if (name_len + 1 <= (int)sizeof namebuf) {
        std::memcpy(namebuf, pbuf, name_len);
        namebuf[name_len] = 0;
      }
      pbuf += name_len + 1;
      BufferIO::DecodeUTF8(namebuf, wname);
      BufferIO::CopyCharArray(wname, mainGame->dInfo.clientname);
      break;
    }
    case MSG_SHOW_HINT: {
      char msgbuf[SIZE_HINT_MSG]{};
      wchar_t msg[SIZE_HINT_MSG]{};
      int msg_len = BufferIO::Read<uint16_t>(pbuf);
      if (msg_len + 1 <= (int)sizeof msgbuf) {
        std::memcpy(msgbuf, pbuf, msg_len);
        msgbuf[msg_len] = 0;
      }
      pbuf += msg_len + 1;
      BufferIO::DecodeUTF8(msgbuf, msg);
      mainGame->gMutex.lock();
      mainGame->SetStaticText(mainGame->stMessage, 310, mainGame->guiFont, msg);
      mainGame->PopupElement(mainGame->wMessage);
      mainGame->gMutex.unlock();
      mainGame->actionSignal.Reset();
      mainGame->actionSignal.Wait();
      break;
    }
    }
  }
  return is_continuing;
}
inline void SingleMode::ReloadLocation(int player, int location, int flag, std::vector<unsigned char>& queryBuffer) {
	auto session=ActiveSession();if(!session)return;
 queryBuffer=session->Live().QueryField(player,location,flag & 0xefffff);
 undo::Bytes frame{MSG_UPDATE_DATA,uint8_t(player),uint8_t(location)};frame.insert(frame.end(),queryBuffer.begin(),queryBuffer.end());RecordVisible(frame);
	mainGame->dField.UpdateFieldCard(mainGame->LocalPlayer(player), location, queryBuffer.data());
}
void SingleMode::SinglePlayRefresh(int flag) {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(SIZE_QUERY_BUFFER);
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);
}
void SingleMode::SingleRefreshLocation(int player, int location, int flag) {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(SIZE_QUERY_BUFFER);
	ReloadLocation(player, location, flag, queryBuffer);
}
inline void SingleMode::SinglePlayRefreshHand(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_HAND, flag);
}
inline void SingleMode::SinglePlayRefreshGrave(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_GRAVE, flag);
}
inline void SingleMode::SinglePlayRefreshDeck(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_DECK, flag);
}
inline void SingleMode::SinglePlayRefreshExtra(int player, int flag) {
	SingleRefreshLocation(player, LOCATION_EXTRA, flag);
}
void SingleMode::SinglePlayRefreshSingle(int player, int location, int sequence, int flag) {
	auto session=ActiveSession();if(!session)return;auto queryBuffer=session->Live().QueryCard(player,location,sequence,flag & 0xefffff);
 undo::Bytes frame{MSG_UPDATE_CARD,uint8_t(player),uint8_t(location),uint8_t(sequence)};frame.insert(frame.end(),queryBuffer.begin(),queryBuffer.end());RecordVisible(frame);
 mainGame->dField.UpdateCard(mainGame->LocalPlayer(player),location,sequence,queryBuffer.data());
}
void SingleMode::SinglePlayReload() {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.resize(SIZE_QUERY_BUFFER);
	unsigned int flag = 0xefdfff;
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);

	ReloadLocation(0, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(1, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(0, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(1, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(0, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_REMOVED, flag, queryBuffer);
	ReloadLocation(1, LOCATION_REMOVED, flag, queryBuffer);
}

} // namespace ygo
