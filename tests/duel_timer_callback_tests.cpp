// Compile the actual callback in this translation unit; production code has
// no fault-injection entry point. The target excludes its duplicate source.
#include "netserver.cpp"
#include "test_support.h"
#include "game.h"
#include <iostream>

namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) { throw std::runtime_error("Unexpected GUI event"); }
void Game::AddDebugMsg(const char*) { throw std::runtime_error("Unexpected GUI diagnostic"); }
void DeckBuilder::RefreshPackListScroll() { throw std::runtime_error("Unexpected editor callback"); }

class TimerFaultDuel final : public SingleDuel {
public:
    explicit TimerFaultDuel(int fault) : SingleDuel(false), fault_(fault) {}
    void TimerTick() override {
        ++ticks;
        if(fault_ == 1) throw std::runtime_error("Timer transport failure");
        if(fault_ == 2) throw 7;
    }
    void EndDuel() override { ++ends; }
    unsigned ticks{}, ends{};
private:
    int fault_;
};

static void exercise(int fault, bool eventDispatch) {
    TimerFaultDuel duel(fault);
    net_evbase = event_base_new();
    CHECK(net_evbase);
    duel_mode = &duel;
    event* timer = nullptr;
    bool escaped = false;
    try {
        if(eventDispatch) {
            timer = event_new(net_evbase, -1, 0, DuelTimer, &duel);
            CHECK(timer);
            timeval immediate{};
            CHECK(event_add(timer, &immediate) == 0);
            CHECK(event_base_dispatch(net_evbase) >= 0);
        } else {
            // This first assertion gives the old callback a controlled red,
            // without relying on unwinding through the C event dispatcher.
            DuelTimer(-1, EV_TIMEOUT, &duel);
            CHECK(event_base_loop(net_evbase, EVLOOP_NONBLOCK) >= 0);
        }
    } catch(...) { escaped = true; }
    const bool exited = event_base_got_exit(net_evbase) != 0;
    if(timer) event_free(timer);
    duel_mode = nullptr;
    event_base_free(net_evbase);
    net_evbase = nullptr;
    CHECK(!escaped);
    CHECK(duel.ticks == 1);
    CHECK(duel.ends == (fault ? 1u : 0u));
    CHECK(exited == bool(fault));
}
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        WSADATA winsock{};
        CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
        for(int fault : {1, 2, 0}) ygo::exercise(fault, false);
        for(int fault : {1, 2, 0}) ygo::exercise(fault, true);
        WSACleanup();
        std::cout << "PASS actual DuelTimer contains standard/nonstandard exceptions, ends the room, and exits libevent; ordinary ticks continue\n";
    } catch(const std::exception& error) {
        WSACleanup();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
