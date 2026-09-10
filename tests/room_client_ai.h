// Actual menu -> private AI host -> network participant -> replay save ->
// editor.
static int aiGame() {
  WSADATA winsock{};
  CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  evthread_use_windows_threads();
  CHECK(game.chkBotHand && game.chkBotNoCheckDeck && game.chkBotNoShuffleDeck && game.chkBotUndoLoopback);
  game.closeDoneSignal.SetNoWait(false);
  game.actionSignal.SetNoWait(false);
  game.chkWaitChain->setChecked(false);
  game.chkMAutoPos->setChecked(true);
  game.chkSTAutoPos->setChecked(true);
  game.chkAutoSaveReplay->setChecked(false);
  game.chkBotHand->setChecked(true);
  game.chkBotNoCheckDeck->setChecked(true);
  game.chkBotNoShuffleDeck->setChecked(true);
  game.chkBotUndoLoopback->setChecked(true);
  game.cbBotRule->setSelected(2);
  game.gameConf.bot_room_public = 0;
  ShowWindow(
      static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd),
      SW_HIDE);
  irr::SEvent open{};open.EventType=irr::EET_GUI_EVENT;
  open.GUIEvent.Caller=game.btnSingleMode;open.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;
  game.menuHandler.OnEvent(open);
  int selection = -1;
  for (size_t i = 0; i < game.botInfo.size(); ++i)
    if (std::wstring(game.botInfo[i].command).find(L"Deck=ChainBurn") !=
        std::wstring::npos) {
      selection = int(i);
      break;
    }
  CHECK(selection >= 0);
  game.lstBotList->setSelected(selection);
  std::ostringstream publicDeck;
  publicDeck << "#main\n";
  for (int i = 0; i < 40; ++i)
    publicDeck << "15025844\n";
  publicDeck << "#extra\n!side\n";
  {
    std::ofstream file("deck/n2-ai-human.ydk");
    file << publicDeck.str();
  }
  std::istringstream input(publicDeck.str());
  Deck human;
  DeckManager::LoadDeckFromStream(human, input);
  CHECK(human.main.size() == 40);
  auto click = [&](irr::gui::IGUIElement *widget, bool menu = false) {
    irr::SEvent event{};
    event.EventType = irr::EET_GUI_EVENT;
    event.GUIEvent.Caller = widget;
    event.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
    if (menu)
      game.menuHandler.OnEvent(event);
    else
      game.dField.OnEvent(event);
  };
  // This is the real menu handler: selection, checkbox overrides, frozen
  // resource/config capture, private worker path, listener and client launch.
  click(game.btnStartBot, true);
  CHECK(NetServer::IsRunning());
  bool deckSent = false, started = false, a = false, requested = false,
       b = false, bSeen = false, saved = false;
  uint64_t firstPrompt{}, manualTurn{}, lastResponsePrompt{};
  unsigned humanTurns{};
  uint8_t humanPlayer{};
  auto nextDiagnostic = std::chrono::steady_clock::now();
  const auto deadline = nextDiagnostic + std::chrono::seconds(150);
  while (std::chrono::steady_clock::now() < deadline) {
    game.device->run();
    auto room = DuelClient::Room();
    {
      std::lock_guard<std::mutex> lock(game.gMutex);
      game.DrawGUI();
      if (std::chrono::steady_clock::now() >= nextDiagnostic) {
        std::cout << "AI menu room=" << bool(room)
                  << " msg=" << int(game.dInfo.curMsg)
                  << " turn=" << game.dInfo.turn
                  << " finished=" << game.dInfo.isFinished;
        if (room)
          std::cout << " epoch=" << room->Token().epoch
                    << " prompt=" << room->Token().prompt
                    << " paused=" << room->InputPaused()
                    << " undo=" << room->CanUndo();
        std::cout << std::endl;
        nextDiagnostic =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
      }
      if (room && game.wHostPrepare->isVisible() && !deckSent) {
        DuelClient::SendUpdateDeck(human);
        DuelClient::SendPacketToServer(CTOS_HS_READY);
        deckSent = true;
      }
      if (deckSent && !started && game.chkHostPrepReady[0]->isChecked() &&
          game.chkHostPrepReady[1]->isChecked()) {
        DuelClient::SendPacketToServer(CTOS_HS_START);
        started = true;
      }
      if (game.fadingList.empty()) {
        if (game.wHand->isVisible())
          click(game.btnHand[1]); // Checked Hand freezes the actual bot choice to rock.
        if (game.wFTSelect->isVisible())
          click(game.btnFirst);
        if (game.wMessage->isVisible())
          click(game.btnMsgOK);
        if (game.wReplaySave->isVisible() && !saved) {
          CHECK(game.dInfo.isFinished && requested && bSeen);
          game.ebRSName->setText(L"n2-ai-branch");
          click(game.btnRSYes);
          saved = true;
        }
      }
      if (room && room->Token().epoch == 1 && !b)
        lastResponsePrompt = 0;
      if (room && !room->InputPaused() && game.fadingList.empty() &&
          room->Token().prompt != lastResponsePrompt) {
        const auto token = room->Token();
        if (game.dInfo.curMsg == MSG_SELECT_IDLECMD) {
          if (!a) {
            humanPlayer = game.dInfo.isFirst ? 0 : 1;
            firstPrompt = token.prompt;
            manualTurn = game.dInfo.turn;
            ++humanTurns;
            click(game.btnEP);
            lastResponsePrompt = token.prompt;
            a = true;
          } else if (!requested && game.dInfo.turn > manualTurn &&
                     room->CanUndo()) {
            CHECK(room->RequestUndo());
            requested = true;
          } else if (token.epoch == 1 && !b) {
            CHECK(token.prompt == firstPrompt);
            DuelClient::SetResponseI(0);
            DuelClient::SendResponse();
            lastResponsePrompt = token.prompt;
            b = true;
          } else if (b) {
            bSeen = bSeen ||
                    std::any_of(game.dField.mzone[0].begin(),
                                game.dField.mzone[0].end(), [](auto *card) {
                                  return card && card->code == 15025844;
                                });
            click(game.btnEP);
            lastResponsePrompt = token.prompt;
            ++humanTurns;
          }
        } else if (game.dInfo.curMsg == MSG_SELECT_CARD &&
                   !game.dField.selectable_cards.empty()) {
          Bytes response{uint8_t(game.dField.select_min)};
          for (unsigned i = 0; i < game.dField.select_min; ++i)
            response.push_back(
                uint8_t(game.dField.selectable_cards.at(i)->select_seq));
          DuelClient::SetResponseB(response.data(), response.size());
          DuelClient::SendResponse();
          lastResponsePrompt = token.prompt;
        } else if (game.dInfo.curMsg == MSG_SELECT_CHAIN &&
                   !game.dField.chain_forced) {
          DuelClient::SetResponseI(-1);
          DuelClient::SendResponse();
          lastResponsePrompt = token.prompt;
        }
      }
    }
    if(game.closeSignal.TryWait()){std::lock_guard<std::mutex> lock(game.gMutex);game.CloseDuelWindow();}
    if (saved && std::filesystem::exists("replay/n2-ai-branch.yrp") && !DuelClient::Room())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(saved && bSeen && humanTurns >= 2);
  Replay replay;
  CHECK(replay.OpenReplay(L"n2-ai-branch.yrp"));
  CHECK((replay.pheader.base.flag & REPLAY_UNDO_CORE) &&
        !(replay.pheader.base.flag & REPLAY_SINGLE_MODE));
  CHECK(replay.UndoInitial().noCheckDeck && replay.UndoInitial().noShuffleDeck);
  // The replay begins at the retained pre-A choice and keeps B's summon.
  auto resources = replay.CaptureUndoResources(
      std::filesystem::current_path().u8string(), dataManager, false);
  auto core = replay.CreateUndoDriver(resources);
  auto boundary = core->Advance();
  Bytes response;
  bool firstManual = false;
  unsigned replayInputs = 0;
  while (boundary.kind == BoundaryKind::AwaitResponse) {
    CHECK(replay.ReadUndoResponse(boundary.checkpoint, response));
    if (boundary.checkpoint.player == humanPlayer &&
        boundary.checkpoint.prompt.size() &&
        boundary.checkpoint.prompt[0] == MSG_SELECT_IDLECMD && !firstManual) {
      CHECK(response == Bytes({0, 0, 0, 0}));
      firstManual = true;
    }
    core->Submit(response);
    boundary = core->Advance();
    CHECK(++replayInputs < 1000);
  }
  CHECK(firstManual && boundary.kind == BoundaryKind::Finished);
  DuelClient::StopClient();
  NetServer::StopServer();
  for (int i = 0; i < 5000 && (DuelClient::Room() || NetServer::IsRunning());
       ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(!DuelClient::Room() && !NetServer::IsRunning());
  for(int i=0;i<60;++i){
    game.device->run();
    std::lock_guard<std::mutex> lock(game.gMutex);game.DrawGUI();
  }
  {
    std::lock_guard<std::mutex> lock(game.gMutex);
    CHECK(!game.dInfo.isStarted && !game.dInfo.isInDuel);
    click(game.btnBotCancel,true);
    click(game.btnDeckEdit, true);
    CHECK(game.is_building && !game.deckBuilder.editorHistory.CanUndo());
    auto before = game.deckBuilder.CaptureEditorDeck();
    CHECK(!deckManager.current_deck.main.empty());
    game.deckBuilder.BeginEditorEdit();
    deckManager.current_deck.main.pop_back();
    game.deckBuilder.FinishEditorEdit(true);
    game.env->setFocus(game.btnUndoDeck);
    auto editorState=game.deckBuilder.EditorUndoState();
    std::cout << "editor gate history=" << editorState.history << " text=" << editorState.textFocus << " readonly=" << editorState.readOnly << " modal=" << editorState.modal << " query=" << game.wQuery->isVisible() << " message=" << game.wMessage->isVisible() << " big=" << game.wBigCard->isVisible() << " categories=" << game.wCategories->isVisible() << " manage=" << game.wDeckManage->isVisible() << " dm=" << game.wDMQuery->isVisible() << " marks=" << game.wLinkMarks->isVisible() << std::endl;
    CHECK(game.deckBuilder.UndoEditorEdit());
    CHECK(game.deckBuilder.CaptureEditorDeck() == before);
  }
  std::cout
      << "PASS actual AI menu/checkboxes/private host, human+bot turn, undo B, "
         "normal finish, network save/replay and editor isolation\n";
  return 0;
}
