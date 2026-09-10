// Compile the actual transport implementation in this test TU to publish a
// controlled RoomClient through its existing internal slot, without a new
// production hook or a fake input/GUI implementation.
#include "duelclient.cpp"
#include "test_support.h"
#include <iostream>
using namespace ygo;
using namespace undo;
static Game game;
static void put(Bytes& b,uint64_t v,unsigned n){while(n--){b.push_back(uint8_t(v));v>>=8;}}
static bool key(bool down,bool control=true,bool shift=false,bool repeat=false) {
 irr::SEvent event{};event.EventType=irr::EET_KEY_INPUT_EVENT;
 event.KeyInput.Key=irr::KEY_KEY_Z;event.KeyInput.PressedDown=down;
 event.KeyInput.Control=control;event.KeyInput.Shift=shift;event.KeyInput.AutoRepeat=repeat;
 return game.dField.OnEvent(event);
}
static void tap(){key(true);key(false);}
static void pump(){game.device->run();{std::lock_guard<std::mutex> lock(game.gMutex);game.DrawGUI();}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
template<class F> static void until(F ready){for(unsigned i=0;i<5000;++i){pump();if(ready())return;}throw std::runtime_error("Single shortcut timeout");}
static bool idle(const std::shared_ptr<SingleUndo>& s,uint64_t after=0){return s->Token().prompt>after&&!s->HasPendingResponse()&&!SingleMode::InputPaused()&&game.dInfo.curMsg==MSG_SELECT_IDLECMD&&game.btnEP->isVisible()&&game.fadingList.empty();}
static int activation(const Bytes& b,uint32_t card){size_t at=2;for(int i=0;i<5;++i){auto n=b.at(at++);at+=7*n;}auto n=b.at(at++);for(unsigned i=0;i<n;++i,at+=11){uint32_t code=0;for(int j=3;j>=0;--j)code=(code<<8)|b.at(at+j);if(code==card)return (i<<16)|5;}throw std::runtime_error("Actual card activation absent");}
static void singleShortcut(){
 game.dInfo.isFinished=false;game.is_building=false;game.env->setFocus(nullptr);
 game.replaySignal.SetNoWait(true);game.chkSTAutoPos->setChecked(true);
 game.open_file=true;BufferIO::CopyWideString(L"c4-single.lua",game.open_file_name);
 std::thread worker(SingleMode::SinglePlayThread);
 try{
  until([]{return bool(SingleMode::ActiveSession());});auto s=SingleMode::ActiveSession();until([&]{return idle(s);});std::cerr<<"single initial idle\n";
  CHECK(!SingleMode::CanUndo(0));tap();CHECK(s->Token().epoch==0);
  auto submitA=[&]{auto before=s->Token().prompt;DuelClient::SetResponseI(activation(s->Current().checkpoint.prompt,70368879));DuelClient::SendResponse();until([&]{return idle(s,before);});CHECK(SingleMode::CanUndo(0));};
  submitA();std::cerr<<"single A accepted\n";CHECK(game.dInfo.lp[1]==9000);CHECK(key(true));
  until([&]{return s->Token().epoch==1&&idle(s);});std::cerr<<"single first undo\n";CHECK(game.dInfo.lp[1]==8000);CHECK(s->History().empty());
  // The same held physical key must not request again on a new valid branch.
  submitA();key(true,true,false,true);for(int i=0;i<30;++i)pump();CHECK(s->Token().epoch==1);
  key(false);CHECK(key(true));key(false);until([&]{return s->Token().epoch==2&&idle(s);});
  CHECK(s->History().empty());CHECK(game.dInfo.lp[1]==8000);
  SingleMode::StopPlay(true);worker.join();
 }catch(const std::exception& e){std::cerr<<"single failure "<<e.what()<<" msg="<<int(game.dInfo.curMsg)<<" error="<<SingleMode::LastUndoError()<<std::endl;SingleMode::StopPlay(true);worker.join();throw;}
 std::cout<<"Actual SingleMode card effect -> Ctrl+Z twice; held key does not repeat PASS\n";
}
static void editorShortcut(){
 game.is_building=true;game.dInfo.isSingleMode=false;game.dInfo.isStarted=false;game.dInfo.isFinished=false;game.env->setFocus(nullptr);
 auto& editor=game.deckBuilder;editor.readonly=false;
 std::array<std::vector<unsigned int>,3> before{{{70368879},{},{}}};
 CHECK(editor.RestoreEditorDeck(before));editor.ResetEditorHistory();
 auto after=before;after[0].push_back(37812118);CHECK(editor.RestoreEditorDeck(after));editor.editorHistory.Record(before,after);
 irr::SEvent e{};e.EventType=irr::EET_KEY_INPUT_EVENT;e.KeyInput.Key=irr::KEY_KEY_Z;e.KeyInput.Control=true;e.KeyInput.PressedDown=true;
 CHECK(editor.OnEvent(e));CHECK(editor.CaptureEditorDeck()==before);
 std::cout<<"Existing editor Ctrl+Z unchanged PASS\n";
}
int main(){
 try {
  mainGame=&game;CHECK(game.Initialize(std::filesystem::current_path()));
  game.frameSignal.SetNoWait(true);game.actionSignal.SetNoWait(true);
  game.wMainMenu->setVisible(false);game.dInfo.isStarted=true;
  game.dInfo.isInDuel=true;game.dInfo.isSingleMode=false;game.dInfo.isReplay=false;
  auto session=NewSessionId();std::vector<Envelope> sent;
  auto room=std::make_shared<RoomClient>(game,session,[&](const Envelope& e){sent.push_back(e);},
   [](const Bytes& b){DuelClient::HandleLegacySTOC(const_cast<uint8_t*>(b.data()),b.size());},
   [](const InputSubmission&){});
  std::atomic_store(&ygo::roomClient,room);
  uint64_t sequence=0;
  auto frame=[&](Bytes b){b.insert(b.begin(),STOC_GAME_MSG);for(const auto& e:EncodeGamePacket(session,0,1,++sequence,b))room->Receive(e);};
  Bytes start{MSG_START,0,4};put(start,8000,4);put(start,8000,4);for(int i=0;i<4;++i)put(start,0,2);
  frame(start);frame({MSG_SELECT_IDLECMD,0,0,0,0,0,0,0,0,1,0});
  RoomStatus status;status.prompt=1;status.promptPlayer=0;status.eligibleMask=1;
  auto boundary=[&]{room->Receive({WireKind::Status,{session,0,0,0,{}},EncodeRoomStatus(status)});};
  boundary();game.fadingList.clear();game.env->setFocus(nullptr);
  game.UpdateDuelUndoStatus();CHECK(room->CanUndo());
  auto requests=[&]{return std::count_if(sent.begin(),sent.end(),[](const Envelope& e){return e.kind==WireKind::Request;});};
  auto abort=[&]{auto request=*std::find_if(sent.rbegin(),sent.rend(),[](const Envelope& e){return e.kind==WireKind::Request;});room->Receive({WireKind::Abort,request.key,{0,1}});CHECK(room->CanUndo());};
  auto unchanged=[&]{auto n=requests();tap();room->Poll();CHECK(requests()==n);CHECK(room->CanUndo());};
  game.env->setFocus(game.ebChatInput);CHECK(!key(true));key(false);room->Poll();CHECK(requests()==0);
  game.env->setFocus(nullptr);
  for(auto* window:{game.wMessage,game.wSurrender,game.wReplaySave}) {
   window->setVisible(true);unchanged();window->setVisible(false);
  }
  game.dInfo.isReplay=true;unchanged();game.dInfo.isReplay=false;
  game.dInfo.isFinished=true;unchanged();game.dInfo.isFinished=false;
  game.dInfo.isStarted=false;unchanged();game.dInfo.isStarted=true;
  key(true,false);key(false,false);room->Poll();CHECK(requests()==0);
  key(true,true,true);key(false,true,true);room->Poll();CHECK(requests()==0);
  status.eligibleMask=0;boundary();tap();room->Poll();CHECK(requests()==0);
  status.eligibleMask=1;boundary();CHECK(room->CanUndo());
  status.state=TxState::Preparing;boundary();game.env->installPreparedFocus(game.ebChatInput);
  CHECK(key(true));key(false);room->Poll();CHECK(requests()==0);
  game.env->installPreparedFocus(nullptr);status.state=TxState::Running;boundary();
  // A normal card YES/NO prompt has the same undo shortcut as the button.
  game.wQuery->setVisible(true);
  std::cout<<"eligible Room Ctrl+Z should queue the same Request as its button\n"<<std::flush;
  CHECK(key(true));room->Poll();CHECK(requests()==1);CHECK(room->InputPaused());
  // Stay physically held across a completed abort: a repeat must not request again.
  abort();game.wQuery->setVisible(false);CHECK(room->CanUndo());key(true,true,false,true);room->Poll();CHECK(requests()==1);
  key(false);key(true);room->Poll();CHECK(requests()==2);abort();
  // Lost key-up (focus left this receiver): a fresh native down is not repeat.
  CHECK(key(true));room->Poll();CHECK(requests()==3);key(false);abort();
  // The original mouse/menu entry is unchanged.
  irr::SEvent click{};click.EventType=irr::EET_GUI_EVENT;click.GUIEvent.Caller=game.btnUndoDuel;click.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;
  game.dField.OnEvent(click);room->Poll();CHECK(requests()==4);abort();
  // A focused native modal dialog must retain keyboard ownership.
  auto* dialog=game.env->addMessageBox(L"Test",L"Modal");
  game.env->setFocus(dialog);unchanged();dialog->remove();game.env->setFocus(nullptr);
  room->Close();std::atomic_store(&ygo::roomClient,std::shared_ptr<RoomClient>{});
  std::cout<<"Room shortcut gating, held-key and original button PASS\n";
  singleShortcut();editorShortcut();game.device->closeDevice();
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
