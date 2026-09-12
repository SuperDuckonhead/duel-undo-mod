// Use the real transport publication slot and native Irrlicht/MainLoop paths.
// Only incoming server packets are controlled; no signal wait is disabled.
#include "duelclient.cpp"
#include "test_support.h"
#include <windows.h>
#include <atomic>
#include <iostream>

using namespace ygo;
using namespace undo;
static Game game;
static HWND window;
static std::shared_ptr<RoomClient> room;
static SessionId session;
static std::vector<Envelope> sent;
static uint64_t sequence;
static std::atomic<bool> workerDone{};
static std::thread worker;
static std::string scenario;
static unsigned stage;
static ULONGLONG startedAt, actionAt;
static bool passed, sawFrameWait;
static irr::core::dimension2du frozenSize;
static uint16_t frozenTime;

static void fail(const std::string& why) {
    std::cerr << "FAIL native Room UI (" << scenario << "): " << why << std::endl;
    ExitProcess(2);
}
static void put(Bytes& b, uint64_t n, unsigned count) {
    while(count--) { b.push_back(uint8_t(n)); n >>= 8; }
}
static void packet(Bytes bytes, uint64_t prompt = 1) {
    for(const auto& envelope : EncodeGamePacket(session, 0, prompt, ++sequence, bytes))
        room->Receive(envelope);
}
static void frame(Bytes bytes, uint64_t prompt = 1) {
    bytes.insert(bytes.begin(), STOC_GAME_MSG);
    packet(std::move(bytes), prompt);
}
static void boundary(TxState state = TxState::Running) {
    RoomStatus status;
    status.prompt = 1;
    status.promptPlayer = 0;
    status.eligibleMask = 1;
    status.state = state;
    status.timePlayer = 0;
    status.clock.remainingMs = {10000, 10000};
    room->Receive({WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
}
static void nextTimePacket() {
    // The original server sends its time packet before the next selection.
    // The old response is now stale, while presentation remains unfrozen.
    STOC_TimeLimit time;
    time.player = 0;
    time.left_time = 10;
    Bytes bytes{STOC_TIME_LIMIT};
    const auto* raw = reinterpret_cast<const uint8_t*>(&time);
    bytes.insert(bytes.end(), raw, raw + sizeof(time));
    packet(std::move(bytes), 2);
    CHECK(room->InputPaused() && !room->PresentationFrozen());
}
static void mouse(irr::gui::IGUIElement* widget) {
    CHECK(widget->isTrulyVisible() && widget->isEnabled());
    DWORD owner{};
    GetWindowThreadProcessId(window, &owner);
    CHECK(owner == GetCurrentProcessId());
    const auto point = widget->getAbsolutePosition().getCenter();
    const auto position = MAKELPARAM(point.X, point.Y);
    CHECK(PostMessageW(window, WM_MOUSEMOVE, 0, position));
    CHECK(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position));
    CHECK(PostMessageW(window, WM_LBUTTONUP, 0, position));
}
static void resize() {
    RECT rect{};
    CHECK(GetWindowRect(window, &rect));
    CHECK(SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left + 60,
                       rect.bottom - rect.top + 40, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
}
static size_t responses() {
    return std::count_if(sent.begin(), sent.end(), [](const Envelope& e) {
        return e.kind == WireKind::Response;
    });
}
static void finish(size_t expectedResponses = 0) {
    CHECK(workerDone);
    CHECK(sawFrameWait);
    worker.join();
    room->Poll();
    CHECK(responses() == expectedResponses);
    room->Close();
    std::atomic_store(&ygo::roomClient, std::shared_ptr<RoomClient>{});
    room.reset();
    passed = true;
    KillTimer(window, 1);
    CHECK(PostMessageW(window, WM_CLOSE, 0, 0));
    std::cout << "PASS actual MainLoop " << scenario << "; native signal waits retained" << std::endl;
}
static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
    try {
        const auto now = GetTickCount64();
        if(now - startedAt > 12000) fail("MainLoop deadline");
        std::unique_lock<std::mutex> lock(game.gMutex, std::try_to_lock);
        if(!lock.owns_lock()) return;
        if(game.signalFrame > 0) sawFrameWait = true;
        if(scenario == "confirm") {
            if(stage == 0 && game.wCardSelect->isTrulyVisible() && game.fadingList.empty()) {
                CHECK(room->InputPaused() && !room->PresentationFrozen() && room->Token().prompt == 1);
                CHECK(game.dInfo.curMsg == MSG_CONFIRM_CARDS && !workerDone);
                mouse(game.btnSelectOK);
                actionAt = now; stage = 1;
                std::cout << "POST native confirm mouse while original handler waits on actionSignal" << std::endl;
            } else if(stage == 1) {
                if(workerDone && !game.wCardSelect->isVisible() && game.fadingList.empty()) {
                    lock.unlock(); finish();
                } else if(now - actionAt > 2000) fail("native confirmation did not complete the original actionSignal wait");
            }
            return;
        }
        if(!workerDone) return;
        if(scenario == "early-status") {
            if(stage == 0 && game.wQuery->isTrulyVisible() && game.fadingList.empty()) {
                CHECK(room->InputPaused() && !room->PresentationFrozen());
                mouse(game.btnYes); stage = 1; actionAt = now;
                std::cout << "POST early native YES before the room boundary status arrives" << std::endl;
            } else if(stage == 1 && now - actionAt > 400) {
                CHECK(game.wQuery->isTrulyVisible() && game.fadingList.empty());
                lock.unlock(); room->Poll(); CHECK(responses() == 0); boundary();
                CHECK(!room->InputPaused()); stage = 2;
            } else if(stage == 2) {
                CHECK(game.wQuery->isTrulyVisible() && game.fadingList.empty());
                mouse(game.btnYes); stage = 3; actionAt = now;
            } else if(stage == 3 && !game.wQuery->isVisible() && game.fadingList.empty()) {
                lock.unlock(); room->Poll(); CHECK(responses() == 1);
                const auto response = std::find_if(sent.begin(), sent.end(), [](const Envelope& e) { return e.kind == WireKind::Response; });
                CHECK(response != sent.end() && response->key.session == session && response->key.epoch == 0 && response->key.request == 1);
                std::cout << "PASS delayed status preserves visible selection and exactly one current-prompt response" << std::endl;
                finish(1);
            } else if(stage == 3 && now - actionAt > 2000) fail("YES did not complete after the room boundary opened input");
            return;
        }
        if(scenario == "fade") {
            if(stage == 0 && game.wQuery->isTrulyVisible() && game.fadingList.empty()) {
                CHECK(!room->InputPaused());
                mouse(game.btnYes); stage = 1; actionAt = now;
            } else if(stage == 1 && !game.fadingList.empty()) {
                CHECK(game.fadingList.back().signalAction && game.fadingList.back().fadingFrame > 0);
                lock.unlock(); nextTimePacket();
                stage = 2; actionAt = now;
                std::cout << "Native response fade active when next prompt invalidates its captured token" << std::endl;
            } else if(stage == 2) {
                if(game.fadingList.empty()) {
                    CHECK(!game.wQuery->isVisible());
                    CHECK(room->InputPaused() && !room->PresentationFrozen());
                    lock.unlock(); finish();
                } else if(now - actionAt > 2000) fail("obsolete native response fade stayed frozen by InputPaused");
            } else if(stage == 1 && now - actionAt > 2000) fail("native YES click did not produce its ordinary response fade");
            return;
        }
        if(stage == 0 && game.fadingList.empty()) {
            lock.unlock(); nextTimePacket(); lock.lock();
            resize(); actionAt = now; stage = 1;
        } else if(stage == 1 && now - actionAt > 1300) {
            CHECK(room->InputPaused() && !room->PresentationFrozen());
            if(scenario == "resize") {
                CHECK(game.window_size == game.driver->getScreenSize());
                CHECK(game.xScale > 1.0f && game.yScale > 1.0f);
            } else {
                CHECK(game.dInfo.time_left[0] < 10);
            }
            std::cout << "PASS ordinary response pause retains native " << scenario << std::endl;
            // Start a fade, then freeze it using an actual transaction status.
            game.wQuery->setVisible(true);
            game.HideElement(game.wQuery, true);
            lock.unlock(); boundary(TxState::Preparing); lock.lock();
            CHECK(room->PresentationFrozen());
            frozenSize = game.window_size;
            frozenTime = game.dInfo.time_left[0];
            resize(); actionAt = now; stage = 2;
        } else if(stage == 2 && now - actionAt > 1300) {
            CHECK(room->PresentationFrozen());
            CHECK(game.window_size == frozenSize && game.window_size != game.driver->getScreenSize());
            CHECK(game.dInfo.time_left[0] == frozenTime);
            CHECK(!game.fadingList.empty() && game.fadingList.back().fadingFrame == 10);
            std::cout << "PASS actual transaction freezes resize, visible clock, and response fade" << std::endl;
            lock.unlock(); finish();
        }
    } catch(const std::exception& e) { fail(e.what()); }
}
int main(int argc, char** argv) {
    try {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        CHECK(argc == 2);
        scenario = argv[1];
        CHECK(scenario == "confirm" || scenario == "fade" || scenario == "resize" || scenario == "clock" || scenario == "early-status");
        mainGame = &game;
        CHECK(game.Initialize(std::filesystem::current_path()));
        game.gameConf.quick_animation = 0;
        game.wMainMenu->setVisible(false);
        game.dInfo.isStarted = true;
        game.dInfo.isInDuel = true;
        game.dInfo.isSingleMode = false;
        game.dInfo.isReplay = false;
        game.device->setEventReceiver(&game.dField);
        window = static_cast<HWND>(game.driver->getExposedVideoData().OpenGLWin32.HWnd);
        CHECK(window); ShowWindow(window, SW_HIDE);
        session = NewSessionId();
        room = std::make_shared<RoomClient>(game, session,
            [](const Envelope& e) { sent.push_back(e); },
            [](const Bytes& bytes) { DuelClient::HandleLegacySTOC(const_cast<uint8_t*>(bytes.data()), bytes.size()); },
            [](const InputSubmission& response) { DuelClient::SendResponse(response); });
        std::atomic_store(&ygo::roomClient, room);
        worker = std::thread([] {
            try {
                Bytes start{MSG_START, 0, 4}; put(start, 8000, 4); put(start, 8000, 4);
                put(start, 2, 2); put(start, 0, 2); put(start, 2, 2); put(start, 0, 2);
                frame(std::move(start));
                if(scenario == "fade" || scenario == "early-status") {
                    Bytes yesno{MSG_SELECT_YESNO, 0}; put(yesno, 30, 4);
                    frame(std::move(yesno));
                } else frame({MSG_SELECT_IDLECMD, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0});
                if(scenario != "early-status") boundary();
                if(scenario == "confirm") {
                    Bytes confirm{MSG_CONFIRM_CARDS, 0, 0, 2};
                    for(uint8_t i = 0; i < 2; ++i) { put(confirm, 48305365, 4); confirm.insert(confirm.end(), {0, LOCATION_DECK, i}); }
                    frame(std::move(confirm));
                }
                workerDone = true;
            } catch(const std::exception& e) { fail(e.what()); }
        });
        startedAt = GetTickCount64();
        CHECK(SetTimer(window, 1, 10, tick));
        game.MainLoop();
        CHECK(passed);
        return 0;
    } catch(const std::exception& e) { fail(e.what()); return 2; }
}
