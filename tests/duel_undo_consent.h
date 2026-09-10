// Exercise raw mouse events through the actual device and Irrlicht controls.
// A synthetic EGET_BUTTON_CLICKED would skip the input gate that broke clicks.
static void consentMouse(const std::shared_ptr<RoomClient>& room,
                         SessionId session, std::vector<Envelope>& sent) {
 game.device->setEventReceiver(&game.dField);
 game.env->setFocus(nullptr);
 auto count=[&](WireKind kind){return std::count_if(sent.begin(),sent.end(),[&](const Envelope& e){return e.kind==kind;});};
 auto mouse=[&](irr::EMOUSE_INPUT_EVENT type,irr::core::position2di p){
  irr::SEvent e{};e.EventType=irr::EET_MOUSE_INPUT_EVENT;
  e.MouseInput.Event=type;e.MouseInput.X=p.X;e.MouseInput.Y=p.Y;
  e.MouseInput.ButtonStates=type==irr::EMIE_LMOUSE_PRESSED_DOWN?irr::EMBSM_LEFT:0;
  return game.device->postEventFromUser(e);
 };
 auto click=[&](irr::gui::IGUIButton* b){
  CHECK(b->isTrulyVisible()&&b->isEnabled());auto p=b->getAbsolutePosition().getCenter();
  mouse(irr::EMIE_MOUSE_MOVED,p);mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,p);mouse(irr::EMIE_LMOUSE_LEFT_UP,p);
  room->Poll();
 };
 uint64_t request=100;
 auto ask=[&]{
  TxKey k{session,0,++request,0,{}};
  room->Receive({WireKind::Consent,k,{1}});game.UpdateDuelUndoStatus();
  CHECK(room->NeedsConsent()&&room->InputPaused());return k;
 };
 auto cancel=[&](const TxKey& k){room->Receive({WireKind::Abort,k,{0,1}});game.UpdateDuelUndoStatus();CHECK(!room->InputPaused());};
 auto voteCount=count(WireKind::Consent);
 for(bool yes:{true,false}) {
  game.env->setFocus(yes?static_cast<irr::gui::IGUIElement*>(game.btnEP):game.ebChatInput);
  auto* previousFocus=game.env->getFocus();
  auto k=ask();click(yes?game.btnUndoApprove:game.btnUndoDecline);
  CHECK(count(WireKind::Consent)==++voteCount);
  const auto& vote=*std::find_if(sent.rbegin(),sent.rend(),[](const Envelope& e){return e.kind==WireKind::Consent;});
  CHECK(SameKey(vote.key,k)&&vote.payload==Bytes{uint8_t(yes)});
  CHECK(game.env->getFocus()==previousFocus);
  CHECK(!room->NeedsConsent()&&room->InputPaused());cancel(k);
 }
 std::cout<<"PASS native mouse approve and decline while gameplay remains paused\n";
 auto k=ask();auto point=game.btnUndoApprove->getAbsolutePosition().getCenter();
 auto responses=count(WireKind::Response);
 game.wPhase->setVisible(true);game.btnEP->setVisible(true);game.btnEP->setEnabled(true);
 click(game.btnEP);CHECK(count(WireKind::Response)==responses&&room->NeedsConsent());
 // A release without a matching press, or a drag across buttons, is not consent.
 mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,{900,400});mouse(irr::EMIE_LMOUSE_LEFT_UP,point);room->Poll();CHECK(count(WireKind::Consent)==voteCount);
 mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);
 mouse(irr::EMIE_LMOUSE_LEFT_UP,game.btnUndoDecline->getAbsolutePosition().getCenter());room->Poll();CHECK(count(WireKind::Consent)==voteCount);
 // A timed-out request can be replaced before the held mouse is released.
 mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);cancel(k);k=ask();
 mouse(irr::EMIE_LMOUSE_LEFT_UP,point);room->Poll();CHECK(count(WireKind::Consent)==voteCount&&room->NeedsConsent());
 click(game.btnUndoApprove);CHECK(count(WireKind::Consent)==++voteCount);cancel(k);
 // Releasing after cancellation must not activate underlying normal controls.
 k=ask();mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);cancel(k);
 CHECK(mouse(irr::EMIE_LMOUSE_LEFT_UP,game.btnEP->getAbsolutePosition().getCenter()));room->Poll();
 CHECK(count(WireKind::Response)==responses);
 std::cout<<"PASS consent timeout/replacement and cross-button mouse protection\n";
 k=ask();mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);
 auto* cover=game.env->addStaticText(L"covered",game.btnUndoApprove->getAbsolutePosition(),true);
 mouse(irr::EMIE_LMOUSE_LEFT_UP,point);room->Poll();
 CHECK(count(WireKind::Consent)==voteCount&&room->NeedsConsent());
 cover->remove();
 mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);game.btnUndoApprove->setVisible(false);
 mouse(irr::EMIE_LMOUSE_LEFT_UP,point);room->Poll();CHECK(count(WireKind::Consent)==voteCount);
 game.UpdateDuelUndoStatus();
 mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,point);
 irr::SEvent premature{};premature.EventType=irr::EET_GUI_EVENT;
 premature.GUIEvent.Caller=game.btnUndoApprove;premature.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;
 game.device->postEventFromUser(premature);room->Poll();CHECK(count(WireKind::Consent)==voteCount);
 mouse(irr::EMIE_LMOUSE_LEFT_UP,point);room->Poll();CHECK(count(WireKind::Consent)==++voteCount);cancel(k);
 std::cout<<"PASS covered/hidden controls and only complete native mouse gestures vote\n";
 k=ask();
 // Transport can replace A before DrawGUI refreshes A's still-visible text.
 const std::wstring displayed=game.stUndoDuel->getText();
 room->Receive({WireKind::Abort,k,{0,1}});
 k.request=++request;k.targetIndex=7;
 room->Receive({WireKind::Consent,k,{1}});
 CHECK(std::wstring(game.stUndoDuel->getText())==displayed);
 click(game.btnUndoApprove);CHECK(count(WireKind::Consent)==voteCount&&room->NeedsConsent());
 game.UpdateDuelUndoStatus();click(game.btnUndoApprove);
 CHECK(count(WireKind::Consent)==++voteCount);
 CHECK(SameKey(sent.back().key,k));cancel(k);
 std::cout<<"PASS a click on stale displayed consent cannot approve a newer request\n";
 const auto oldX=game.xScale,oldY=game.yScale;
 game.xScale=1.5;game.yScale=1.25;game.OnResize();k=ask();
 const auto undoRect=game.btnUndoDuel->getAbsolutePosition();
 const auto approveRect=game.btnUndoApprove->getAbsolutePosition();
 const auto declineRect=game.btnUndoDecline->getAbsolutePosition();
 CHECK(approveRect.UpperLeftCorner.X>undoRect.LowerRightCorner.X);
 CHECK(declineRect.UpperLeftCorner.X>approveRect.LowerRightCorner.X);
 CHECK(approveRect.getWidth()>75&&approveRect.getHeight()>30);
 click(game.btnUndoDecline);CHECK(count(WireKind::Consent)==++voteCount);cancel(k);
 game.xScale=oldX;game.yScale=oldY;game.OnResize();game.UpdateDuelUndoStatus();
 std::cout<<"PASS resized consent buttons remain separate and clickable\n";
}
