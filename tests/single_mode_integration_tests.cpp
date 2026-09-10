#include "client_card.h"
#include "duelclient.h"
#include "game.h"
#define private public
#include "replay_mode.h"
#undef private
#include "single_mode.h"
#include "undo_prompt.h"
#include "test_support.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <windows.h>
using namespace ygo;
static Game game;
// Actual GUI factory failure during hidden widget construction, after the
// separate N3 field has prepared. No production-only fault switch is needed.
class FailingWidgetFactory final : public irr::gui::IGUIElementFactory {
public:
  std::atomic<int> remaining{-1};
  std::atomic<int> calls{0};
  irr::gui::IGUIElement* addGUIElement(const char*, irr::gui::IGUIElement*) override {
    if (remaining >= 0) {
      ++calls;
      if (remaining-- == 0) throw std::bad_alloc();
    }
    return nullptr;
  }
  irr::gui::IGUIElement* addGUIElement(irr::gui::EGUI_ELEMENT_TYPE, irr::gui::IGUIElement*) override { return nullptr; }
  irr::s32 getCreatableGUIElementTypeCount() const override { return 0; }
  irr::gui::EGUI_ELEMENT_TYPE getCreateableGUIElementType(irr::s32) const override { return irr::gui::EGUIET_ELEMENT; }
  const char* getCreateableGUIElementTypeName(irr::s32) const override { return nullptr; }
  const char* getCreateableGUIElementTypeName(irr::gui::EGUI_ELEMENT_TYPE) const override { return nullptr; }
};
static void pump() {
  game.device->run();
  {
    std::lock_guard<std::mutex> lock(game.gMutex);
    game.DrawGUI();
  }
  if (game.closeSignal.TryWait())
    game.CloseDuelWindow();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
template <class F> static void until(F test) {
  for (int i = 0; i < 15000; ++i) {
    pump();
    if (test())
      return;
  }
  auto s = SingleMode::ActiveSession();
  std::cerr << "timeout msg=" << unsigned(game.dInfo.curMsg)
            << " session=" << bool(s) << " paused=" << SingleMode::InputPaused()
            << " error=" << SingleMode::LastUndoError() << "\n";
  if (s)
    std::cerr << "history=" << s->History().size()
              << " seq=" << s->Token().prompt << " epoch=" << s->Token().epoch
              << " kind=" << int(s->Current().kind)
              << " failure=" << s->Current().failure << "\n";
  throw std::runtime_error("Game integration timeout");
}
static void button(irr::gui::IGUIButton *control) {
  irr::SEvent e{};
  e.EventType = irr::EET_GUI_EVENT;
  e.GUIEvent.Caller = control;
  e.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
  game.dField.OnEvent(e);
}
static uint32_t u32(const undo::Bytes &b, size_t at) {
  return uint32_t(b.at(at)) | uint32_t(b.at(at + 1)) << 8 |
         uint32_t(b.at(at + 2)) << 16 | uint32_t(b.at(at + 3)) << 24;
}
static int activation(const undo::Bytes &p, uint32_t code) {
  CHECK(p.at(0) == MSG_SELECT_IDLECMD);
  size_t at = 2;
  for (int i = 0; i < 5; ++i) {
    auto n = p.at(at++);
    at += 7 * n;
  }
  auto n = p.at(at++);
  for (int i = 0; i < n; ++i, at += 11)
    if (u32(p, at) == code)
      return (i << 16) | 5;
  throw std::runtime_error("Card activation absent");
}
static void submit(int value) {
  DuelClient::SetResponseI(value);
  DuelClient::SendResponse();
}
static bool idle(const std::shared_ptr<undo::SingleUndo> &s,
                 uint64_t after = 0) {
  return s->Token().prompt > after && !s->HasPendingResponse() &&
         game.dInfo.curMsg == MSG_SELECT_IDLECMD &&
         !SingleMode::InputPaused() && game.btnEP->isVisible();
}
int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  try {
    auto started=std::chrono::steady_clock::now();std::ofstream evidence("c4-evidence.txt");
    mainGame = &game;
    CHECK(game.Initialize(std::filesystem::current_path()));
    auto* failingWidgets = new FailingWidgetFactory;
    game.env->registerGUIElementFactory(failingWidgets);
    failingWidgets->drop();
    game.frameSignal.SetNoWait(true);
    game.actionSignal.SetNoWait(true);
    game.chkSTAutoPos->setChecked(true);
    game.chkAutoSaveReplay->setChecked(false);
    if (std::getenv("C4_REPLAY_FAILURE_ONLY")) {
      CHECK(ReplayMode::cur_replay.OpenReplay(L"c4-single-current-branch.yrp"));
      game.wReplay->setVisible(false);
      game.actionSignal.SetNoWait(false);
      ReplayMode::Pause(true, false);
      CHECK(ReplayMode::StartReplay(0));
      until([] { return ReplayMode::is_paused && ReplayMode::current_step > 0; });
      // Only the future recreation is invalidated; the active frozen core is
      // already running. This exercises StartDuel's real failure result.
      auto& recreate = const_cast<undo::InitialState&>(ReplayMode::cur_replay.UndoInitial());
      recreate.resourceDigest[0] ^= 1;
      std::thread watchdog([] {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        std::cerr << "Replay backstep failed responsiveness deadline\n";
        std::_Exit(3);
      });
      watchdog.detach();
      ReplayMode::Undo();
      until([] {
        if (game.wMessage->isVisible()) game.actionSignal.Set();
        return game.wReplay->isVisible();
      });
      CHECK(!game.dInfo.isReplay && game.dInfo.isFinished);
      CHECK(ReplayMode::pduel == 0);
      CHECK(game.gMutex.try_lock()); game.gMutex.unlock();
      std::cout << "Actual paused replay recreation failure remains responsive\n";
      game.device->closeDevice(); game.device->drop();
      return 0;
    }
    {
      std::lock_guard<std::mutex> lock(game.gMutex);
      game.wANCard->setVisible(true);
      game.ebANCard->setText(L"pristine declaration");
      game.cbANNumber->clear(); game.cbANNumber->addItem(L"99",99);
      game.dField.panel=game.wANCard;
      game.dField.ancard={70368879,37812118};
      auto* originalCard=game.dField.CreateCard();
      game.dField.AddCard(originalCard,0,LOCATION_HAND,0);
      originalCard->is_selectable=true;
      game.dField.selectable_cards={originalCard};
      auto* inactiveCard=game.dField.CreateCard();
      game.dField.DestroyCard(inactiveCard);
      game.dField.display_cards={inactiveCard}; // legacy inactive cache after Clear
      game.env->setFocus(game.ebANCard);
      auto pristine=CaptureUndoPrompt(game);
      game.ebANCard->setText(L"unsubmitted edit");
      game.dField.ancard={89631139};
      auto* originalEdit=game.ebANCard;
      auto* originalPanel=game.dField.panel;
      failingWidgets->remaining=1;
      bool failed=false;
      try { auto candidate=PrepareUndoPrompt(game,*pristine); }
      catch(const std::bad_alloc&) { failed=true; }
      failingWidgets->remaining=-1;
      CHECK(failed);
      CHECK(game.ebANCard==originalEdit && game.env->getFocus()==originalEdit);
      CHECK(game.dField.panel==originalPanel);
      CHECK(std::wstring(game.ebANCard->getText())==L"unsubmitted edit");
      auto candidateModel=std::make_unique<ClientField>();
      auto* candidateCard=candidateModel->CreateCard();
      candidateModel->AddCard(candidateCard,0,LOCATION_HAND,0);
      auto candidate=PrepareUndoPrompt(game,*pristine,candidateModel.get());
      CHECK(game.ebANCard==originalEdit && game.env->getFocus()==originalEdit);
      game.dField.SwapPreparedModel(*candidateModel);
      candidate->Install();
      CHECK(game.ebANCard!=originalEdit && game.env->getFocus()==game.ebANCard);
      CHECK(game.dField.panel==game.wANCard);
      CHECK(std::wstring(game.ebANCard->getText())==L"pristine declaration");
      CHECK(game.ebANCard->getID()==EDITBOX_ANCARD);
      CHECK(game.cbANNumber->getItemData(0)==99);
      CHECK((game.dField.ancard==std::vector<int>{70368879,37812118}));
      CHECK(game.dField.selectable_cards.size()==1);
      CHECK(game.dField.selectable_cards.front()==candidateCard);
      CHECK(game.dField.selectable_cards.front()!=originalCard);
      CHECK(candidateCard->is_selectable);
      CHECK(game.dField.display_cards.empty());
      game.wANCard->setVisible(false);
      game.env->installPreparedFocus(nullptr);
      std::cout<<"Prepared focused edit and combo value bank installs without live mutation\n";
    }
    undo::Bytes scenarioBytes;
    for (unsigned policy = 0; policy < 4; ++policy) {
      game.chkNoCheckDeck->setChecked(policy & 1);
      game.chkNoShuffleDeck->setChecked(policy & 2);
      game.wMainMenu->setVisible(false);
      game.open_file = true;
      const std::wstring scenarioForms[] = {
          (game.runtime_root / "single" / "c4-single.lua").wstring(),
          L"single\\c4-single.lua", L"./single/c4-single.lua", L"c4-single.lua"};
      BufferIO::CopyWideString(scenarioForms[policy].c_str(), game.open_file_name);
      std::thread worker(SingleMode::SinglePlayThread);
      try {
        until([] { return bool(SingleMode::ActiveSession()); });
        auto s = SingleMode::ActiveSession();
        until([&] { return idle(s); });
        std::cerr << "initial idle\n";
        CHECK(!SingleMode::CanUndo(0));
        CHECK(game.dField.hand[0].size() == 2);
        auto initial = s->Current().checkpoint;
        auto first = s->Token();
        const auto& init=s->Live().Initial();
        CHECK(init.scenarioName == "single/c4-single.lua");
        auto frozenScenario = s->Live().Resources()->Read(init.scenarioName);
        CHECK(!frozenScenario.empty());
        if (policy == 0) scenarioBytes = frozenScenario;
        else CHECK(scenarioBytes == frozenScenario);
        evidence<<"policy="<<policy<<" scenario="<<init.scenarioName<<" seed=";
        for(auto word:init.seed)evidence<<word<<',';
        evidence<<" resources=";for(auto byte:init.resourceDigest)evidence<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);evidence<<std::dec;
        evidence<<" scripts="<<s->Live().Resources()->ScriptCount()<<" cards="<<s->Live().Resources()->Cards().size()<<" target=";
        for(auto byte:initial.transcriptDigest)evidence<<std::hex<<std::setw(2)<<unsigned(byte);evidence<<std::dec<<'\n';
        CHECK(!s->Live().Initial().noCheckDeck);
        CHECK(!s->Live().Initial().noShuffleDeck);
        undo::Bytes maximum(256, 0x5a);
        DuelClient::SetResponseB(maximum.data(), maximum.size());
        CHECK(DuelClient::CaptureResponse().response == maximum);
        maximum.push_back(1);
        DuelClient::SetResponseB(maximum.data(), maximum.size());
        CHECK(DuelClient::CaptureResponse().response.empty());
        DuelClient::SendResponse();
        CHECK(!s->HasPendingResponse());
        submit(99);
        until([&] { return idle(s); });
        CHECK(s->History().empty());
        std::cerr << "retry done\n";
        int a = activation(initial.prompt, 70368879);
        submit(a);
        until([&] { return idle(s, first.prompt); });
        std::cerr << "A resolved\n";
        CHECK(s->History().size() >= 1);
        CHECK(s->History().front().origin == undo::Origin::Manual);
        for (size_t i = 1; i < s->History().size(); ++i)
          CHECK(s->History()[i].origin == undo::Origin::Automatic);
        CHECK(game.dInfo.lp[1] == 9000);
        CHECK(game.dField.grave[0].size() == 1);
        auto beforeFailure = s->Current();
        auto beforeClock = s->Clock();
        auto beforeCount = s->History().size();
        auto originalPrepare =
            s->SetPrepare([](const undo::CoreDriver &, const undo::Checkpoint &,
                             uint64_t) -> std::unique_ptr<undo::PreparedUndo> {
              throw std::runtime_error("Game injected prepare failure");
            });
        button(game.btnUndoDuel);
        until([&] { return s->State() == undo::LocalUndoState::Failed; });
        CHECK(undo::SamePosition(s->Current().checkpoint,
                                 beforeFailure.checkpoint));
        CHECK(s->History().size() == beforeCount);
        CHECK(s->Clock().remainingMs == beforeClock.remainingMs);
        CHECK(game.dInfo.lp[1] == 9000);
        CHECK(game.dField.grave[0].size() == 1);
        CHECK(game.btnEP->isVisible());
        s->SetPrepare(std::move(originalPrepare));
        const auto widgetEpoch = s->Token();
        auto* oldQuery = game.wQuery;
        auto* oldEndButton = game.btnEP;
        auto* oldFocus = game.env->getFocus();
        failingWidgets->remaining = 1;
        button(game.btnUndoDuel);
        until([&] { return s->State() == undo::LocalUndoState::Failed || s->Token().epoch != widgetEpoch.epoch; });
        failingWidgets->remaining = -1;
        CHECK(s->State() == undo::LocalUndoState::Failed);
        CHECK(failingWidgets->calls >= 2);
        CHECK(s->Token() == widgetEpoch);
        CHECK(game.wQuery == oldQuery && game.btnEP == oldEndButton);
        CHECK(game.env->getFocus() == oldFocus);
        CHECK(undo::SamePosition(s->Current().checkpoint, beforeFailure.checkpoint));
        CHECK(s->History().size() == beforeCount);
        CHECK(s->Clock().remainingMs == beforeClock.remainingMs);
        CHECK(game.dInfo.lp[1] == 9000 && game.dField.grave[0].size() == 1);
        CHECK(game.btnEP->isVisible());

        DuelClient::SetResponseI(activation(s->Current().checkpoint.prompt,37812118));
        game.HideElement(game.wQuery,true);
        auto stale = game.fadingList.back().response;
        CHECK(stale.origin==undo::Origin::Manual);CHECK(stale.token==s->Token());
        auto epoch = s->Token().epoch;
        button(game.btnUndoDuel);
        until([&] { return s->Token().epoch == epoch + 1 && idle(s); });
        std::cerr << "restore done\n";
        CHECK(s->History().empty());
        CHECK(std::none_of(game.fadingList.begin(),game.fadingList.end(),[](const FadingUnit& f){return f.signalAction;}));
        CHECK(undo::SamePosition(s->Current().checkpoint, initial));
        CHECK(game.dInfo.lp[1] == 8000);
        CHECK(game.dField.hand[0].size() == 2);
        CHECK(game.dField.grave[0].empty());
        DuelClient::SendResponse(stale);
        CHECK(!s->HasPendingResponse());
        CHECK(game.btnEP->isVisible());
        // Repeat A and undo once more before selecting the different real-card
        // branch.
        auto seq = s->Token().prompt;
        submit(activation(s->Current().checkpoint.prompt, 70368879));
        until([&] { return idle(s, seq); });
        epoch = s->Token().epoch;
        button(game.btnUndoDuel);
        until([&] { return s->Token().epoch == epoch + 1 && idle(s); });
        CHECK(s->History().empty());
        submit(activation(s->Current().checkpoint.prompt, 37812118));
        until([&] { return game.wReplaySave->isVisible(); });
        CHECK(game.dInfo.isFinished);
        CHECK(!s->CanUndo(0));
        CHECK(!s->History().empty());
        CHECK(u32(s->History().front().response, 0) ==
              uint32_t(activation(initial.prompt, 37812118)));
        evidence<<"retained=";for(const auto& rec:s->History()){evidence<<"["<<int(rec.origin)<<":";for(auto b:rec.response)evidence<<std::hex<<std::setw(2)<<unsigned(b);evidence<<std::dec<<"]";}evidence<<'\n';evidence.flush();
        game.ebRSName->setText(L"c4-single-current-branch");
        game.actionParam = 1;
        game.replaySignal.Set();
        until([] { return !SingleMode::ActiveSession(); });
        until([] { return !game.dInfo.isSingleMode; });
        worker.join();
        CHECK(std::filesystem::exists("replay/c4-single-current-branch.yrp"));
        game.wSinglePlay->setVisible(false);
        game.wReplay->setVisible(false);
        game.dField.Clear();
        CHECK(
            ReplayMode::cur_replay.OpenReplay(L"c4-single-current-branch.yrp"));
        CHECK(ReplayMode::StartReplay(0));
        until([] { return game.wReplay->isVisible(); });
        CHECK(game.dInfo.isFinished);
        CHECK(!game.dInfo.isReplay);
        CHECK(game.dInfo.curMsg == MSG_WIN);
        CHECK(game.dInfo.lp[1] == 8000);
        CHECK(game.dField.grave[0].size() == 1);
        CHECK(game.dField.grave[0][0]->code == 37812118);
        CHECK(std::any_of(game.dField.hand[0].begin(),
                          game.dField.hand[0].end(),
                          [](ClientCard *c) { return c->code == 70368879; }));
        if(policy == 0) {
          Replay network;
          network.RecordUndoDuel(s->Live().Initial(), s->History(), L"host", L"peer");
          auto packet = network.ExportUndoReplay();
          game.wReplay->setVisible(false);
          game.dField.Clear();
          CHECK(ReplayMode::cur_replay.LoadUndoReplay(packet));
          CHECK(!(ReplayMode::cur_replay.pheader.base.flag & REPLAY_SINGLE_MODE));
          // The previous replay window can still have a pending fade-in.
          // Wait for this playback to finish, not just for that old animation.
          game.dInfo.isFinished = false;
          CHECK(ReplayMode::StartReplay(0));
          until([] { return game.wReplay->isVisible() && game.dInfo.isFinished && !game.dInfo.isReplay; });
          CHECK(game.dInfo.isFinished && !game.dInfo.isReplay);
          CHECK(game.dInfo.curMsg == MSG_WIN && game.dInfo.lp[1] == 8000);
          CHECK(game.dField.grave[0].size() == 1 && game.dField.grave[0][0]->code == 37812118);
          CHECK(std::any_of(game.dField.hand[0].begin(), game.dField.hand[0].end(),
                            [](ClientCard* c) { return c->code == 70368879; }));
          std::cout << "Actual non-SINGLE memory ReplayMode bootstrap and retained B-only playback passed\n";
        }
        std::cout << "actual Game SingleMode + ReplayMode: retry, "
                     "manual/automatic input, A undo A undo B end save, epoch "
                     "rejection, LP/card restoration passed\n";
      } catch (...) {
        SingleMode::StopPlay(true);
        game.actionParam = 0;
        game.replaySignal.Set();
        game.closeDoneSignal.Set();
        if (worker.joinable())
          worker.join();
        throw;
      }
      std::cout << "host options=" << policy
                << " remain isolated from SingleMode scenario; actual replay "
                   "passed\n";
    }
    // Replay back-step recreates the initial visible board immediately, even
    // before its queued Debug reload message is processed again.
    CHECK(ReplayMode::cur_replay.OpenReplay(L"c4-single-current-branch.yrp"));
    game.dInfo.isReplay = true;
    game.dInfo.isSingleMode = true;
    game.dInfo.isFirst = true;
    ReplayMode::Restart(true);
    const auto restartedHandCount=game.dField.hand[0].size();
    const bool restartedGraveEmpty=game.dField.grave[0].empty();
    const auto restartedLP=game.dInfo.lp[1];
    const bool restartedIdentity=std::any_of(game.dField.hand[0].begin(),game.dField.hand[0].end(),[](ClientCard* c){return c->code==70368879;});
    game.dField.ReplaySwap();
    ReplayMode::Restart(true);
    const auto swappedRestartHandCount=game.dField.hand[1].size();
    const bool swappedRestartIdentity=std::any_of(game.dField.hand[1].begin(),game.dField.hand[1].end(),[](ClientCard* c){return c->code==70368879;});
    ReplayMode::StopReplay(true);
    ReplayMode::EndDuel();
    CHECK(restartedHandCount == 2);
    CHECK(restartedGraveEmpty);
    CHECK(restartedLP == 8000);
    CHECK(restartedIdentity);
    CHECK(swappedRestartHandCount == 2);
    CHECK(swappedRestartIdentity);
    std::cout<<"Replay Restart restores initial model before processing queued frames\n";
    std::cout<<"Game integration elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<"\n";
    game.device->closeDevice();
    game.device->drop();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
