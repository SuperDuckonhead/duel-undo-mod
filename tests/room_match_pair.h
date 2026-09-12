// Two separate initialized Games, TCP clients and native MainLoops. This path
// deliberately leaves frame/action/replay/close signals at production defaults.
namespace match_pair {
struct Run {
  bool host{}, three{}, free{}, passed{}, deckSent{}, started{}, handPosted{};
  HWND window{};
  std::filesystem::path shared;
  Deck deck;
  std::shared_ptr<const ResourceView> resources;
  FailingFactory *factory{};
  unsigned round{1}, saved{}, ticks{}, choiceRound{};
  unsigned sideStage{}, playStage{};
  uint64_t roundEpoch{}, endingEpoch{}, discardPrompt{}, beforeUndoPrompt{};
  InputToken oldToken{}, preservedToken{};
  irr::gui::IGUIButton *preservedButton{};
  int preservedLP{}, preservedClock{};
  ULONGLONG begin{};
  bool roundSeen{}, winSeen{}, replayPosted{}, endPosted{}, surrenderSent{};
  bool failureArmed{}, failureSeen{}, consentPosted{};
  bool sideKeyChecked{}, legalPosted{};
  std::vector<std::vector<uint32_t>> seeds;

  std::string role() const { return host ? "host" : "guest"; }
  std::string tag(const std::string &name) const {
    return "r" + std::to_string(round) + "-" + name;
  }
  void mark(const std::string &name) { std::ofstream(shared / name) << "ok\n"; }
  bool has(const std::string &name) const { return std::filesystem::exists(shared / name); }
  void mouse(irr::gui::IGUIElement *widget, const char *label) {
    CHECK(widget && widget->isTrulyVisible() && widget->isEnabled());
    DWORD owner{}; GetWindowThreadProcessId(window, &owner);
    CHECK(owner == GetCurrentProcessId());
    auto p = widget->getAbsolutePosition().getCenter();
    const auto position = MAKELPARAM(p.X, p.Y);
    CHECK(PostMessageW(window, WM_MOUSEMOVE, 0, position));
    CHECK(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position));
    CHECK(PostMessageW(window, WM_LBUTTONUP, 0, position));
    std::cout << role() << " r" << round << " native mouse " << label << std::endl;
  }
  void menu(irr::gui::IGUIElement *widget) {
    irr::SEvent event{}; event.EventType = irr::EET_GUI_EVENT;
    event.GUIEvent.Caller = widget;
    event.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
    game.menuHandler.OnEvent(event);
  }
  void raw(const Envelope &envelope) {
    auto bytes = Encode(envelope);
    DuelClient::SendBufferToServer(CTOS_UNDO, bytes.data(), bytes.size());
  }
  void preserve(const std::shared_ptr<RoomClient> &room) {
    preservedToken = room->Token(); preservedButton = game.btnEP;
    preservedLP = game.dInfo.lp[0]; preservedClock = game.dInfo.time_left[0];
  }
  void checkPreserved(const std::shared_ptr<RoomClient> &room) {
    CHECK(room->Token() == preservedToken && game.btnEP == preservedButton);
    CHECK(game.dInfo.lp[0] == preservedLP && game.dInfo.time_left[0] == preservedClock);
  }
  irr::gui::IGUIElement *sideError(irr::gui::IGUIElement *node) {
    if (node->getType() == irr::gui::EGUIET_MESSAGE_BOX) {
      std::function<bool(irr::gui::IGUIElement *)> contains = [&](auto *child) {
        if (child->getText() && std::wstring(child->getText()) == dataManager.GetSysString(1408))
          return true;
        for (auto *descendant : child->getChildren()) if (contains(descendant)) return true;
        return false;
      };
      if (contains(node)) return node;
    }
    for (auto *child : node->getChildren()) if (auto *found = sideError(child)) return found;
    return nullptr;
  }
  void verifyReplay(unsigned number) {
    Replay replay;
    const auto name = L"match-game-" + std::to_wstring(number) + L".yrp";
    CHECK(replay.OpenReplay(name.c_str()));
    CHECK(replay.pheader.base.flag & REPLAY_UNDO_CORE);
    const auto &initial = replay.UndoInitial();
    CHECK(initial.cards.size() == 80 && initial.players[0].lp == 8000);
    CHECK(initial.players[1].lp == 8000 && !initial.seed.empty());
    if (!seeds.empty()) CHECK(initial.seed != seeds.back());
    seeds.push_back(initial.seed);
    unsigned sided = 0;
    for (const auto &card : initial.cards) {
      CHECK(card.location == LOCATION_DECK);
      if (card.code == 89631139) ++sided;
    }
    CHECK(sided == (number == 2 ? 2u : 0u));
    // Replay the actual accepted final response branch against the frozen host
    // resources. Surrender is native Match control, so it has no core response.
    if (host) {
      auto core = replay.CreateUndoDriver(resources);
      unsigned responses = 0;
      while (responses < 40) {
        const auto boundary = core->Advance();
        CHECK(boundary.kind != BoundaryKind::Failed && !boundary.rejectedResponse);
        if (boundary.kind == BoundaryKind::Finished) break;
        Bytes response;
        if (!replay.ReadUndoResponse(boundary.checkpoint, response)) break;
        core->Submit(response); ++responses;
      }
      CHECK(responses > 0 && responses < 40);
      if (number == 2) {
        auto field = core->QueryField(0, LOCATION_MZONE, QUERY_CODE);
        auto word = [&](size_t at) {
          CHECK(at + 4 <= field.size());
          return uint32_t(field[at]) | (uint32_t(field[at + 1]) << 8) |
                 (uint32_t(field[at + 2]) << 16) | (uint32_t(field[at + 3]) << 24);
        };
        unsigned summoned = 0;
        for (size_t at = 0; at < field.size();) {
          const auto size = word(at); CHECK(size >= 4 && at + size <= field.size());
          if (size > 4) { CHECK(size >= 12 && (word(at + 4) & QUERY_CODE)); if (word(at + 8) == 15025844) ++summoned; }
          at += size;
        }
        CHECK(summoned == 1); // The actual continued summon is in the saved branch.
      }
      std::cout << "verified final per-game replay round=" << number
                << " responses=" << responses << " sided=" << sided << std::endl;
    }
  }
  void tick() {
    ++ticks;
    std::unique_lock<std::mutex> lock(game.gMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    const auto now = GetTickCount64(); auto room = DuelClient::Room();
    if (ticks % 25 == 0) {
      std::cout << role() << " round=" << round << " play=" << playStage
                << " msg=" << int(game.dInfo.curMsg) << " frame=" << game.signalFrame
                << " side=" << game.is_building << " save=" << game.wReplaySave->isVisible()
                << " hand=" << game.wHand->isVisible() << " first=" << game.wFTSelect->isVisible()
                << " fade=" << game.fadingList.size();
      if (room) std::cout << " paused=" << room->InputPaused() << " epoch=" << room->Token().epoch
                          << " prompt=" << room->Token().prompt << " undo=" << room->CanUndo();
      std::cout << std::endl;
    }
    CHECK(now - begin < 150000);
    if (room && game.wHostPrepare->isVisible()) {
      if (host) mark("host-lobby");
      if (!deckSent) {
        deckManager.current_deck = deck;
        DuelClient::SendUpdateDeck(deck); DuelClient::SendPacketToServer(CTOS_HS_READY);
        deckSent = true;
      }
      if (host && !started && game.chkHostPrepReady[0]->isChecked() && game.chkHostPrepReady[1]->isChecked()) {
        DuelClient::SendPacketToServer(CTOS_HS_START); started = true;
      }
    }
    if (game.wHand->isVisible() && game.fadingList.empty() && !handPosted) {
      mouse(game.btnHand[host ? 0 : 2], "RPS"); handPosted = true;
    }
    if (game.wFTSelect->isVisible() && game.fadingList.empty() && choiceRound != saved + 1) {
      CHECK((saved == 0 && host) || (saved == 1 && !host) || (saved == 2 && host));
      mouse(saved == 2 ? game.btnSecond : game.btnFirst, saved == 2 ? "second" : "first");
      choiceRound = saved + 1;
    }
    if (endPosted && game.wLanWindow->isVisible()) {
      CHECK(saved == (three ? 3u : 2u)); mark("done-" + role());
      if (has("done-host") && has("done-guest")) {
        CHECK(has("failure-preserved-host") && has("failure-preserved-guest"));
        CHECK(has("undo-committed-host") && has("undo-committed-guest") && has("g2-B-visible"));
        CHECK(has("r2-old-input-rejected-host") && has("r2-old-input-rejected-guest"));
        if (free) CHECK(!has("declined"));
        else CHECK(has("decline-preserved-host") && has("decline-preserved-guest"));
        for (unsigned number = 1; number <= saved; ++number) {
          const auto prefix = "r" + std::to_string(number) + "-";
          CHECK(has(prefix + "verify-host") && has(prefix + "verify-guest"));
          if (number < saved) CHECK(has(prefix + "invalid-side-rejected") &&
              has(prefix + "side-no-undo-host") && has(prefix + "side-no-undo-guest"));
        }
        passed = true; KillTimer(window, 31); CHECK(PostMessageW(window, WM_CLOSE, 0, 0));
      }
    }
    if (!room) return;
    if (free) CHECK(!room->NeedsConsent());
    if (roundSeen && game.dInfo.isFinished && !game.is_building) {
      CHECK(!room->CanUndo() && !room->RequestUndo());
      if (game.showcard == 101 && game.signalFrame > 0 && !winSeen) {
        const bool hostWins = round != 2 || !three;
        CHECK(game.showcardcode == (host == hostWins ? 1u : 2u));
        winSeen = true; endingEpoch = room->Token().epoch;
        std::cout << role() << " native win wait frame=" << game.signalFrame << std::endl;
      }
    }
    if (game.wReplaySave->isVisible() && game.fadingList.empty() && !replayPosted) {
      CHECK(winSeen && !room->CanUndo());
      const auto name = L"match-game-" + std::to_wstring(round);
      game.ebRSName->setText(name.c_str());
      mouse(game.btnRSYes, "save per-game replay"); replayPosted = true; saved = round;
    }
    if (saved == round && !game.wReplaySave->isVisible() && has(tag("verify-" + role())) == false &&
        std::filesystem::exists(std::filesystem::path("replay") / ("match-game-" + std::to_string(round) + ".yrp"))) {
      verifyReplay(round); mark(tag("verify-" + role()));
    }
    if (game.is_building && game.is_siding && game.btnSideOK->isTrulyVisible() && saved == round) {
      CHECK(!room->CanUndo() && !room->RequestUndo());
      if (!sideKeyChecked) {
        const auto before = deckManager.current_deck.main;
        irr::SEvent key{}; key.EventType = irr::EET_KEY_INPUT_EVENT;
        key.KeyInput.Key = irr::KEY_KEY_Z; key.KeyInput.Control = true; key.KeyInput.PressedDown = true;
        lock.unlock(); game.device->postEventFromUser(key); lock.lock();
        CHECK(before == deckManager.current_deck.main && !room->CanUndo());
        sideKeyChecked = true; mark(tag("side-no-undo-" + role()));
      }
      if (sideStage == 0 && host) {
        auto invalid = deckManager.current_deck;
        invalid.main[0] = invalid.side[0]; // Same sizes, illegal card multiset.
        DuelClient::SendUpdateDeck(invalid); sideStage = 1;
      }
      if (sideStage == 1 && host) {
        if (auto *error = sideError(game.env->getRootGUIElement())) {
          CHECK(room->Token().epoch == endingEpoch && game.is_siding);
          irr::gui::IGUIElement *button = nullptr;
          for (auto *child : error->getChildren()) if (child->getType() == irr::gui::EGUIET_BUTTON) button = child;
          CHECK(button); mouse(button, "dismiss native illegal-side error");
          mark(tag("invalid-side-rejected")); sideStage = 2;
        }
      }
      if (has(tag("invalid-side-rejected")) && !sideError(game.env->getRootGUIElement()) &&
          game.fadingList.empty() && !legalPosted) {
        std::swap(deckManager.current_deck.main[0], deckManager.current_deck.side[0]);
        mouse(game.btnSideOK, "native legal side confirmation"); legalPosted = true;
      }
    }
    if (saved == round && room->Token().epoch == endingEpoch + 1 && !game.is_building &&
        !game.dInfo.isFinished && game.dInfo.lp[0] == 8000 && room->Token().prompt) {
      ++round; roundEpoch = room->Token().epoch; roundSeen = false; playStage = 0;
      winSeen = replayPosted = surrenderSent = sideKeyChecked = legalPosted = false;
      sideStage = 0; discardPrompt = 0;
    }
    const bool idle = !room->InputPaused() && game.dInfo.curMsg == MSG_SELECT_IDLECMD &&
                      game.fadingList.empty() && !game.dInfo.isFinished;
    if (!roundSeen && room->Token().prompt && !game.dInfo.isFinished && game.dInfo.lp[0] == 8000 &&
        !room->InputPaused() && !game.is_building) {
      CHECK(!room->CanUndo());
      roundSeen = true; roundEpoch = room->Token().epoch;
      CHECK(roundEpoch == (round == 1 ? 0 : round == 2 ? 1 : 3));
      CHECK(game.dInfo.isFirst == (round == 1 ? host : !host));
      if (round == 1) oldToken = room->Token();
      if (round == 2) {
        CHECK(!room->Submit({Bytes{7, 0, 0, 0}, Origin::Manual, oldToken}));
        CHECK(!room->Submit({Bytes{7, 0, 0, 0}, Origin::Automatic, oldToken}));
        // Bypass only the local stale-token check to prove host epoch fencing on
        // the real TCP connection. No old response/request may mutate game two.
        raw(EncodeResponse({oldToken.session, oldToken.epoch, oldToken.prompt, 0, {}}, Origin::Manual, Bytes{7, 0, 0, 0}));
        raw({WireKind::Request, {oldToken.session, oldToken.epoch, 1, 0, {}}, {}});
        mark(tag("old-input-rejected-" + role()));
      }
      mark(tag("start-" + role()));
    }
    if (!room->InputPaused() && game.fadingList.empty() && game.dInfo.curMsg == MSG_SELECT_CARD &&
        room->Token().prompt != discardPrompt) {
      Bytes response{uint8_t(game.dField.select_min)};
      for (unsigned i = 0; i < game.dField.select_min; ++i)
        response.push_back(uint8_t(game.dField.selectable_cards.at(i)->select_seq));
      discardPrompt = room->Token().prompt;
      DuelClient::SetResponseB(response.data(), response.size()); DuelClient::SendResponse();
    }
    if (roundSeen && (round == 1 || round == 3)) {
      const bool firstPlayer = round == 1 ? host : !host;
      if (idle && firstPlayer && playStage == 0 && has(tag("start-host")) && has(tag("start-guest"))) {
        mouse(game.btnEP, "continue initial game"); playStage = 1; mark(tag("first-pass"));
      }
      if (idle && !firstPlayer && has(tag("first-pass"))) mark(tag("continued"));
      if (!host && has(tag("continued")) && !surrenderSent && !room->InputPaused()) {
        DuelClient::SendPacketToServer(CTOS_SURRENDER); surrenderSent = true;
      }
    }
    if (roundSeen && round == 2) playSecond(room, idle);
    if (game.wMessage->isVisible() && game.fadingList.empty() && saved == (three ? 3u : 2u) && !endPosted) {
      CHECK(winSeen && !room->CanUndo());
      mouse(game.btnMsgOK, "native Match-end acknowledgement"); endPosted = true;
    }
  }
  void playSecond(const std::shared_ptr<RoomClient> &room, bool idle) {
    if (room->NeedsConsent() && !consentPosted && game.fadingList.empty()) {
      game.UpdateDuelUndoStatus(); preserve(room);
      mouse(has("decline-request") && !has("declined") ? game.btnUndoDecline : game.btnUndoApprove,
            has("decline-request") && !has("declined") ? "decline game-two undo" : "approve game-two undo");
      consentPosted = true;
      if (has("decline-request") && !has("declined")) mark("declined");
    }
    if (!room->NeedsConsent() && !room->InputPaused()) consentPosted = false;
    if (idle && !host && playStage == 0 && has(tag("start-host")) && has(tag("start-guest"))) {
      mouse(game.btnEP, "game-two branch A"); playStage = 1; mark("g2-A");
    }
    if (idle && host && has("g2-A") && playStage == 0) {
      mouse(game.btnEP, "game-two peer continuation"); playStage = 1; mark("g2-peer-A");
    }
    if (!free && idle && !host && playStage == 1 && has("g2-peer-A") && room->CanUndo()) {
      preserve(room); mark("decline-request"); CHECK(room->RequestUndo()); playStage = 2;
    }
    if (!free && has("declined") && !room->InputPaused() && !has("decline-preserved-" + role())) {
      checkPreserved(room); mark("decline-preserved-" + role());
    }
    if (!free && idle && !host && playStage == 2 && has("decline-preserved-host") && has("decline-preserved-guest")) {
      mouse(game.btnEP, "continued after decline"); playStage = 3; mark("after-decline");
    }
    if (!free && idle && host && playStage == 1 && has("after-decline")) {
      mouse(game.btnEP, "peer continued after decline"); playStage = 2; mark("peer-after-decline");
    }
    if (host && has("arm-failure") && !failureArmed) {
      factory->remaining = 1; preserve(room); failureArmed = true; mark("failure-armed");
    }
    if (idle && !host && playStage == (free ? 1u : 3u) && has(free ? "g2-peer-A" : "peer-after-decline")) {
      mark("arm-failure");
      if (has("failure-armed") && room->CanUndo()) {
        preserve(room); beforeUndoPrompt = room->Token().prompt;
        CHECK(room->RequestUndo()); playStage = 4;
      }
    }
    if (host && failureArmed && factory->calls >= 2 && !room->InputPaused() && !failureSeen) {
      checkPreserved(room); factory->remaining = -1; failureSeen = true; mark("failure-preserved-host");
    }
    if (!host && playStage == 4 && !room->InputPaused() && has("failure-preserved-host")) {
      checkPreserved(room); mark("failure-preserved-guest");
      if (room->CanUndo()) { CHECK(room->RequestUndo()); playStage = 5; }
    }
    if (!room->InputPaused() && room->Token().epoch == roundEpoch + 1) {
      mark("undo-committed-" + role());
      if (idle && !host && playStage == 5 && has("undo-committed-host")) {
        CHECK(room->Token().prompt <= beforeUndoPrompt && game.btnEP != preservedButton);
        DuelClient::SetResponseI(0); DuelClient::SendResponse(); playStage = 6;
      }
      if (idle && !host && playStage == 6 && std::any_of(game.dField.mzone[0].begin(), game.dField.mzone[0].end(), [](auto *c) { return c && c->code == 15025844; })) {
        mouse(game.btnEP, "continued summon after game-two undo"); playStage = 7; mark("g2-branch-B");
      }
      if (idle && host && has("g2-branch-B")) {
        CHECK(std::any_of(game.dField.mzone[1].begin(), game.dField.mzone[1].end(), [](auto *c) { return c && c->code == 15025844; }));
        mark("g2-B-visible");
      }
      if (has("g2-B-visible") && !surrenderSent && host == three) {
        DuelClient::SendPacketToServer(CTOS_SURRENDER); surrenderSent = true;
      }
    }
  }
};
static Run *current{};
static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
  try { current->tick(); }
  catch (const std::exception &error) {
    std::cerr << "FAIL native Match pair " << current->role() << " r" << current->round << ": " << error.what() << std::endl;
    ExitProcess(2);
  }
}
} // namespace match_pair

static int matchPairGame(bool host, const std::filesystem::path &shared, bool three, bool free) {
  using namespace match_pair;
  WSADATA winsock{}; CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  CHECK(evthread_use_windows_threads() == 0);
  Run run; current = &run; run.host = host; run.three = three; run.free = free; run.shared = shared;
  game.gameConf.quick_animation = 0;
  game.chkWaitChain->setChecked(false); game.chkSTAutoPos->setChecked(true); game.chkMAutoPos->setChecked(true);
  game.chkNoCheckDeck->setChecked(true); game.chkNoShuffleDeck->setChecked(true);
  game.chkAutoSaveReplay->setChecked(false); game.ebTimeLimit->setText(L"0");
  game.ebNickName->setText(host ? L"Match Host" : L"Match Guest");
  run.window = static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd);
  CHECK(run.window); ShowWindow(run.window, SW_HIDE);
  std::ostringstream text; text << "#main\n";
  for (int i = 0; i < 40; ++i) text << (host ? 48305365 : 15025844) << '\n';
  text << "#extra\n!side\n89631139\n";
  std::istringstream input(text.str()); DeckManager::LoadDeckFromStream(run.deck, input);
  CHECK(run.deck.main.size() == 40 && run.deck.side.size() == 1);
  if (host) {
    run.resources = CaptureRoomConfig(dataManager, std::filesystem::current_path().u8string(), false,
                                     free ? RoomMode::LoopbackFree : RoomMode::ConsentLan)->resources;
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(probe != INVALID_SOCKET);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    int length = sizeof(address); CHECK(getsockname(probe, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    const auto port = ntohs(address.sin_port); closesocket(probe); game.gameConf.serverport = port;
    run.menu(game.btnLanMode); run.menu(game.btnCreateHost); game.cbMatchMode->setSelected(1);
    game.chkUndoLoopback->setChecked(free); run.menu(game.btnHostConfirm); CHECK(NetServer::IsRunning());
    std::ofstream(shared / "port") << port;
  } else {
    unsigned short port{};
    for (unsigned i = 0; i < 15000 && (!port || !run.has("host-lobby")); ++i) {
      std::ifstream(shared / "port") >> port; std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(port && run.has("host-lobby"));
    run.menu(game.btnLanMode); game.ebJoinHost->setText(L"127.0.0.1");
    game.ebJoinPort->setText(std::to_wstring(port).c_str()); run.menu(game.btnJoinHost);
  }
  run.factory = new FailingFactory; game.env->registerGUIElementFactory(run.factory); run.factory->drop();
  run.begin = GetTickCount64(); CHECK(SetTimer(run.window, 31, 100, tick));
  std::cout << "ENTER actual Match Game::MainLoop " << run.role() << " 2:" << (three ? 1 : 0)
            << " free=" << free << " normal frame/action/replay/close waits" << std::endl;
  game.MainLoop(); CHECK(run.passed);
  if (host) NetServer::StopServer();
  std::cout << "PASS actual Match Game " << run.role() << " 2:" << (three ? 1 : 0)
            << " native waits, per-game replay, side validation, seat change, second-game undo and old epoch fencing" << std::endl;
  return 0;
}
