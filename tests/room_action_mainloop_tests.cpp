#include "game.h"
#include "client_card.h"
#include "duelclient.h"
#include "netserver.h"
#include "deck_manager.h"
#include "room_client.h"
#include "test_support.h"
#include <event2/thread.h>
#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace ygo;
static Game game;
static HWND window;
static Deck human;
static bool deckSent{},started{},handPosted{},handHidden{},resultSeen{},firstPosted{},passed{};
static unsigned ticks{},handRounds{},resultAnimations{};
static unsigned actionStage{}; static uint64_t initialEpoch{}; static ULONGLONG menuAt{};
static std::string scenario="first";
static bool resultShowing{};



static ULONGLONG begin{},handAt{},resultAt{};
static void fail(const std::string& why){std::cerr<<"FAIL real MainLoop: "<<why<<std::endl;ExitProcess(2);}
static void mouse(irr::gui::IGUIElement* widget,const char* label){
    if(!widget->isVisible() || !widget->isEnabled())fail(std::string("button unavailable: ")+label);
    const auto point=widget->getAbsolutePosition().getCenter();
    DWORD owner{};GetWindowThreadProcessId(window,&owner);if(owner!=GetCurrentProcessId())fail("window ownership mismatch");
    const auto position=MAKELPARAM(point.X,point.Y);
    CHECK(PostMessageW(window,WM_MOUSEMOVE,0,position));
    CHECK(PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,position));
    CHECK(PostMessageW(window,WM_LBUTTONUP,0,position));
    std::cout<<"POST real Win32 mouse "<<label<<" x="<<point.X<<" y="<<point.Y<<std::endl;
}
static bool containsWidget(irr::gui::IGUIElement* root, const irr::gui::IGUIElement* target) {
    if(root==target)return true; for(auto* child:root->getChildren())if(containsWidget(child,target))return true; return false;
}
static void CALLBACK tick(HWND,UINT,UINT_PTR,DWORD){
    try {
        ++ticks;
        std::unique_lock<std::mutex> lock(game.gMutex,std::try_to_lock);
        if(!lock.owns_lock()){if(ticks%10==0)std::cout<<"TICK mutex busy "<<ticks<<std::endl;return;}
        const auto now=GetTickCount64();auto room=DuelClient::Room();
        if(ticks%10==0){wchar_t caption[256]{};GetWindowTextW(window,caption,256);
            std::cout<<"TICK "<<ticks<<" hand="<<game.wHand->isVisible()<<" first="<<game.wFTSelect->isVisible()<<" fade="<<game.fadingList.size()<<" signalFrame="<<game.signalFrame<<" showcard="<<game.showcard<<" msg="<<int(game.dInfo.curMsg);
            if(room)std::cout<<" paused="<<room->InputPaused()<<" prompt="<<room->Token().prompt;
            std::wcout<<L" caption="<<caption<<std::endl;
        }
        if(room && game.wHostPrepare->isVisible() && !deckSent){DuelClient::SendUpdateDeck(human);DuelClient::SendPacketToServer(CTOS_HS_READY);deckSent=true;std::cout<<"READY actual human deck"<<std::endl;}
        if(deckSent && !started && game.chkHostPrepReady[0]->isChecked() && game.chkHostPrepReady[1]->isChecked()){DuelClient::SendPacketToServer(CTOS_HS_START);started=true;std::cout<<"START actual host"<<std::endl;}
        if(handPosted && handHidden && game.wHand->isVisible() && game.showcard!=100){handPosted=false;handHidden=false;std::cout<<"RPS actual tie, another hand requested"<<std::endl;}
        if(game.wHand->isVisible() && game.fadingList.empty() && !handPosted){
            std::cout<<"RPS visible paused="<<(room&&room->InputPaused())<<" prompt="<<(room?room->Token().prompt:0)<<std::endl;
            const unsigned choice=(scenario=="tie" && handRounds==0)?0:1;
            mouse(game.btnHand[choice],choice==0?"rock (force real tie)":"paper");++handRounds;handPosted=true;handAt=now;
        }
        if(handPosted && !handHidden && !game.wHand->isVisible()){handHidden=true;std::cout<<"RPS mouse processed afterMs="<<now-handAt<<std::endl;}
        if(game.showcard!=100)resultShowing=false;
        if(game.showcard==100 && game.signalFrame>0 && !resultShowing){resultShowing=true;resultSeen=true;++resultAnimations;resultAt=now;std::cout<<"RPS result real animation frameRemaining="<<game.signalFrame<<" code="<<game.showcardcode<<" round="<<resultAnimations<<std::endl;}
        if(game.wFTSelect->isVisible() && game.fadingList.empty() && !firstPosted){
            std::cout<<"TURN order afterResultMs="<<now-resultAt<<" signalFrame="<<game.signalFrame<<std::endl;
            mouse(scenario=="second"?game.btnSecond:game.btnFirst,scenario=="second"?"second":"first");firstPosted=true;
        }
        if(room && !room->InputPaused() && room->Token().prompt && game.dInfo.curMsg==MSG_SELECT_IDLECMD && game.fadingList.empty()) {
            auto handClick=[&] {
                CHECK(!game.dField.hand[0].empty());
                const int count=int(game.dField.hand[0].size());
                const int x=430+(count<7?76*(6-count)/2:0)+25;
                const auto position=MAKELPARAM(x,550);
                CHECK(PostMessageW(window,WM_MOUSEMOVE,0,position));
                CHECK(PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,position));
                CHECK(PostMessageW(window,WM_LBUTTONUP,0,position));
                std::cout<<"POST actual hand mouse actionStage="<<actionStage<<" x="<<x<<" epoch="<<room->Token().epoch<<std::endl;
            };
            if(actionStage==0){ initialEpoch=room->Token().epoch;handClick();actionStage=1; }
            else if(actionStage==1 && game.wCmdMenu->isVisible()){
                CHECK(game.btnSummon->isVisible());mouse(game.btnSummon,"summon");actionStage=2;
            } else if(actionStage==2 && game.dField.hand[0].size()==4 && room->CanUndo()){
                bool summoned=false;for(auto* c:game.dField.mzone[0])if(c&&c->code==48305365)summoned=true;
                CHECK(summoned);std::cout<<"A real summon complete; requesting real undo"<<std::endl;
                mouse(game.btnUndoDuel,"undo");actionStage=3;
            } else if(actionStage==3 && room->Token().epoch>initialEpoch){
                bool owned=false;for(auto* child:game.wCmdMenu->getChildren())if(child==game.btnOperation)owned=true;
                std::cout<<"POST-RESUME btnOperation owned-by-current-menu="<<owned<<" pointer="<<game.btnOperation<<" menu="<<game.wCmdMenu<<" stCardListTip owned-by-current-tree="<<containsWidget(game.env->getRootGUIElement(),game.stCardListTip)<<" tip="<<game.stCardListTip<<std::endl;
                handClick();actionStage=4;menuAt=now;
            } else if(actionStage==4 && game.wCmdMenu->isVisible() && now-menuAt>1500){
                bool owned=false;for(auto* child:game.wCmdMenu->getChildren())if(child==game.btnOperation)owned=true;
                const bool tipOwned=containsWidget(game.env->getRootGUIElement(),game.stCardListTip);std::cout<<"POST-MENU ownership operation="<<owned<<" tip="<<tipOwned<<std::endl;CHECK(owned&&tipOwned);
                game.dField.HideMenu();
                // Only list setup is programmatic; hover and closing use Win32 input.
                game.dField.display_cards=game.dField.hand[0];
                game.dField.ShowLocationCard();actionStage=5;
            } else if(actionStage==5 && game.wCardDisplay->isVisible()){
                const auto p=game.btnCardDisplay[0]->getAbsolutePosition().getCenter();
                CHECK(PostMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(p.X,p.Y)));
                std::cout<<"POST native display-list hover after undo"<<std::endl;actionStage=6;
            } else if(actionStage==6){
                CHECK(game.stCardListTip->isTrulyVisible() && game.stCardListTip->getParent()==game.wCardDisplay);
                CHECK(std::wstring(game.stCardListTip->getText()).size()>0);
                std::cout<<"PASS native hover tooltip on restored display list"<<std::endl;
                mouse(game.btnDisplayOK,"close display list");actionStage=7;
            } else if(actionStage==7 && !game.wCardDisplay->isVisible()){
                handClick();actionStage=8;
            } else if(actionStage==8 && game.wCmdMenu->isVisible()){
                mouse(game.btnSummon,"second summon");actionStage=9;
            } else if(actionStage==9 && game.dField.hand[0].size()==4 && room->CanUndo()){
                irr::SEvent key{};key.EventType=irr::EET_KEY_INPUT_EVENT;
                key.KeyInput.Key=irr::KEY_KEY_Z;key.KeyInput.Control=true;key.KeyInput.PressedDown=true;
                CHECK(game.device->postEventFromUser(key));
                key.KeyInput.PressedDown=false;game.device->postEventFromUser(key);
                std::cout<<"POST normalized native Ctrl+Z through device receiver"<<std::endl;actionStage=10;
            } else if(actionStage==10 && room->Token().epoch==initialEpoch+2){
                CHECK(containsWidget(game.wCmdMenu,game.btnOperation));
                CHECK(containsWidget(game.env->getRootGUIElement(),game.stCardListTip));
                CHECK(game.dField.hand[0].size()==5);
                game.dField.selectable_cards=game.dField.hand[0];
                game.dField.ShowSelectCard(true);actionStage=11;
            } else if(actionStage==11 && game.wCardSelect->isVisible()){
                const auto p=game.btnCardSelect[0]->getAbsolutePosition().getCenter();
                CHECK(PostMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(p.X,p.Y)));
                std::cout<<"POST native select-list hover after second undo"<<std::endl;actionStage=12;
            } else if(actionStage==12){
                CHECK(game.stCardListTip->isTrulyVisible() && game.stCardListTip->getParent()==game.wCardSelect);
                CHECK(std::wstring(game.stCardListTip->getText()).size()>0);
                mouse(game.btnSelectOK,"close select list");actionStage=13;
            } else if(actionStage==13 && !game.wCardSelect->isVisible()){
                passed=true;std::cout<<"PASS actual two summons/two undos, button and Ctrl+Z, both native list hovers and normal MainLoop closure"<<std::endl;
                KillTimer(window,1);PostMessageW(window,WM_CLOSE,0,0);
            }
        }
        if(handPosted && !handHidden && now-handAt>6000)fail("RPS remains visible after real mouse; live frame timer continues");
        if(now-begin>45000)fail("initial human idle prompt deadline");
    } catch(const std::exception& error){fail(error.what());}
}
int main(int argc,char** argv){try{
    if(argc==2)scenario=argv[1];CHECK(scenario=="first"||scenario=="tie"||scenario=="second"||scenario=="unchecked");
    WSADATA ws{};CHECK(WSAStartup(MAKEWORD(2,2),&ws)==0);CHECK(evthread_use_windows_threads()==0);
    mainGame=&game;CHECK(game.Initialize(std::filesystem::current_path()));
    // All frame/action/close signals retain their production defaults.
    game.gameConf.quick_animation=0;
    game.chkWaitChain->setChecked(false);game.chkMAutoPos->setChecked(true);game.chkSTAutoPos->setChecked(true);game.chkAutoSaveReplay->setChecked(false);
    game.chkBotHand->setChecked(scenario!="unchecked");game.chkBotNoCheckDeck->setChecked(true);game.chkBotNoShuffleDeck->setChecked(true);game.chkBotUndoLoopback->setChecked(true);game.cbBotRule->setSelected(2);game.gameConf.bot_room_public=0;
    window=static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd);CHECK(window);ShowWindow(window,SW_HIDE);
    auto menu=[](irr::gui::IGUIElement* widget){irr::SEvent event{};event.EventType=irr::EET_GUI_EVENT;event.GUIEvent.Caller=widget;event.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;game.menuHandler.OnEvent(event);};
    menu(game.btnSingleMode);
    int selection=-1;for(size_t i=0;i<game.botInfo.size();++i)if(std::wstring(game.botInfo[i].command).find(L"Deck=ChainBurn")!=std::wstring::npos){selection=int(i);break;}CHECK(selection>=0);game.lstBotList->setSelected(selection);
    std::ostringstream text;text<<"#main\n";for(int i=0;i<40;++i)text<<"48305365\n";text<<"#extra\n!side\n";std::ofstream("deck/mainloop-human.ydk")<<text.str();std::istringstream deck(text.str());DeckManager::LoadDeckFromStream(human,deck);CHECK(human.main.size()==40);
    menu(game.btnStartBot);CHECK(NetServer::IsRunning());begin=GetTickCount64();CHECK(SetTimer(window,1,100,tick));
    std::cout<<"ENTER actual Game::MainLoop pid="<<GetCurrentProcessId()<<" scenario="<<scenario<<std::endl;
    game.MainLoop();CHECK(passed);NetServer::StopServer();std::cout<<"MainLoop returned"<<std::endl;return 0;
}catch(const std::exception& error){fail(error.what());return 2;}}