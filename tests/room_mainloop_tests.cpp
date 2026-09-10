#include "game.h"
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
        if(room && !room->InputPaused() && room->Token().prompt && game.dInfo.curMsg==MSG_SELECT_IDLECMD && game.fadingList.empty()){
            CHECK(handPosted&&handHidden&&resultSeen);if(scenario!="unchecked")CHECK(firstPosted);if(scenario=="tie")CHECK(handRounds>=2&&resultAnimations>=2);if(scenario=="second")CHECK(!game.dInfo.isFirst&&game.dInfo.turn>=2);passed=true;
            std::cout<<"PASS actual Game::MainLoop normal frame/action signals, Win32 RPS mouse, real 60-frame result, first/second choice and initial human idle prompt="<<room->Token().prompt<<" scenario="<<scenario<<" turn="<<game.dInfo.turn<<" isFirst="<<game.dInfo.isFirst<<std::endl;
            KillTimer(window,1);PostMessageW(window,WM_CLOSE,0,0);
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