// Included by the actual initialized Game integration executable. Each role
// owns a separate process, Game, RoomClient, resource capture and GUI
// environment.
static int pairGame(bool host, const std::filesystem::path &shared) {
  WSADATA winsock{};
  CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  evthread_use_windows_threads();
  game.closeDoneSignal.SetNoWait(true);
  game.chkWaitChain->setChecked(false);
  game.chkSTAutoPos->setChecked(true);
  game.chkMAutoPos->setChecked(true);
  game.chkNoCheckDeck->setChecked(true);
  game.chkNoShuffleDeck->setChecked(true);
  game.ebTimeLimit->setText(L"0");
  ShowWindow(
      static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd),
      SW_HIDE);
  auto mark = [&](const std::string &name) {
    std::ofstream(shared / name) << "ok\n";
  };
  auto has = [&](const std::string &name) {
    return std::filesystem::exists(shared / name);
  };
  auto click = [&](irr::gui::IGUIElement *widget) {
    irr::SEvent e{};
    e.EventType = irr::EET_GUI_EVENT;
    e.GUIEvent.Caller = widget;
    e.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
    game.dField.OnEvent(e);
  };
  auto config =
      CaptureRoomConfig(dataManager, std::filesystem::current_path().u8string(),
                        false, RoomMode::ConsentLan);
  unsigned short port{};
  if (host) {
    CHECK(NetServer::StartServer(0, 0x7f000001, &port, false,
                                 &config->capability, config));
    std::ofstream(shared / "port") << port;
  } else {
    for (int i = 0; i < 15000 && !port; ++i) {
      std::ifstream(shared / "port") >> port;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(port);
    for (int i = 0; i < 15000 && !has("host-lobby"); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(has("host-lobby"));
  }
  game.ebNickName->setText(host ? L"Actual Game Host" : L"Actual Game Guest");
  DuelClient::ConfigureRoom(config);
  CHECK(DuelClient::StartClient(0x7f000001, port, host));
  auto *factory = new FailingFactory;
  game.env->registerGUIElementFactory(factory);
  factory->drop();
  bool deckSent = false, started = false, hand = false, first = false,
       requestFailure = false, requestOne = false, requestTwo = false,
       armed = false, failedSeen = false;
  bool declineRequested = false, declineContinued = false,
       guestDeclineContinued = false;
  bool a1 = false, a2 = false, g1 = false, g2 = false, b = false, ended = false;
  uint64_t originalPrompt{}, lastDiscard{};
  irr::gui::IGUIButton *failedWidget{};
  undo::InputToken failedToken;
  int failedLP{}, failedClock{};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(90);
  auto nextDiagnostic = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() < deadline) {
    game.device->run();
    std::shared_ptr<RoomClient> room = DuelClient::Room();
    {
      std::lock_guard<std::mutex> lock(game.gMutex);
      game.DrawGUI();
      if (std::chrono::steady_clock::now() >= nextDiagnostic) {
        std::cout << (host ? "host" : "guest") << " room=" << bool(room)
                  << " msg=" << int(game.dInfo.curMsg)
                  << " lobby=" << game.wHostPrepare->isVisible()
                  << " hand=" << game.wHand->isVisible()
                  << " first=" << game.wFTSelect->isVisible()
                  << " fading=" << game.fadingList.size();
        if (room)
          std::cout << " paused=" << room->InputPaused()
                    << " epoch=" << room->Token().epoch
                    << " prompt=" << room->Token().prompt
                    << " undo=" << room->CanUndo();
        std::cout << std::endl;
        nextDiagnostic =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
      }
      if (host && room && game.wHostPrepare->isVisible())
        mark("host-lobby");
      if (room && game.wHostPrepare->isVisible() && !deckSent) {
        Deck deck;
        std::ostringstream text;
        text << "#main\n";
        for (int i = 0; i < 40; ++i)
          text << (host ? 15025844 : 89631139) << "\n";
        text << "#extra\n!side\n";
        std::istringstream input(text.str());
        DeckManager::LoadDeckFromStream(deck, input);
        CHECK(deck.main.size() == 40);
        DuelClient::SendUpdateDeck(deck);
        DuelClient::SendPacketToServer(CTOS_HS_READY);
        deckSent = true;
      }
      if (host && deckSent && !started &&
          game.chkHostPrepReady[0]->isChecked() &&
          game.chkHostPrepReady[1]->isChecked()) {
        DuelClient::SendPacketToServer(CTOS_HS_START);
        started = true;
      }
      if (game.wHand->isVisible() && !hand && game.fadingList.empty()) {
        click(game.btnHand[host ? 0 : 2]);
        hand = true;
      }
      if (host && game.wFTSelect->isVisible() && !first &&
          game.fadingList.empty()) {
        click(game.btnFirst);
        first = true;
      }
      if (room && room->NeedsConsent()) {
        game.UpdateDuelUndoStatus();
        CHECK(game.btnUndoApprove->isVisible() &&
              game.stUndoDuel->getRelativePosition().getWidth() >= 400);
        if (!host && has("decline-next") && !has("declined")) {
          failedWidget = game.btnEP;
          failedToken = room->Token();
          failedLP = game.dInfo.lp[0];
          failedClock = game.dInfo.time_left[0];
          click(game.btnUndoDecline);
          mark("declined");
        } else
          click(game.btnUndoApprove);
      }
      if(room && !room->InputPaused() && game.fadingList.empty() &&
         game.dInfo.curMsg==MSG_SELECT_CARD && room->Token().prompt!=lastDiscard) {
        Bytes response{uint8_t(game.dField.select_min)};
        for(unsigned i=0;i<game.dField.select_min;++i)response.push_back(uint8_t(game.dField.selectable_cards.at(i)->select_seq));
        DuelClient::SetResponseB(response.data(),response.size());DuelClient::SendResponse();lastDiscard=room->Token().prompt;
      }
      bool idle = room && !room->InputPaused() &&
                  game.dInfo.curMsg == MSG_SELECT_IDLECMD &&
                  game.fadingList.empty();
      if (host && idle && !a1) {
        originalPrompt = room->Token().prompt;
        click(game.btnEP);
        a1 = true;
        mark("first-A");
      }
      if (!host && idle && has("first-A") && !g1) {
        click(game.btnEP);
        g1 = true;
        mark("guest-A");
      }
      if (host && idle && has("guest-A") && !declineRequested &&
          room->CanUndo()) {
        failedWidget = game.btnEP;
        failedToken = room->Token();
        failedLP = game.dInfo.lp[0];
        failedClock = game.dInfo.time_left[0];
        mark("decline-next");
        CHECK(room->RequestUndo());
        declineRequested = true;
      }
      if (room && has("declined") && !room->InputPaused() &&
          !has(host ? "host-decline-preserved" : "guest-decline-preserved")) {
        CHECK(game.btnEP == failedWidget && room->Token() == failedToken &&
              game.dInfo.lp[0] == failedLP &&
              game.dInfo.time_left[0] == failedClock);
        mark(host ? "host-decline-preserved" : "guest-decline-preserved");
      }
      if (host && idle && has("host-decline-preserved") &&
          has("guest-decline-preserved") && !declineContinued) {
        originalPrompt = room->Token().prompt;
        click(game.btnEP);
        declineContinued = true;
        mark("decline-continued");
      }
      if (!host && idle && has("decline-continued") && !guestDeclineContinued) {
        click(game.btnEP);
        guestDeclineContinued = true;
        mark("guest-decline-continued");
      }
      if (!host && has("arm-failure") && !armed) {
        factory->remaining = 1;
        failedWidget = game.btnEP;
        failedToken = room->Token();
        failedLP = game.dInfo.lp[0];
        failedClock = game.dInfo.time_left[0];
        armed = true;
        mark("armed");
      }
      if (host && idle && has("guest-decline-continued") && !requestFailure) {
        mark("arm-failure");
        if (has("armed") && room->CanUndo()) {
          failedWidget = game.btnEP;
          failedToken = room->Token();
          failedLP = game.dInfo.lp[0];
          failedClock = game.dInfo.time_left[0];
          CHECK(room->RequestUndo());
          requestFailure = true;
        }
      }
      if (!host && armed && factory->calls >= 2 && !room->InputPaused() &&
          !failedSeen) {
        CHECK(game.btnEP == failedWidget && room->Token() == failedToken &&
              game.dInfo.lp[0] == failedLP &&
              game.dInfo.time_left[0] == failedClock);
        factory->remaining = -1;
        failedSeen = true;
        mark("guest-failure-preserved");
      }
      if (host && requestFailure && !room->InputPaused() && !requestOne &&
          has("guest-failure-preserved")) {
        CHECK(game.btnEP == failedWidget && room->Token() == failedToken &&
              game.dInfo.lp[0] == failedLP &&
              game.dInfo.time_left[0] == failedClock);
        if (room->CanUndo()) {
          CHECK(room->RequestUndo());
          requestOne = true;
        }
      }
      if (room && !room->InputPaused() && room->Token().epoch == 1) {
        CHECK(room->Token().prompt >= originalPrompt);
        mark(host ? "host-epoch1" : "guest-epoch1");
        if (host && idle && !a2 && has("guest-epoch1")) {
          CHECK(room->Token().prompt == originalPrompt);
          CHECK(game.btnEP != failedWidget);
          click(game.btnEP);
          a2 = true;
          mark("second-A");
        }
        if (!host && idle && !g2 && has("second-A")) {
          click(game.btnEP);
          g2 = true;
          mark("guest-second-A");
        }
      }
      if (host && idle && a2 && has("guest-second-A") && !requestTwo &&
          room->CanUndo()) {
        CHECK(room->RequestUndo());
        requestTwo = true;
      }
      if (room && !room->InputPaused() && room->Token().epoch == 2) {
        mark(host ? "host-epoch2" : "guest-epoch2");
        if (host && idle && !b && has("guest-epoch2")) {
          CHECK(room->Token().prompt == originalPrompt);
          DuelClient::SetResponseI(0);
          DuelClient::SendResponse();
          b = true;
        }
        if (host && idle && b && !ended &&
            std::any_of(game.dField.mzone[0].begin(),
                        game.dField.mzone[0].end(),
                        [](auto *c) { return c && c->code == 15025844; })) {
          click(game.btnEP);
          ended = true;
          mark("branch-B");
        }
        if (!host && idle && has("branch-B")) {
          CHECK(std::any_of(game.dField.mzone[1].begin(),
                            game.dField.mzone[1].end(),
                            [](auto *c) { return c && c->code == 15025844; }));
          mark("guest-done");
        }
      }
    }
    if (host && has("guest-done")) {
      mark("host-done");
      break;
    }
    if (!host && has("host-done"))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(has("host-done") && has("guest-done"));
  std::cerr << (host ? "host" : "guest") << " stopping client" << std::endl;
  DuelClient::StopClient();
  if (host)
    NetServer::StopServer();
  for (int i = 0;
       i < 3000 && (DuelClient::Room() || (host && NetServer::IsRunning()));
       ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  std::cerr << (host ? "host" : "guest") << " stopped room" << std::endl;
  CHECK(!DuelClient::Room());
  if (host)
    CHECK(!NetServer::IsRunning());
  std::cout << "PASS paired actual Game " << (host ? "host" : "guest")
            << " LAN decline/continued input, failure rollback, two epochs, "
               "B-only continued field\n";
  return 0;
}
