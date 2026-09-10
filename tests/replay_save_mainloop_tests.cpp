// Regression: a terminal Room pauses gameplay, but its replay save dialog must
// still receive native mouse/key input through the production MainLoop.
#include "game.h"
#include "duelclient.h"
#include "netserver.h"
#include "deck_manager.h"
#include "replay.h"
#include "room_client.h"
#include "test_support.h"
#include <event2/thread.h>
#include <winsock2.h>
#include <windows.h>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace ygo;
static Game game;
static HWND window;
static Deck human;
static std::string scenario = "edit-save";
static bool deckSent{}, started{}, handPosted{}, firstPosted{}, passed{};
static unsigned stage{}, digit{};
static ULONGLONG begin{}, actionAt{};
static std::wstring replayName;
static bool currentDateDefault{};
static std::wstring today() {
    const auto now = std::time(nullptr); wchar_t date[32]{};
    std::wcsftime(date, 32, L"%Y-%m-%d", std::localtime(&now)); return date;
}
static void fail(const std::string& why) {
    std::cerr << "FAIL replay save MainLoop: " << why << std::endl;
    ExitProcess(2);
}
static void mouse(irr::gui::IGUIElement* widget, const char* label) {
    CHECK(widget->isTrulyVisible() && widget->isEnabled());
    DWORD owner{};
    GetWindowThreadProcessId(window, &owner);
    CHECK(owner == GetCurrentProcessId());
    const auto point = widget->getAbsolutePosition().getCenter();
    const auto position = MAKELPARAM(point.X, point.Y);
    CHECK(PostMessageW(window, WM_MOUSEMOVE, 0, position));
    CHECK(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position));
    CHECK(PostMessageW(window, WM_LBUTTONUP, 0, position));
    std::cout << "POST real Win32 mouse " << label << std::endl;
}
static void key(UINT vk) {
    const LPARAM scan = LPARAM(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    CHECK(PostMessageW(window, WM_KEYDOWN, vk, 1 | scan));
    CHECK(PostMessageW(window, WM_KEYUP, vk, 1 | scan | LPARAM(0xc0000000u)));
}
static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
    try {
        const auto now = GetTickCount64();
        if(now - begin > 55000) fail("terminal flow deadline");
        std::unique_lock<std::mutex> lock(game.gMutex, std::try_to_lock);
        if(!lock.owns_lock()) return;
        auto room = DuelClient::Room();
        if(room && game.wHostPrepare->isVisible() && !deckSent) {
            DuelClient::SendUpdateDeck(human);
            DuelClient::SendPacketToServer(CTOS_HS_READY);
            deckSent = true;
        }
        if(deckSent && !started && game.chkHostPrepReady[0]->isChecked() &&
           game.chkHostPrepReady[1]->isChecked()) {
            DuelClient::SendPacketToServer(CTOS_HS_START);
            started = true;
        }
        if(!game.fadingList.empty()) return;
        if(game.wHand->isVisible() && !handPosted) {
            mouse(game.btnHand[1], "paper");
            handPosted = true;
        }
        if(game.wFTSelect->isVisible() && !firstPosted) {
            mouse(game.btnFirst, "first");
            firstPosted = true;
        }
        if(stage == 0 && room && !room->InputPaused() &&
           room->Token().prompt && game.dInfo.curMsg == MSG_SELECT_IDLECMD) {
            mouse(game.btnLeaveGame, "surrender menu");
            stage = 1;
        } else if(stage == 1 && game.wSurrender->isVisible()) {
            mouse(game.btnSurrenderYes, "surrender yes");
            stage = 2;
        } else if(stage == 2 && game.wReplaySave->isVisible()) {
            CHECK(room && room->InputPaused() && game.dInfo.isFinished);
            CHECK(!room->PresentationFrozen());
            replayName = game.ebRSName->getText();
            CHECK(!replayName.empty());
            currentDateDefault = replayName.substr(0, 10) == today();
            std::wcout << L"DEFAULT replay name=" << replayName << L" current-date=" << currentDateDefault << std::endl;
            std::cout << "REAL terminal replay dialog finished=" << game.dInfo.isFinished
                      << " paused=" << room->InputPaused()
                      << " frozen=" << room->PresentationFrozen()
                      << " prompt=" << room->Token().prompt
                      << " scenario=" << scenario << std::endl;
            if(scenario == "edit-save") {
                mouse(game.ebRSName, "replay filename");
                stage = 3;
            } else {
                mouse(scenario == "cancel" ? game.btnRSNo : game.btnRSYes,
                      scenario == "cancel" ? "replay cancel" : "replay save");
                stage = 7;
            }
            actionAt = now;
        } else if(stage == 2 && scenario == "auto-save" && game.wMessage->isVisible()) {
            CHECK(room && room->InputPaused() && game.dInfo.isFinished);
            unsigned count = 0;
            for(const auto& entry : std::filesystem::directory_iterator("replay")) {
                if(entry.path().extension() != L".yrp") continue;
                replayName = entry.path().stem().wstring();
                ++count;
            }
            CHECK(count == 1);
            currentDateDefault = replayName.substr(0, 10) == today();
            std::wcout << L"DEFAULT auto replay name=" << replayName << L" current-date=" << currentDateDefault << std::endl;
            Replay replay;
            CHECK(replay.OpenReplay((replayName + L".yrp").c_str()));
            CHECK(replay.pheader.base.flag & REPLAY_UNDO_CORE);
            mouse(game.btnMsgOK, "auto-save terminal acknowledgment");
            stage = 9;
            actionAt = now;
        } else if(stage == 3) {
            if(game.env->getFocus() != game.ebRSName) {
                if(now - actionAt > 2500) fail("native mouse cannot focus replay filename while Room input paused");
                return;
            }
            key(VK_HOME);
            stage = 4;
        } else if(stage == 4) {
            // Home and Delete use the real edit-box caret, never setText/setFocus.
            for(size_t i = 0; i < replayName.size(); ++i) key(VK_DELETE);
            stage = 5;
            actionAt = now;
        } else if(stage == 5) {
            if(std::wstring(game.ebRSName->getText()) != L"") {
                if(now - actionAt > 2500) fail("native Delete did not clear replay filename");
                return;
            }
            replayName = L"123456";
            stage = 6;
        } else if(stage == 6) {
            // One native key per tick lets TranslateMessage supply its WM_CHAR.
            if(digit < replayName.size()) key(UINT(replayName[digit++]));
            else {
                CHECK(std::wstring(game.ebRSName->getText()) == L"123456");
                CHECK(room && room->InputPaused() && !room->CanUndo());
                std::cout << "PASS native mouse focus + Home/Delete + filename typing: 123456" << std::endl;
                mouse(game.btnRSYes, "replay save edited filename");
                stage = 7;
                actionAt = now;
            }
        } else if(stage == 7) {
            if(game.wReplaySave->isVisible()) {
                if(now - actionAt > 2500) fail("native replay " + scenario + " click leaves dialog open while Room input paused");
                return;
            }
            CHECK(game.actionParam == (scenario == "cancel" ? 0 : 1));
            std::cout << "PASS native replay button closed dialog actionParam=" << game.actionParam << std::endl;
            stage = 8;
            actionAt = now;
        } else if(stage == 8 && game.wMessage->isVisible()) {
            CHECK(room && room->InputPaused());
            std::cout << "REAL terminal acknowledgment finished=" << game.dInfo.isFinished
                      << " paused=" << room->InputPaused()
                      << " frozen=" << room->PresentationFrozen() << std::endl;
            const auto path = std::filesystem::path(L"replay") / (replayName + L".yrp");
            if(scenario == "cancel") {
                CHECK(!std::filesystem::exists(path));
                CHECK(!std::filesystem::exists("replay/_LastReplay.yrp"));
                std::cout << "PASS host cancel wrote no replay" << std::endl;
            } else {
                CHECK(std::filesystem::is_regular_file(path));
                Replay replay;
                CHECK(replay.OpenReplay((replayName + L".yrp").c_str()));
                CHECK(replay.pheader.base.flag & REPLAY_UNDO_CORE);
                std::cout << "PASS saved replay reopens as undo replay" << std::endl;
            }
            mouse(game.btnMsgOK, "terminal acknowledgment");
            stage = 9;
            actionAt = now;
        } else if(stage == 9) {
            if(game.wMessage->isVisible() && now - actionAt > 2500)
                fail("native terminal acknowledgment leaves dialog open");
            if(!DuelClient::Room() && !game.dInfo.isInDuel && game.wSinglePlay->isVisible()) {
                CHECK(currentDateDefault);
                passed = true;
                std::cout << "PASS actual MainLoop terminal replay " << scenario
                          << ", normal replay/action/close signals, and return to bot menu" << std::endl;
                KillTimer(window, 1);
                CHECK(PostMessageW(window, WM_CLOSE, 0, 0));
            }
        }
    } catch(const std::exception& error) { fail(error.what()); }
}
int main(int argc, char** argv) {
    try {
        if(argc == 2) scenario = argv[1];
        CHECK(scenario == "save" || scenario == "cancel" || scenario == "edit-save" || scenario == "auto-save");
        WSADATA ws{};
        CHECK(WSAStartup(MAKEWORD(2,2), &ws) == 0);
        CHECK(evthread_use_windows_threads() == 0);
        mainGame = &game;
        CHECK(game.Initialize(std::filesystem::current_path()));
        // Every frame/action/replay/close signal retains its production default.
        game.gameConf.quick_animation = 0;
        game.chkWaitChain->setChecked(false);
        game.chkMAutoPos->setChecked(true);
        game.chkSTAutoPos->setChecked(true);
        game.chkAutoSaveReplay->setChecked(scenario == "auto-save");
        game.chkBotHand->setChecked(true);
        game.chkBotNoCheckDeck->setChecked(true);
        game.chkBotNoShuffleDeck->setChecked(true);
        game.chkBotUndoLoopback->setChecked(true);
        game.cbBotRule->setSelected(2);
        game.gameConf.bot_room_public = 0;
        window = static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd);
        CHECK(window);
        ShowWindow(window, SW_HIDE);
        auto menu = [](irr::gui::IGUIElement* widget) {
            irr::SEvent event{};
            event.EventType = irr::EET_GUI_EVENT;
            event.GUIEvent.Caller = widget;
            event.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
            game.menuHandler.OnEvent(event);
        };
        menu(game.btnSingleMode);
        int selection = -1;
        for(size_t i = 0; i < game.botInfo.size(); ++i)
            if(std::wstring(game.botInfo[i].command).find(L"Deck=ChainBurn") != std::wstring::npos)
                { selection = int(i); break; }
        CHECK(selection >= 0);
        game.lstBotList->setSelected(selection);
        std::ostringstream text;
        text << "#main\n";
        for(int i = 0; i < 40; ++i) text << "48305365\n";
        text << "#extra\n!side\n";
        std::ofstream("deck/replay-save-human.ydk") << text.str();
        std::istringstream deck(text.str());
        DeckManager::LoadDeckFromStream(human, deck);
        CHECK(human.main.size() == 40);
        menu(game.btnStartBot);
        CHECK(NetServer::IsRunning());
        begin = GetTickCount64();
        CHECK(SetTimer(window, 1, 100, tick));
        std::cout << "ENTER actual Game::MainLoop pid=" << GetCurrentProcessId()
                  << " scenario=" << scenario << std::endl;
        game.MainLoop();
        CHECK(passed);
        NetServer::StopServer();
        std::cout << "MainLoop returned" << std::endl;
        return 0;
    } catch(const std::exception& error) { fail(error.what()); return 2; }
}
