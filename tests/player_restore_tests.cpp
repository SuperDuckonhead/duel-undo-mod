#include "card_data.h"
#include "game.h"
#include "client_card.h"
#include "materials.h"
#include "network.h"
#include "ocgapi.h"
#include "test_support.h"
#include "undo/player_restore.h"
#include "undo/player_visible_filter.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
static bool rejectAllocation = false;
void *operator new(std::size_t n) {
    if (rejectAllocation)
        throw std::bad_alloc();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { std::free(p); }
template <class F> bool throws(F action) {
    try {
        action();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
#include <iostream>
using namespace undo;
// GUI is deliberately unavailable: actual model code must not dispatch an event.
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent &) { throw std::runtime_error("GUI during prepare"); }
} // namespace ygo
static void u32(Bytes &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(v >> (i * 8));
}
static Bytes start(unsigned player = 0) {
    Bytes b{MSG_START, (uint8_t)player, 4};
    u32(b, 8000);
    u32(b, 8000);
    b.insert(b.end(), {2, 0, 1, 0, 2, 0, 1, 0});
    return b;
}
static Bytes move(unsigned code, unsigned p, unsigned src, unsigned seq, unsigned dst, unsigned ds,
                  unsigned pos) {
    Bytes b{MSG_MOVE};
    u32(b, code);
    b.insert(b.end(), {(uint8_t)p, (uint8_t)src, (uint8_t)seq, 0, (uint8_t)p, (uint8_t)dst, (uint8_t)ds,
                       (uint8_t)pos});
    u32(b, 0);
    return b;
}
static TxKey key() {
    TxKey k;
    k.session[0] = 7;
    k.epoch = 9;
    k.request = 2;
    k.targetIndex = 1;
    return k;
}
static Bytes prompt() { return {MSG_SELECT_CARD, 0, 0, 1, 1, 1, 0x11, 0x11, 0, 0, 0, LOCATION_MZONE, 0, 0}; }
static std::vector<Bytes> history() {
    return {start(),
            move(0x1111, 0, LOCATION_DECK, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK),
            move(0, 1, LOCATION_DECK, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE),
            {MSG_NEW_TURN, 0},
            {MSG_NEW_PHASE, 4, 0}};
}
static byte *emptyScript(const char *, int *len) {
    static byte empty[1]{};
    *len = 0;
    return empty;
}
static uint32_t visibleCard(uint32_t code, card_data *data) {
    *data = {};
    data->code = code;
    data->type = TYPE_MONSTER | TYPE_NORMAL;
    data->level = 4;
    data->attack = 1900;
    return 0;
}
static bool containsCode(const Bytes &b, uint32_t code) {
    Bytes c;
    u32(c, code);
    return std::search(b.begin(), b.end(), c.begin(), c.end()) != b.end();
}
static void actualPrivacy() {
    set_script_reader(emptyScript);
    set_card_reader(visibleCard);
    uint32_t seed[SEED_COUNT]{};
    auto core = create_duel_v2(seed);
    constexpr uint32_t handId = 0x17283940, facedownId = 0x15263748, publicId = 0x13243546;
    new_card(core, handId, 1, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    new_card(core, facedownId, 1, 1, LOCATION_MZONE, 0, POS_FACEDOWN_DEFENSE);
    new_card(core, publicId, 1, 1, LOCATION_MZONE, 1, POS_FACEUP_ATTACK);
    start_duel(core, 5u << 16);
    Bytes reload(4096);
    reload.resize(query_field_info(core, reload.data()));
    std::vector<Bytes> filteredFrames{reload};
    for (unsigned loc : {LOCATION_HAND, LOCATION_MZONE}) {
        Bytes raw(65536);
        raw[0] = MSG_UPDATE_DATA;
        raw[1] = 1;
        raw[2] = loc;
        int n = query_field_card(core, 1, loc, QUERY_CODE | QUERY_POSITION | QUERY_ATTACK, raw.data() + 3, 0);
        CHECK(n > 0);
        raw.resize(n + 3);
        const auto full = raw;
        auto projected = FilterVisibleQuery(raw);
        CHECK(raw == full);
        CHECK(projected.owner == raw);
        CHECK(containsCode(projected.owner, loc == LOCATION_HAND ? handId : facedownId));
        CHECK(!containsCode(projected.opponent, handId));
        CHECK(!containsCode(projected.opponent, facedownId));
        if (loc == LOCATION_MZONE)
            CHECK(containsCode(projected.opponent, publicId));
        // Compare every byte against the pre-extraction baseline loop from SingleDuel.
        Bytes old = raw;
        size_t at = 3;
        while (at < old.size()) {
            uint32_t len;
            std::memcpy(&len, old.data() + at, 4);
            if (len > 4) {
                unsigned pos = old[at + 15];
                bool hidden = loc == LOCATION_HAND ? !(pos & POS_FACEUP)
                                                   : ((pos & POS_FACEDOWN) && !(pos & POS_REVEAL));
                if (loc != LOCATION_HAND)
                    old[at + 15] &= ~POS_REVEAL;
                if (hidden)
                    std::memset(old.data() + at + 4, 0, len - 4);
            }
            at += len;
        }
        CHECK(projected.opponent == old);
        filteredFrames.push_back(projected.opponent);
    }
    ygo::ClientField live;
    PlayerViewState state;
    ClientRestore restore(live, state, 0, key().session, 9);
    auto packet = BuildPlayerRestore(0, filteredFrames, {MSG_WAITING});
    CHECK(restore.Prepare(key(), packet, {MSG_WAITING}));
    CHECK(restore.PreparedField()->hand[1][0]->code == 0);
    CHECK(restore.PreparedField()->mzone[1][0]->code == 0);
    CHECK(restore.PreparedField()->mzone[1][1]->code == publicId);
    restore.Abort(key());
    filteredFrames.insert(filteredFrames.begin(), reload);
    filteredFrames.push_back({MSG_TOSS_COIN, 0, 1, 1});
    auto reloaded = BuildPlayerRestore(0, filteredFrames, {MSG_WAITING});
    CHECK(restore.Prepare(key(), reloaded, {MSG_WAITING}));
    restore.Abort(key());
    end_duel(core);
}
static void reviewedMessageRegressions() {
    unsigned failures = 0;
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << name << " passed\n"; }
        catch(const std::exception& error) { ++failures; std::cerr << name << ": " << error.what() << '\n'; }
    };
    run("I1 confirm skip_panel", [] {
        for(unsigned skip : {0U, 1U}) {
            ygo::ClientField live;
            PlayerViewState state;
            ClientRestore restore(live, state, 0, key().session, 9);
            const std::vector<Bytes> initial{start(), move(0, 0, LOCATION_DECK, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE)};
            Bytes confirm{MSG_CONFIRM_CARDS, 0, static_cast<uint8_t>(skip), 1};
            u32(confirm, 0x61524312);
            confirm.insert(confirm.end(), {0, LOCATION_HAND, 0});
            auto frames = initial;
            frames.push_back(confirm);
            CHECK(restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
            CHECK(restore.PreparedField()->hand[0][0]->code == 0x61524312);
            CHECK(live.hand[0].empty());
            restore.Abort(key());
            auto hidden = confirm;
            std::fill(hidden.begin() + 4, hidden.begin() + 8, 0);
            frames.push_back(hidden);
            CHECK(restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
            CHECK(restore.PreparedField()->hand[0][0]->code == 0x61524312);
            restore.Abort(key());
            for(size_t n = 1; n < confirm.size(); ++n) {
                frames = initial;
                frames.emplace_back(confirm.begin(), confirm.begin() + n);
                CHECK(!restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
                CHECK(live.hand[0].empty());
            }
        }
    });
    run("I2 recipient-relative place mask", [] {
        for(unsigned recipient : {0U, 1U})
            for(unsigned message : {MSG_SELECT_PLACE, MSG_SELECT_DISFIELD})
                for(uint32_t allowed : {1U, 0x10000U})
                    for(unsigned count : {1U}) {
                        ygo::ClientField live;
                        PlayerViewState state;
                        ClientRestore restore(live, state, recipient, key().session, 9);
                        Bytes target{static_cast<uint8_t>(message), static_cast<uint8_t>(recipient), static_cast<uint8_t>(count)};
                        u32(target, ~allowed);
                        CHECK(restore.Prepare(key(), BuildPlayerRestore(recipient, {start(recipient)}, target), target));
                        CHECK(restore.PreparedField()->selectable_field == allowed);
                        CHECK(restore.PreparedField()->select_min == 1);
                        CHECK(restore.PreparedField()->select_cancelable == (count == 0));
                    }
    });
    run("I2 count-zero cancellation", [] {
        for(unsigned recipient : {0U, 1U})
            for(unsigned message : {MSG_SELECT_PLACE, MSG_SELECT_DISFIELD})
                for(uint32_t allowed : {1U, 0x10000U})
                    for(unsigned count : {0U}) {
                        ygo::ClientField live;
                        PlayerViewState state;
                        ClientRestore restore(live, state, recipient, key().session, 9);
                        Bytes target{static_cast<uint8_t>(message), static_cast<uint8_t>(recipient), static_cast<uint8_t>(count)};
                        u32(target, ~allowed);
                        CHECK(restore.Prepare(key(), BuildPlayerRestore(recipient, {start(recipient)}, target), target));
                        CHECK(restore.PreparedField()->selectable_field == allowed);
                        CHECK(restore.PreparedField()->select_min == 1);
                        CHECK(restore.PreparedField()->select_cancelable == (count == 0));
                    }
    });
    run("I3 extra face-down top offset", [] {
        ygo::ClientField live;
        PlayerViewState state;
        ClientRestore restore(live, state, 0, key().session, 9);
        auto initial = std::vector<Bytes>{start(), move(0, 0, LOCATION_DECK, 1, LOCATION_EXTRA, 1, POS_FACEDOWN_DEFENSE),
                                        move(0x72536415, 0, LOCATION_DECK, 0, LOCATION_EXTRA, 2, POS_FACEUP_ATTACK)};
        Bytes type{MSG_UPDATE_CARD, 0, LOCATION_EXTRA, 2};
        u32(type, 12); u32(type, QUERY_TYPE); u32(type, TYPE_MONSTER | TYPE_PENDULUM);
        initial.push_back(type);
        Bytes confirm{MSG_CONFIRM_EXTRATOP, 0, 1};
        u32(confirm, 0x61524312);
        confirm.insert(confirm.end(), {0, LOCATION_EXTRA, 1});
        auto frames = initial;
        frames.push_back(confirm);
        CHECK(restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
        CHECK(restore.PreparedField()->extra_p_count[0] == 1);
        CHECK(restore.PreparedField()->extra[0][2]->type & TYPE_PENDULUM);
        CHECK(restore.PreparedField()->extra[0][2]->position == POS_FACEUP_ATTACK);
        CHECK(restore.PreparedField()->extra[0][1]->code == 0x61524312);
        CHECK(restore.PreparedField()->extra[0][2]->code == 0x72536415);
        restore.Abort(key());
        auto hidden = confirm;
        std::fill(hidden.begin() + 3, hidden.begin() + 7, 0);
        frames.push_back(hidden);
        CHECK(restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
        CHECK(restore.PreparedField()->extra[0][1]->code == 0x61524312);
        CHECK(restore.PreparedField()->extra[0][2]->code == 0x72536415);
        restore.Abort(key());
        Bytes excess{MSG_CONFIRM_EXTRATOP, 0, 3};
        for(unsigned i = 0; i < 3; ++i) {
            u32(excess, 1);
            excess.insert(excess.end(), {0, LOCATION_EXTRA, static_cast<uint8_t>(i)});
        }
        frames = initial;
        frames.push_back(excess);
        CHECK(!restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
        for(size_t n = 1; n < confirm.size(); ++n) {
            frames = initial;
            frames.emplace_back(confirm.begin(), confirm.begin() + n);
            CHECK(!restore.Prepare(key(), BuildPlayerRestore(0, frames, {MSG_WAITING}), {MSG_WAITING}));
        }
    });
    run("I4 effect prompt disclosure", [] {
        for(unsigned recipient : {0U, 1U})
            for(unsigned location : {LOCATION_DECK, LOCATION_MZONE}) {
                ygo::ClientField live;
                PlayerViewState state;
                ClientRestore restore(live, state, recipient, key().session, 9);
                auto frames = std::vector<Bytes>{start(recipient)};
                if(location == LOCATION_MZONE)
                    frames.push_back(move(0, recipient, LOCATION_DECK, 1, LOCATION_MZONE, 0, POS_FACEDOWN_DEFENSE));
                Bytes effect{MSG_SELECT_EFFECTYN, static_cast<uint8_t>(recipient)};
                u32(effect, 0x61524312);
                effect.insert(effect.end(), {static_cast<uint8_t>(recipient), static_cast<uint8_t>(location), 0, 0});
                u32(effect, 123);
                CHECK(restore.Prepare(key(), BuildPlayerRestore(recipient, frames, effect), effect));
                CHECK((location == LOCATION_DECK ? restore.PreparedField()->deck[0][0] : restore.PreparedField()->mzone[0][0])->code == 0x61524312);
                CHECK(restore.PreparedField()->highlighting_card == nullptr);
                restore.Abort(key());
                frames.push_back(effect);
                CHECK(restore.Prepare(key(), BuildPlayerRestore(recipient, frames, {MSG_WAITING}), {MSG_WAITING}));
                CHECK((location == LOCATION_DECK ? restore.PreparedField()->deck[0][0] : restore.PreparedField()->mzone[0][0])->code == 0x61524312);
                CHECK(restore.PreparedField()->highlighting_card == nullptr);
            }
    });
    run("M1 chain zone activation", [] {
        for(unsigned recipient : {0U, 1U})
            for(unsigned location : {LOCATION_DECK, LOCATION_GRAVE, LOCATION_REMOVED, LOCATION_EXTRA}) {
                ygo::ClientField live;
                PlayerViewState state;
                ClientRestore restore(live, state, recipient, key().session, 9);
                auto frames = std::vector<Bytes>{start(recipient)};
                if(location == LOCATION_GRAVE || location == LOCATION_REMOVED)
                    frames.push_back(move(0x61524312, recipient, LOCATION_DECK, 1, location, 0, POS_FACEUP_ATTACK));
                Bytes chain{MSG_SELECT_CHAIN, static_cast<uint8_t>(recipient), 1, 1};
                u32(chain, 0); u32(chain, 0);
                chain.insert(chain.end(), {0, 0});
                u32(chain, 0x61524312);
                chain.insert(chain.end(), {static_cast<uint8_t>(recipient), static_cast<uint8_t>(location), 0, 0});
                u32(chain, 123);
                CHECK(restore.Prepare(key(), BuildPlayerRestore(recipient, frames, chain), chain));
                auto field = restore.PreparedField();
                CHECK(field->deck_act[0] == (location == LOCATION_DECK));
                CHECK(field->grave_act[0] == (location == LOCATION_GRAVE));
                CHECK(field->remove_act[0] == (location == LOCATION_REMOVED));
                CHECK(field->extra_act[0] == (location == LOCATION_EXTRA));
                CHECK(field->activatable_cards.at(0)->cmdFlag & COMMAND_ACTIVATE);
            }
    });
    run("M1 pendulum idle activation", [] {
        for(unsigned rule : {3U, 4U, 5U}) {
            ygo::ClientField live;
            PlayerViewState state;
            ClientRestore restore(live, state, 0, key().session, 9);
            const auto left = static_cast<uint8_t>(rule >= 4 ? 0 : 6);
            auto begin = start(); begin[2] = rule;
            auto frames = std::vector<Bytes>{begin, move(0x61524312, 0, LOCATION_DECK, 1, LOCATION_SZONE, left, POS_FACEUP_ATTACK)};
            Bytes query{MSG_UPDATE_CARD, 0, LOCATION_SZONE, left};
            u32(query, 12); u32(query, QUERY_TYPE); u32(query, TYPE_MONSTER | TYPE_PENDULUM);
            frames.push_back(query);
            Bytes idle{MSG_SELECT_IDLECMD, 0, 0, 1};
            u32(idle, 0x61524312);
            idle.insert(idle.end(), {0, LOCATION_SZONE, left, 0, 0, 0, 0, 1, 1, 0});
            CHECK(restore.Prepare(key(), BuildPlayerRestore(0, frames, idle), idle));
            CHECK(restore.PreparedField()->pzone_act[0]);
            CHECK(restore.PreparedField()->szone[0][left]->cmdFlag & COMMAND_SPSUMMON);
        }
    });
    CHECK(failures == 0);
}

int main() {
    reviewedMessageRegressions();
    actualPrivacy();
    Bytes cached{MSG_UPDATE_DATA, 0, LOCATION_HAND, 8, 0, 0, 0, 0, 0, 0, 0};
    CHECK(FilterVisibleQuery(cached).owner == cached);
    ygo::ClientField live;
    PlayerViewState state;
    state.lp[0] = 1234;
    auto sentinel = live.CreateCard();
    sentinel->code = 0xeeee;
    live.hand[0].push_back(sentinel);
    live.hovered_card = sentinel;
    live.selected_cards.push_back(sentinel);
    ClientRestore restore(live, state, 0, key().session, 9);
    auto packet = BuildPlayerRestore(0, history(), prompt());
    CHECK(restore.Prepare(key(), packet, prompt()));
    CHECK(live.hand[0][0] == sentinel && state.lp[0] == 1234);
    CHECK(restore.PreparedField()->hand[1][0]->code == 0);
    CHECK(restore.PreparedField()->mzone[0][0]->code == 0x1111);
    rejectAllocation = true;
    bool installed = restore.Commit(key(), 10);
    rejectAllocation = false;
    CHECK(installed);
    CHECK(restore.Paused());
    CHECK(!restore.Commit(key(), 10));
    CHECK(live.hovered_card == nullptr && live.selected_cards.empty());
    CHECK(live.selectable_cards.size() == 1);
    CHECK(state.lp[0] == 8000 && state.turn == 1 && state.phase == 4);
    CHECK(state.prompt == prompt());
    CHECK(!restore.AcceptsGameplay(key().session, 10));
    CHECK(restore.Resume(key()));
    CHECK(restore.AcceptsGameplay(key().session, 10));
    CHECK(!restore.AcceptsGameplay(key().session, 9));
    auto encoded = EncodePlayerRestore(packet);
    CHECK(DecodePlayerRestore(encoded).frames == packet.frames);
    PlayerRestoreAssembler assembler(key(), 0);
    for (auto &f : Fragment(encoded))
        CHECK(assembler.Add(key(), 0, f));
    CHECK(assembler.Finish().visibleDigest == packet.visibleDigest);
    auto bad = packet;
    bad.frames[1][1] ^= 1;
    ClientRestore fail(live, state, 0, key().session, 9);
    CHECK(!fail.Prepare(key(), bad, prompt()));
    bad = packet;
    std::swap(bad.frames[0], bad.frames[1]);
    bad.visibleDigest = HashVisibleRestore(bad);
    CHECK(!fail.Prepare(key(), bad, prompt()));
    bad = packet;
    bad.frames[1].pop_back();
    bad.visibleDigest = HashVisibleRestore(bad);
    CHECK(!fail.Prepare(key(), bad, prompt()));
    bad = packet;
    bad.player = 1;
    bad.visibleDigest = HashVisibleRestore(bad);
    CHECK(!fail.Prepare(key(), bad, prompt()));
    CHECK(!fail.Prepare(key(), packet, {MSG_WAITING}));
    auto wrong = key();
    wrong.request++;
    PlayerRestoreAssembler bound(key(), 0);
    CHECK(!bound.Add(wrong, 0, Fragment(encoded)[0]));
    CHECK(fail.Prepare(key(), packet, prompt()));
    fail.Abort(wrong);
    CHECK(fail.PreparedField() != nullptr);
    fail.Abort(key());
    CHECK(fail.PreparedField() == nullptr);
    CHECK(live.mzone[0][0]->code == 0x1111);

    // A real idle-command prompt must prepare command lists before installation.
    Bytes idle{MSG_SELECT_IDLECMD, 0, 1};
    u32(idle, 0x1111);
    idle.insert(idle.end(), {0, LOCATION_MZONE, 0, 0, 0, 0, 0, 0, 1, 1, 0});
    auto idleRestore = BuildPlayerRestore(0, history(), idle);
    CHECK(fail.Prepare(key(), idleRestore, idle));
    CHECK(fail.PreparedField()->summonable_cards.size() == 1);
    fail.Abort(key());
    auto rich = history();
    rich.push_back(move(0x2222, 0, LOCATION_DECK, 0, LOCATION_MZONE | LOCATION_OVERLAY, 0, 0));
    Bytes q{MSG_UPDATE_CARD, 0, LOCATION_MZONE, 0};
    u32(q, 24);
    u32(q, QUERY_CODE | QUERY_ATTACK | QUERY_COUNTERS);
    u32(q, 0x1111);
    u32(q, 1900);
    u32(q, 1);
    q.insert(q.end(), {1, 0, 3, 0});
    rich.push_back(q);
    Bytes ch{MSG_CHAINING};
    u32(ch, 0x1111);
    ch.insert(ch.end(), {0, LOCATION_MZONE, 0, 0, 0, LOCATION_MZONE, 0});
    u32(ch, 123);
    ch.push_back(1);
    rich.push_back(ch);
    rich.push_back({MSG_BECOME_TARGET, 1, 0, LOCATION_MZONE, 0, 0});
    rich.push_back({MSG_CHAINED, 1});
    auto full = BuildPlayerRestore(0, rich, prompt());
    CHECK(fail.Prepare(key(), full, prompt()));
    CHECK(fail.PreparedField()->overlay_cards.size() == 1);
    CHECK(fail.PreparedField()->mzone[0][0]->counters.at(1) == 3);
    CHECK(fail.PreparedField()->chains.size() == 1);
    CHECK(fail.PreparedField()->chains[0].target.count(fail.PreparedField()->mzone[0][0]) == 1);
    fail.Abort(key());
    auto prior = history();
    prior.push_back(prompt());
    prior.push_back({MSG_HINT, HINT_SELECTMSG, 0, 42, 0, 0, 0});
    prior.push_back({MSG_PLAYER_HINT, 0, PHINT_DESC_ADD, 17, 0, 0, 0});
    auto prefixed = BuildPlayerRestore(0, prior, idle);
    CHECK(fail.Prepare(key(), prefixed, idle));
    CHECK(fail.PreparedField()->selectable_cards.empty());
    CHECK(fail.PreparedField()->selected_cards.empty());
    CHECK(fail.PreparedField()->select_hint == 42);
    CHECK(fail.PreparedField()->player_desc_hints[0].at(17) == 1);
    fail.Abort(key());
    std::vector<Bytes> prompts;
    Bytes tribute{MSG_SELECT_TRIBUTE, 0, 0, 2, 2, 1};
    u32(tribute, 0x1111);
    tribute.insert(tribute.end(), {0, LOCATION_MZONE, 0, 2});
    prompts.push_back(tribute);
    Bytes sum{MSG_SELECT_SUM, 0, 0};
    u32(sum, 4);
    sum.insert(sum.end(), {1, 1, 0, 1});
    u32(sum, 0x1111);
    sum.insert(sum.end(), {0, LOCATION_MZONE, 0});
    u32(sum, 4);
    prompts.push_back(sum);
    Bytes counter{MSG_SELECT_COUNTER, 0, 1, 0, 2, 0, 1};
    u32(counter, 0x1111);
    counter.insert(counter.end(), {0, LOCATION_MZONE, 0, 3, 0});
    prompts.push_back(counter);
    Bytes unselect{MSG_SELECT_UNSELECT_CARD, 0, 1, 0, 1, 1, 0, 1};
    u32(unselect, 0x1111);
    unselect.insert(unselect.end(), {0, LOCATION_MZONE, 0, 0});
    prompts.push_back(unselect);
    prompts.push_back({MSG_ANNOUNCE_RACE, 0, 1, 1, 0, 0, 0});
    prompts.push_back({MSG_ANNOUNCE_ATTRIB, 0, 1, 1, 0, 0, 0});
    prompts.push_back({MSG_ANNOUNCE_NUMBER, 0, 2, 1, 0, 0, 0, 2, 0, 0, 0});
    prompts.push_back({MSG_ANNOUNCE_CARD, 0, 1, 1, 0, 0, 0});
    for (auto &target : prompts) {
        auto packet = BuildPlayerRestore(0, history(), target);
        if (!fail.Prepare(key(), packet, target))
            throw std::runtime_error(fail.Error());
        if (target[0] == MSG_SELECT_TRIBUTE) {
            CHECK(fail.PreparedField()->selectsum_all.size() == 1);
            CHECK(fail.PreparedField()->selectsum_all[0]->opParam == 0x20001);
        }
        if (target[0] == MSG_SELECT_SUM)
            CHECK(fail.PreparedField()->selectable_cards.size() == 1);
        if (target[0] == MSG_SELECT_COUNTER)
            CHECK(fail.PreparedField()->select_counter_count == 2);
        if (target[0] == MSG_SELECT_UNSELECT_CARD)
            CHECK(fail.PreparedField()->selectable_cards[0]->is_selected);
        fail.Abort(key());
        for (size_t n = 1; n < target.size(); ++n) {
            Bytes truncated(target.begin(), target.begin() + n);
            auto bad = BuildPlayerRestore(0, history(), truncated);
            CHECK(!fail.Prepare(key(), bad, truncated));
        }
    }
    auto remembered = history();
    remembered.push_back(move(0, 0, LOCATION_MZONE, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE));
    auto memory = BuildPlayerRestore(0, remembered, {MSG_WAITING});
    CHECK(fail.Prepare(key(), memory, {MSG_WAITING}));
    CHECK(fail.PreparedField()->hand[0][0]->code == 0x1111);
    fail.Abort(key());
    auto combat = history();
    combat.push_back({MSG_ATTACK, 0, LOCATION_MZONE, 0, 0, 1, 0, 0, 0});
    Bytes battle{MSG_BATTLE, 0, LOCATION_MZONE, 0, 0};
    u32(battle, 1900);
    u32(battle, 1000);
    battle.insert(battle.end(), {0, 1, 0, 0, 0});
    u32(battle, 0);
    u32(battle, 0);
    battle.push_back(0);
    combat.push_back(battle);
    auto fought = BuildPlayerRestore(0, combat, prompt());
    CHECK(fail.Prepare(key(), fought, prompt()));
    CHECK(fail.PreparedField()->attacker == fail.PreparedField()->mzone[0][0]);
    CHECK(fail.PreparedField()->mzone[0][0]->attack == 1900);
    fail.Abort(key());
    auto swaps = history();
    swaps.push_back(move(0x2222, 0, LOCATION_DECK, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK));
    Bytes sw{MSG_SWAP};
    u32(sw, 0x1111);
    sw.insert(sw.end(), {0, LOCATION_MZONE, 0, 1});
    u32(sw, 0x2222);
    sw.insert(sw.end(), {0, LOCATION_MZONE, 1, 1});
    swaps.push_back(sw);
    auto swapped = BuildPlayerRestore(0, swaps, {MSG_WAITING});
    CHECK(fail.Prepare(key(), swapped, {MSG_WAITING}));
    CHECK(fail.PreparedField()->mzone[0][1]->code == 0x1111);
    fail.Abort(key());
    swaps.push_back({MSG_SHUFFLE_SET_CARD, LOCATION_MZONE, 2, 0, LOCATION_MZONE, 0, 0, 0, LOCATION_MZONE, 1,
                     0, 0, LOCATION_MZONE, 1, 0, 0, LOCATION_MZONE, 0, 0});
    auto shuffled = BuildPlayerRestore(0, swaps, {MSG_WAITING});
    CHECK(fail.Prepare(key(), shuffled, {MSG_WAITING}));
    CHECK(fail.PreparedField()->mzone[0][0]->code == 0);
    fail.Abort(key());
    auto gd = history();
    gd.push_back(move(0x3333, 0, LOCATION_DECK, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK));
    gd.push_back({MSG_SWAP_GRAVE_DECK, 0});
    auto graveSwap = BuildPlayerRestore(0, gd, {MSG_WAITING});
    CHECK(fail.Prepare(key(), graveSwap, {MSG_WAITING}));
    CHECK(fail.PreparedField()->deck[0].size() == 1 && fail.PreparedField()->grave[0].empty());
    fail.Abort(key());
    auto largeHistory = history();
    for (unsigned i = 0; i < 8000; ++i)
        largeHistory.push_back({MSG_HINT, HINT_EVENT, 0, 0, 0, 0, 0});
    auto large = EncodePlayerRestore(BuildPlayerRestore(0, largeHistory, prompt()));
    auto fragments = Fragment(large);
    CHECK(fragments.size() > 1);
    PlayerRestoreAssembler missing(key(), 0);
    CHECK(missing.Add(key(), 0, fragments[0]));
    CHECK(throws([&] { missing.Finish(); }));
    CHECK(!missing.Add(key(), 0, fragments[0]));
    CHECK(throws([&] { missing.Finish(); }));
    PlayerRestoreAssembler wrongSeat(key(), 0);
    CHECK(!wrongSeat.Add(key(), 1, fragments[0]));
    PlayerRestoreAssembler ordered(key(), 0);
    CHECK(!ordered.Add(key(), 0, fragments[1]));
    auto corrupted = encoded;
    corrupted.back() ^= 1;
    CHECK(throws([&] { DecodePlayerRestore(corrupted); }));
    for (size_t n = 0; n < encoded.size(); ++n) {
        Bytes prefix(encoded.begin(), encoded.begin() + n);
        CHECK(throws([&] { DecodePlayerRestore(prefix); }));
    }
    auto unknown = history();
    unknown.push_back({255});
    CHECK(!fail.Prepare(key(), BuildPlayerRestore(0, unknown, prompt()), prompt()));
    auto wrongSession = key();
    wrongSession.session[0] ^= 1;
    CHECK(!fail.Prepare(wrongSession, packet, prompt()));
    wrongSession = key();
    ++wrongSession.epoch;
    CHECK(!fail.Prepare(wrongSession, packet, prompt()));
    auto relations = history();
    relations.push_back(move(0x2222, 0, LOCATION_DECK, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK));
    Bytes targets{MSG_UPDATE_CARD, 0, LOCATION_MZONE, 0};
    u32(targets, 16);
    u32(targets, QUERY_TARGET_CARD);
    u32(targets, 1);
    targets.insert(targets.end(), {0, LOCATION_MZONE, 1, 0});
    relations.push_back(targets);
    Bytes noTargets{MSG_UPDATE_CARD, 0, LOCATION_MZONE, 1};
    u32(noTargets, 12);
    u32(noTargets, QUERY_TARGET_CARD);
    u32(noTargets, 0);
    relations.push_back(noTargets);
    auto related = BuildPlayerRestore(0, relations, prompt());
    CHECK(fail.Prepare(key(), related, prompt()));
    CHECK(fail.PreparedField()->mzone[0][0]->cardTarget.count(fail.PreparedField()->mzone[0][1]) == 1);
    fail.Abort(key());
    auto doubled = rich;
    auto ch2 = ch;
    ch2.back() = 2;
    doubled.push_back(ch2);
    doubled.push_back({MSG_CHAINED, 2});
    auto doubleChain = BuildPlayerRestore(0, doubled, prompt());
    CHECK(fail.Prepare(key(), doubleChain, prompt()));
    CHECK(fail.PreparedField()->chains[0].chain_pos.Y != fail.PreparedField()->chains[1].chain_pos.Y);
    fail.Abort(key());
    auto manyStart = start();
    manyStart[11] = 25;
    Bytes costly{MSG_SELECT_SUM, 0, 0};
    u32(costly, 50);
    costly.insert(costly.end(), {1, 25, 0, 25});
    for (unsigned i = 0; i < 25; ++i) {
        u32(costly, 1);
        costly.insert(costly.end(), {0, LOCATION_DECK, (uint8_t)i});
        u32(costly, 1);
    }
    auto bounded = BuildPlayerRestore(0, {manyStart}, costly);
    CHECK(!fail.Prepare(key(), bounded, costly));
    CHECK(fail.Error() == "visible selection work limit");
    // Every truncation of a valid message rejects, with the live pointer unchanged.
    for (const auto &frame : rich)
        for (size_t len = 0; len < frame.size(); ++len) {
            auto truncated = rich;
            truncated[&frame - &rich[0]] = Bytes(frame.begin(), frame.begin() + len);
            if (!len)
                continue;
            auto malformed = BuildPlayerRestore(0, truncated, prompt());
            CHECK(!fail.Prepare(key(), malformed, prompt()));
            CHECK(live.mzone[0][0]->code == 0x1111);
        }
    std::cout << "player restore model checks passed\n";
}
