// This fixture goes through the real editor button and UI-frame lifecycle.
// It never constructs a host, sends a ready/start packet, or restores the
// editor itself: those are the product behavior under test.
#include "image_manager.h"
#include <map>
#include <tlhelp32.h>

static void terminateSessionOwnedBot() {
 // Never kill by filename or PID alone. The open process handle pins the
 // identity after both parent and exact fixture executable have been checked.
 using ProcessHandle=std::unique_ptr<void,decltype(&CloseHandle)>;
 ProcessHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0),CloseHandle);
 CHECK(snapshot.get()!=INVALID_HANDLE_VALUE);
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 std::vector<ProcessHandle> owned;
 const auto expected=std::filesystem::current_path()/"WindBot"/"WindBot-undo.exe";
 for(bool found=Process32FirstW(snapshot.get(),&entry)!=FALSE;found;found=Process32NextW(snapshot.get(),&entry)!=FALSE) {
  if(entry.th32ParentProcessID!=GetCurrentProcessId())continue;
  ProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,entry.th32ProcessID),CloseHandle);
  if(!process)continue;
  std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());
  if(!QueryFullProcessImageNameW(process.get(),0,path.data(),&size))continue;
  path.resize(size);
  if(std::filesystem::equivalent(std::filesystem::path(path),expected))owned.push_back(std::move(process));
 }
 CHECK(owned.size()==1);
 const auto pid=GetProcessId(owned.front().get());CHECK(pid!=0);
 CHECK(TerminateProcess(owned.front().get(),86));
 CHECK(WaitForSingleObject(owned.front().get(),5000)==WAIT_OBJECT_0);
 std::cout<<"Injected owned preview bot crash: test PID="<<GetCurrentProcessId()<<" bot PID="<<pid<<std::endl;
}

static int deckTestSessionGame() {
 WSADATA winsock{};CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
 CHECK(evthread_use_windows_threads()==0);
 game.closeDoneSignal.SetNoWait(false);game.actionSignal.SetNoWait(false);
 game.chkAutoSaveReplay->setChecked(false);game.chkWaitChain->setChecked(false);
 game.chkMAutoPos->setChecked(true);game.chkSTAutoPos->setChecked(true);
 ShowWindow(static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd),SW_HIDE);
 game.camera=game.smgr->addCameraSceneNode(0);
 irr::core::matrix4 projection;
 game.BuildProjectionMatrix(projection,-0.90f,0.45f,-0.42f,0.42f,1.0f,100.0f);
 game.camera->setProjectionMatrix(projection);
 projection.buildCameraLookAtMatrixLH(irr::core::vector3df(4.2f,8.0f,7.8f),irr::core::vector3df(4.2f,0,0),irr::core::vector3df(0,0,1));
 game.camera->setViewMatrixAffector(projection);
 game.smgr->setAmbientLight(irr::video::SColorf(1,1,1));
 auto& editor=game.deckBuilder;
 auto click=[&](irr::gui::IGUIElement* widget,bool editing=false) {
  irr::SEvent event{};event.EventType=irr::EET_GUI_EVENT;
  event.GUIEvent.Caller=widget;event.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;
  if(editing)editor.OnEvent(event);else game.dField.OnEvent(event);
 };
 std::function<irr::gui::IGUIElement*(irr::gui::IGUIElement*)> messageBox=[&](auto* node)->irr::gui::IGUIElement* {
  if(node->getType()==irr::gui::EGUIET_MESSAGE_BOX)return node;
  for(auto* child:node->getChildren())if(auto* found=messageBox(child))return found;
  return nullptr;
 };
 const auto acknowledge=[&](irr::gui::IGUIElement* dialog) {
  irr::gui::IGUIElement* ok=nullptr;
  for(auto* child:dialog->getChildren())if(child->getType()==irr::gui::EGUIET_BUTTON &&
    std::wstring(child->getText())==game.env->getSkin()->getDefaultText(irr::gui::EGDT_MSG_BOX_OK))ok=child;
  CHECK(ok);
  irr::SEvent event{};event.EventType=irr::EET_GUI_EVENT;event.GUIEvent.Caller=ok;
  event.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;dialog->OnEvent(event);
 };
 game.wMainMenu->setVisible(false);game.wDeckEdit->setVisible(true);game.is_building=true;
 game.cbDBCategory->clear();for(auto name:{L"pack",L"bot",L"deck"})game.cbDBCategory->addItem(name);
 game.cbDBCategory->setSelected(2);game.cbDBDecks->clear();
 editor.Initialize();game.device->setEventReceiver(&editor);game.env->setFocus(nullptr);
 CHECK(!editor.readonly && !editor.showing_pack && !game.is_siding);
 game.ebDeckname->setText(L"Unsaved session construction");
 auto& cards=dataManager.GetDataTable();
 const auto A=&cards.at(89631139),B=&cards.at(46986414),X=&cards.at(23995346);
 deckManager.current_deck.main={A,B,A,A,B,A,B,A};
 deckManager.current_deck.extra={X};deckManager.current_deck.side={B,A};
 editor.ResetEditorHistory();
 CHECK(deckManager.SaveDeck(deckManager.current_deck,L"./deck/deck-test-session-source.ydk"));
 editor.EditorDeckSaved();const auto saved=editor.CaptureEditorDeck();
 Deck selected;selected.main={B,B,B,B,B,B,B};
 CHECK(deckManager.SaveDeck(selected,L"./deck/deck-test-session-selected.ydk"));
 editor.BeginEditorEdit();std::swap(deckManager.current_deck.main[0],deckManager.current_deck.main[1]);
 deckManager.current_deck.main.push_back(B);deckManager.current_deck.extra.push_back(X);
 editor.FinishEditorEdit(true);CHECK(editor.is_modified && editor.editorHistory.CanUndo());
 game.cbCategorySelect->clear();game.cbCategorySelect->addItem(L"Unrelated lobby category");game.cbCategorySelect->setSelected(0);
 game.cbDeckSelect->clear();game.cbDeckSelect->addItem(L"deck-test-session-selected");game.cbDeckSelect->setSelected(0);
 BufferIO::CopyWideString(L"Saved category",game.gameConf.lastcategory);
 BufferIO::CopyWideString(L"Saved deck",game.gameConf.lastdeck);
 game.cbBotRule->setSelected(2);game.ebNickName->setText(L"Editor tester");
 game.chkBotHand->setChecked(false);game.chkBotNoCheckDeck->setChecked(false);
 game.chkBotNoShuffleDeck->setChecked(false);game.chkBotUndoLoopback->setChecked(false);
 game.gameConf.bot_room_public=1;
 const auto configSnapshot=game.ConfigSnapshot();
 const auto read=[](const std::filesystem::path& path) {
  std::ifstream stream(path,std::ios::binary);CHECK(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),{});
 };
 std::map<std::filesystem::path,std::string> unchanged;
 for(const auto path:{"system.conf","system-undo.conf","bot.conf","deck/deck-test-session-source.ydk","deck/deck-test-session-selected.ydk"})
  unchanged.emplace(path,read(path));
 const auto checkIsolation=[&] {
  for(const auto& file:unchanged)CHECK(read(file.first)==file.second);
  CHECK(game.ConfigSnapshot()==configSnapshot);
  CHECK(std::wstring(game.cbCategorySelect->getText())==L"Unrelated lobby category");
  CHECK(std::wstring(game.cbDeckSelect->getText())==L"deck-test-session-selected");
  CHECK(!game.chkBotHand->isChecked() && !game.chkBotNoCheckDeck->isChecked());
  CHECK(!game.chkBotNoShuffleDeck->isChecked() && !game.chkBotUndoLoopback->isChecked());
  CHECK(game.cbBotRule->getSelected()==2 && game.gameConf.bot_room_public==1);
  CHECK(std::distance(std::filesystem::directory_iterator("deck"),std::filesystem::directory_iterator{})==2);
 };
 const auto screenshot=[&](const wchar_t* name) {
  // Use the native drawing path, including a frame after card motion settles.
  for(int frame=0;frame<30;++frame) {
   game.driver->beginScene(true,true,irr::video::SColor(0,0,0,0));
   if(game.dInfo.isStarted) {
    game.DrawBackImage(imageManager.tBackGround);game.DrawBackGround();
    game.DrawCards();game.DrawMisc();game.smgr->drawAll();
    game.driver->setMaterial(irr::video::IdentityMaterial);game.driver->clearZBuffer();
   } else {game.DrawBackImage(imageManager.tBackGround_deck);game.DrawDeckBd();}
   game.DrawGUI();game.driver->endScene();
  }
  auto* image=game.driver->createScreenShot();CHECK(image);
  CHECK(game.driver->writeImageToFile(image,name));image->drop();
  CHECK(std::filesystem::file_size(name)>0);
 };
 std::uint64_t previousGeneration{};
 unsigned sessionCount{};
 const auto run=[&](const char* label,bool failure=false,bool cancelReplay=false,bool crashBot=false) {
  const bool expectFailure=failure||crashBot;
  ++sessionCount;
  const auto expected=editor.CaptureEditorDeck();
  game.env->setFocus(game.btnUndoDeck);editor.RefreshDeckTestEntry();
  CHECK(editor.GetDeckTestEntryState()==DeckTestEntryState::Ready);
  click(game.btnTestDeck,true);CHECK(editor.HasDeckTestPreparation());
  const auto frozen=editor.DeckTestConfig();CHECK(frozen);
  CHECK(frozen->generation>previousGeneration);previousGeneration=frozen->generation;
  CHECK(frozen->main==expected[0] && frozen->extra==expected[1]);
  CHECK(frozen->host.rule==5 && frozen->host.mode==MODE_SINGLE && frozen->host.duel_rule==5);
  CHECK(frozen->host.start_lp==8000 && frozen->host.start_hand==5 && frozen->host.draw_count==1);
  CHECK(frozen->host.time_limit==0 && frozen->host.no_check_deck && frozen->host.no_shuffle_deck);
  CHECK(frozen->name==L"Editor tester");
  click(game.btnTestDeck,true);CHECK(editor.DeckTestConfig()==frozen);
  bool reachedPrompt=false,surrendered=false,replaySeen=false,failureSeen=false,exitRequested=false,botTerminated=false,faultActionSent=false;
  auto nextDiagnostic=std::chrono::steady_clock::now();
  auto deadline=nextDiagnostic+std::chrono::seconds(60);
  while(std::chrono::steady_clock::now()<deadline) {
   CHECK(game.device->run());editor.PollDeckTest();
   {
    std::lock_guard<std::mutex> lock(game.gMutex);game.DrawGUI();
    CHECK(!game.wHand->isVisible() && !game.wFTSelect->isVisible());
    CHECK(!game.wHostPrepare->isVisible());
    auto room=DuelClient::Room();
    if(std::chrono::steady_clock::now()>=nextDiagnostic) {
     std::cout<<"Deck session "<<label<<" pending="<<editor.HasDeckTestPreparation()
      <<" room="<<bool(room)<<" server="<<NetServer::IsRunning()<<" msg="<<game.dInfo.curMsg
      <<" turn="<<game.dInfo.turn<<" replay="<<game.wReplaySave->isVisible()
      <<" fades="<<game.fadingList.size()<<" paused="<<(room?room->InputPaused():false)
      <<" prompt="<<(room?room->Token().prompt:0)<<std::endl;
     nextDiagnostic=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    }
    if(game.fadingList.empty()) {
     if(auto* dialog=messageBox(game.env->getRootGUIElement())) {CHECK(expectFailure);failureSeen=true;acknowledge(dialog);}
     if(game.wMessage->isVisible()) {failureSeen=true;click(game.btnMsgOK);}
     if(game.wReplaySave->isVisible()) {
      CHECK(surrendered);replaySeen=true;
      if(!cancelReplay)click(game.btnRSNo);
     }
     if(room && !room->InputPaused() && game.dInfo.curMsg==MSG_SELECT_IDLECMD && !reachedPrompt) {
      CHECK(!failure);reachedPrompt=true;
      CHECK(game.dInfo.isFirst && game.dInfo.turn==1 && !game.dInfo.isTag && !game.dInfo.isSingleMode);
      CHECK(game.dInfo.duel_rule==5 && game.dInfo.start_lp==8000 && game.dInfo.time_limit==0);
      CHECK(game.dInfo.lp[0]==8000 && game.dInfo.lp[1]==8000);
      CHECK(game.dField.hand[0].size()==5 && game.dField.deck[0].size()==expected[0].size()-5);
      // Pseudo-shuffle preserves the actual editor top-of-deck order.
      for(size_t i=0;i<5;++i)CHECK(game.dField.hand[0][i]->code==expected[0][i]);
      CHECK(game.dField.extra[0].size()==expected[1].size());
      for(auto card:game.dField.extra[0])CHECK(card->code==23995346);
      CHECK(game.dField.hand[1].size()==5 && !game.dField.deck[1].empty());
      CHECK(std::wstring(game.dInfo.clientname).find(L"悠悠")!=std::wstring::npos);
      if(sessionCount==1)screenshot(L"deck-test-first-turn.png");
      if(!crashBot){click(game.btnLeaveGame);CHECK(game.wSurrender->isVisible());}
     }
     if(reachedPrompt && !surrendered && game.wSurrender->isVisible() && game.fadingList.empty()) {
      click(game.btnSurrenderYes);surrendered=true;
     }
    }
   }
   if(crashBot && reachedPrompt && !botTerminated) {
    terminateSessionOwnedBot();botTerminated=true;
    // The private bot reports pipe failures on its next interaction. Exercise
    // that interaction with the player's real End Phase button, then require
    // product-owned failure recovery without ExitDeckTest/StopClient/StopServer.
    {std::lock_guard<std::mutex> lock(game.gMutex);
     CHECK(game.btnEP->isVisible() && game.btnEP->isEnabled());
     click(game.btnEP);faultActionSent=true;}
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
   }
   if(cancelReplay && replaySeen && !exitRequested){editor.ExitDeckTest();exitRequested=true;}
   if(game.closeSignal.TryWait()){std::lock_guard<std::mutex> lock(game.gMutex);game.CloseDuelWindow();}
   if(!editor.HasEditorSuspension() && !DuelClient::Room() && !NetServer::IsRunning() && game.fadingList.empty())break;
   std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  CHECK(!editor.HasDeckTestPreparation() && !editor.HasEditorSuspension());
  CHECK(!DuelClient::Room() && !NetServer::IsRunning());
  CHECK(crashBot?(reachedPrompt && botTerminated && faultActionSent && !surrendered):failure?!reachedPrompt:(reachedPrompt && surrendered && replaySeen));
  if(cancelReplay)CHECK(exitRequested && !game.wReplaySave->isVisible());
  if(expectFailure)CHECK(failureSeen);
  CHECK(game.is_building && game.wDeckEdit->isVisible() && !game.dInfo.isStarted && !game.dInfo.isInDuel);
  CHECK(game.device->getEventReceiver()==&editor);
  CHECK(!game.wMainMenu->isVisible() && !game.wSinglePlay->isVisible() && !game.wLanWindow->isVisible());
  CHECK(editor.CaptureEditorDeck()==expected && editor.is_modified && editor.editorHistory.CanUndo());
  CHECK(std::wstring(game.ebDeckname->getText())==L"Unsaved session construction");
  CHECK(game.cbDBCategory->getSelected()==2 && game.cbDBDecks->getSelected()==-1);
  if(!expectFailure)CHECK(game.env->getFocus()==game.btnUndoDeck);
  checkIsolation();
  if(sessionCount==1){std::lock_guard<std::mutex> lock(game.gMutex);screenshot(L"deck-test-returned-editor.png");}
  std::cout<<"PASS actual editor session: "<<label<<std::endl;
 };
 run("unsaved ordered deck, real MokeyMokeyKing, fixed human first and surrender return");
 const auto first=editor.CaptureEditorDeck();
 editor.BeginEditorEdit();std::swap(deckManager.current_deck.main[0],deckManager.current_deck.main[2]);
 deckManager.current_deck.main.push_back(A);deckManager.current_deck.side.push_back(B);
 editor.FinishEditorEdit(true);
 run("second click captures latest edits and preserves side/history");
 auto token=std::find_if(cards.begin(),cards.end(),[](const auto& card){return (card.second.type&TYPE_TOKEN)!=0;});
 CHECK(token!=cards.end());
 editor.BeginEditorEdit();deckManager.current_deck.main.push_back(&token->second);editor.FinishEditorEdit(true);
 run("invalid token rejected and exact editor restored",true);
 CHECK(editor.UndoEditorEdit());
 // The runner copies this executable into owned fixture storage. Hiding it
 // exercises a real missing-worker failure without touching installed files.
 const auto bot=std::filesystem::current_path()/"WindBot"/"WindBot-undo.exe";
 const auto hidden=bot.wstring()+L".fixture-hidden";
 CHECK(std::filesystem::is_regular_file(bot));std::filesystem::rename(bot,hidden);
 try {run("missing private bot executable restores editor",true);}
 catch(...) {std::filesystem::rename(hidden,bot);throw;}
 std::filesystem::rename(hidden,bot);
 run("retry after upload and worker failures");
 run("exit while replay-save modal is waiting",false,true);
 run("owned bot crash on next player action restores editor",false,false,true);
 run("retry after in-duel bot crash");
 CHECK(editor.UndoEditorEdit());CHECK(editor.CaptureEditorDeck()==first && editor.is_modified);
 CHECK(editor.UndoEditorEdit());CHECK(editor.CaptureEditorDeck()==saved && !editor.is_modified);
 checkIsolation();
 std::cout<<"PASS actual deck-test session lifecycle, fixed options, repeated starts, failure recovery, undo and file isolation\n";
 return 0;
}
