#include "player_restore.h"
#include "../client_card.h"
#include "../game.h"
#include "../network.h"
#include "resource_view.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace undo {
namespace {
void need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void put(Bytes &b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(v >> (8 * i));
}
struct Reader {
    const Bytes &b;
    std::size_t p{}, end;
    explicit Reader(const Bytes &bytes) : b(bytes), end(bytes.size()) {}
    Reader(const Bytes &bytes, std::size_t begin, std::size_t limit) : b(bytes), p(begin), end(limit) {}
    std::uint8_t u8() {
        need(p < end, "truncated player frame");
        return b[p++];
    }
    std::uint16_t u16() {
        auto a = u8();
        return a | (std::uint16_t(u8()) << 8);
    }
    std::uint32_t u32() {
        auto a = u16();
        return a | (std::uint32_t(u16()) << 16);
    }
    void skip(std::size_t n) {
        need(n <= end - p, "truncated player frame");
        p += n;
    }
    Bytes bytes(std::size_t n) {
        need(n <= end - p, "truncated restore");
        Bytes v(b.begin() + p, b.begin() + p + n);
        p += n;
        return v;
    }
    void done() { need(p == end, "trailing player bytes"); }
};
Bytes visible(const PlayerRestore &r) {
    need(r.player < 2, "invalid restore player");
    need(r.frames.size() <= 65536, "too many frames");
    Bytes b{'Y', 'G', 'O', 'V', 1, r.player};
    put(b, static_cast<std::uint32_t>(r.frames.size()));
    for (std::size_t i = 0; i < r.frames.size(); ++i) {
        const auto &f = r.frames[i];
        need(!f.empty() && f.size() <= MaxRestoreBytes, "invalid frame size");
        need(b.size() + 9 + f.size() <= MaxRestoreBytes, "restore too large");
        b.push_back('F');
        put(b, static_cast<std::uint32_t>(i));
        put(b, static_cast<std::uint32_t>(f.size()));
        b.insert(b.end(), f.begin(), f.end());
    }
    need(!r.prompt.empty() && r.prompt.size() <= MaxPayload, "invalid prompt size");
    need(b.size() + 5 + r.prompt.size() + 32 <= MaxRestoreBytes, "restore too large");
    b.push_back('P');
    put(b, static_cast<std::uint32_t>(r.prompt.size()));
    b.insert(b.end(), r.prompt.begin(), r.prompt.end());
    return b;
}
class Projector {
    ygo::ClientField &f;
    PlayerViewState &s;
    unsigned recipient;
    bool initialized{};
    unsigned created{};
    unsigned pendingHint{};
    unsigned player(Reader &r) {
        unsigned p = r.u8();
        need(p < 2, "invalid player");
        return p ^ recipient;
    }
    std::vector<ygo::ClientCard *> &zone(unsigned p, unsigned l) {
        need(p < 2, "invalid controller");
        switch (l) {
        case LOCATION_DECK:
            return f.deck[p];
        case LOCATION_HAND:
            return f.hand[p];
        case LOCATION_MZONE:
            return f.mzone[p];
        case LOCATION_SZONE:
            return f.szone[p];
        case LOCATION_GRAVE:
            return f.grave[p];
        case LOCATION_REMOVED:
            return f.remove[p];
        case LOCATION_EXTRA:
            return f.extra[p];
        default:
            throw std::runtime_error("invalid zone");
        }
    }
    ygo::ClientCard *card(unsigned p, unsigned l, unsigned seq, unsigned sub = 0) {
        auto &z = zone(p, l & 0x7f);
        need(seq < z.size() && z[seq], "missing card");
        auto c = z[seq];
        if (l & LOCATION_OVERLAY) {
            need(sub < c->overlayed.size(), "missing overlay");
            c = c->overlayed[sub];
        }
        return c;
    }
    ygo::ClientCard *ref(Reader &r) {
        auto p = player(r);
        auto l = r.u8();
        auto q = r.u8();
        auto ss = r.u8();
        return card(p, l, q, ss);
    }
    void reseq(std::vector<ygo::ClientCard *> &z) {
        for (unsigned i = 0; i < z.size(); ++i)
            if (z[i])
                z[i]->sequence = static_cast<unsigned char>(i);
    }
    ygo::ClientCard *make() {
        need(++created <= 4096, "too many visible cards");
        return f.CreateCard();
    }
    void attach(ygo::ClientCard *c, unsigned p, unsigned l, unsigned seq, unsigned pos) {
        c->controler = p;
        c->position = pos;
        c->location = l;
        if (l & LOCATION_OVERLAY) {
            auto target = card(p, l & 0x7f, seq);
            c->location = LOCATION_OVERLAY;
            c->overlayTarget = target;
            c->sequence = target->overlayed.size();
            need(c->sequence < 255, "overlay limit");
            target->overlayed.push_back(c);
            f.overlay_cards.insert(c);
            return;
        }
        auto &z = zone(p, l);
        c->sequence = seq;
        if (l & LOCATION_ONFIELD) {
            need(seq < z.size() && !z[seq], "occupied field slot");
            z[seq] = c;
        } else {
            need(z.size() < 255, "zone limit");
            if (l == LOCATION_DECK && seq == 0)
                z.insert(z.begin(), c);
            else if (l == LOCATION_EXTRA && !(pos & POS_FACEUP))
                z.insert(z.end() - f.extra_p_count[p], c);
            else
                z.push_back(c);
            reseq(z);
        }
        if (l == LOCATION_EXTRA && (pos & POS_FACEUP))
            ++f.extra_p_count[p];
    }
    ygo::ClientCard *add(unsigned p, unsigned l, unsigned seq, unsigned pos = POS_FACEDOWN_DEFENSE) {
        auto c = make();
        c->owner = p;
        attach(c, p, l, seq, pos);
        return c;
    }
    void detach(ygo::ClientCard *c) {
        if (c->overlayTarget) {
            auto &z = c->overlayTarget->overlayed;
            z.erase(std::find(z.begin(), z.end(), c));
            reseq(z);
            c->overlayTarget = nullptr;
            f.overlay_cards.erase(c);
        } else {
            auto &z = zone(c->controler, c->location);
            if (c->location & LOCATION_ONFIELD)
                z[c->sequence] = nullptr;
            else {
                z.erase(z.begin() + c->sequence);
                reseq(z);
            }
            if (c->location == LOCATION_EXTRA && (c->position & POS_FACEUP))
                --f.extra_p_count[c->controler];
        }
    }
    void query(ygo::ClientCard *c, Reader &r) {
        unsigned flags = r.u32();
        need(!(flags & ~0xefffffU), "unknown query flags");
        if (!flags) {
            while (r.p < r.end)
                need(r.u8() == 0, "nonzero hidden query");
            if (c)
                c->ClearData();
            return;
        }
        need(c != nullptr, "query for empty zone");
        auto scalar = [&](unsigned bit, auto &value) {
            if (flags & bit)
                value = r.u32();
        };
        if (flags & QUERY_CODE) {
            auto code = r.u32();
            if (!code)
                c->ClearData();
            c->code = code;
        }
        if (flags & QUERY_POSITION) {
            auto pos = r.u32();
            c->position = pos >> 24;
        }
        scalar(QUERY_ALIAS, c->alias);
        scalar(QUERY_TYPE, c->type);
        scalar(QUERY_LEVEL, c->level);
        scalar(QUERY_RANK, c->rank);
        scalar(QUERY_ATTRIBUTE, c->attribute);
        scalar(QUERY_RACE, c->race);
        scalar(QUERY_ATTACK, c->attack);
        scalar(QUERY_DEFENSE, c->defense);
        scalar(QUERY_BASE_ATTACK, c->base_attack);
        scalar(QUERY_BASE_DEFENSE, c->base_defense);
        scalar(QUERY_REASON, c->reason);
        if (flags & QUERY_REASON_CARD)
            r.u32();
        if (flags & QUERY_EQUIP_CARD) {
            auto target = ref(r);
            if (c->equipTarget)
                c->equipTarget->equipped.erase(c);
            c->equipTarget = target;
            target->equipped.insert(c);
        }
        if (flags & QUERY_TARGET_CARD) {
            auto n = r.u32();
            need(n <= 4096, "target count");
            for (auto target : c->cardTarget)
                target->ownerTarget.erase(c);
            c->cardTarget.clear();
            for (unsigned i = 0; i < n; ++i) {
                auto target = ref(r);
                c->cardTarget.insert(target);
                target->ownerTarget.insert(c);
            }
        }
        if (flags & QUERY_OVERLAY_CARD) {
            auto n = r.u32();
            need(n <= 255, "overlay count");
            while (c->overlayed.size() > n)
                f.DestroyCard(c->overlayed.back());
            while (c->overlayed.size() < n) {
                auto x = make();
                x->owner = c->controler;
                attach(x, c->controler, c->location | LOCATION_OVERLAY, c->sequence, 0);
            }
            for (auto x : c->overlayed)
                x->code = r.u32();
        }
        if (flags & QUERY_COUNTERS) {
            auto n = r.u32();
            need(n <= 4096, "counter count");
            c->counters.clear();
            for (unsigned i = 0; i < n; ++i) {
                auto type = r.u16();
                auto count = r.u16();
                c->counters[type] = count;
            }
        }
        if (flags & QUERY_OWNER) {
            auto owner = r.u32();
            need(owner < 2, "query owner");
            c->owner = owner ^ recipient;
        }
        scalar(QUERY_STATUS, c->status);
        scalar(QUERY_LSCALE, c->lscale);
        scalar(QUERY_RSCALE, c->rscale);
        scalar(QUERY_LINK, c->link);
        if (flags & QUERY_LINK)
            c->link_marker = r.u32();
        // ClientCard display strings are prepared without legacy SetCode/UpdateInfo animation calls.
        if (flags & QUERY_ATTACK) {
            if (c->attack < 0)
                myswprintf(c->atkstring, L"?");
            else
                myswprintf(c->atkstring, L"%d", c->attack);
        }
        if (flags & QUERY_DEFENSE) {
            if (c->type & TYPE_LINK)
                myswprintf(c->defstring, L"-");
            else if (c->defense < 0)
                myswprintf(c->defstring, L"?");
            else
                myswprintf(c->defstring, L"%d", c->defense);
        }
        if (flags & QUERY_LEVEL)
            myswprintf(c->lvstring, L"L%d", c->level);
        if ((flags & QUERY_RANK) && c->rank)
            myswprintf(c->lvstring, L"R%d", c->rank);
        if (flags & QUERY_LSCALE)
            myswprintf(c->lscstring, L"%d", c->lscale);
        if (flags & QUERY_RSCALE)
            myswprintf(c->rscstring, L"%d", c->rscale);
        if (flags & QUERY_LINK)
            myswprintf(c->linkstring, L"L\x2012%d", c->link);
        r.done();
    }
    void queryRecord(ygo::ClientCard *c, Reader &r) {
        auto n = r.u32();
        need(n >= 4 && n % 4 == 0 && n - 4 <= r.end - r.p, "query length");
        if (n > LEN_HEADER) {
            Reader q(r.b, r.p, r.p + n - 4);
            query(c, q);
        }
        r.skip(n - 4);
    }
    ygo::ChainInfo chain(Reader &r) {
        ygo::ChainInfo c;
        c.code = r.u32();
        c.chain_card = ref(r);
        c.controler = player(r);
        c.location = r.u8();
        c.sequence = r.u8();
        zone(c.controler, c.location & 0x7f);
        need(c.location != LOCATION_MZONE || c.sequence < 7, "chain monster slot");
        need(c.location != LOCATION_SZONE || c.sequence < 8, "chain spell slot");
        c.desc = r.u32();
        c.chain_card->code = c.code;
        return c;
    }
    void clearPrompt() {
        auto clear = [](ygo::ClientCard *c) {
            if (c) {
                c->is_selectable = false;
                c->is_selected = false;
                c->cmdFlag = 0;
                c->select_seq = 0;
                c->opParam = 0;
            }
        };
        for (unsigned p = 0; p < 2; ++p)
            for (auto l : {LOCATION_DECK, LOCATION_HAND, LOCATION_MZONE, LOCATION_SZONE, LOCATION_GRAVE,
                           LOCATION_REMOVED, LOCATION_EXTRA})
                for (auto c : zone(p, l))
                    clear(c);
        for (auto c : f.overlay_cards)
            clear(c);
        f.summonable_cards.clear();
        f.spsummonable_cards.clear();
        f.reposable_cards.clear();
        f.msetable_cards.clear();
        f.ssetable_cards.clear();
        f.activatable_cards.clear();
        f.attackable_cards.clear();
        f.conti_cards.clear();
        f.activatable_descs.clear();
        f.select_options.clear();
        f.selectable_cards.clear();
        f.selected_cards.clear();
        f.selectsum_all.clear();
        f.selectsum_cards.clear();
        f.sort_list.clear();
        f.declare_opcodes.clear();
        for (unsigned p = 0; p < 2; ++p) {
            f.deck_act[p] = f.grave_act[p] = f.remove_act[p] = f.extra_act[p] = f.pzone_act[p] = false;
        }
        f.conti_act = false;
        f.chain_forced = false;
        f.select_min = 0;
        f.select_max = 0;
        f.select_cancelable = false;
        f.select_ready = false;
        f.select_hint = 0;
        f.selectable_field = 0;
        f.selected_field = 0;
        f.select_panalmode = false;
        f.must_select_count = 0;
        f.select_curval_l = 0;
        f.select_curval_h = 0;
        f.select_sumval = 0;
        f.select_mode = 0;
        f.announce_count = 0;
        f.select_counter_count = 0;
        f.select_counter_type = 0;
    }

  public:
    Projector(ygo::ClientField &field, PlayerViewState &state, unsigned p)
        : f(field), s(state), recipient(p) {}
    void frame(const Bytes &bytes) {
        Reader r(bytes);
        auto msg = r.u8();
        need(initialized || msg == MSG_START || msg == MSG_RELOAD_FIELD,
             "restore must start with initial state");
        if ((msg >= MSG_SELECT_BATTLECMD && msg <= MSG_SELECT_UNSELECT_CARD) ||
            msg == MSG_ROCK_PAPER_SCISSORS || (msg >= MSG_ANNOUNCE_RACE && msg <= MSG_ANNOUNCE_NUMBER)) {
            prompt(bytes);
            clearPrompt();
            return;
        }
        switch (msg) {
        case MSG_HINT: {
            auto type = r.u8();
            player(r);
            auto value = r.u32();
            need(type >= HINT_EVENT && type <= HINT_ZONE, "unknown hint");
            if (type == HINT_SELECTMSG)
                pendingHint = value;
            break;
        }
        case MSG_CARD_HINT: {
            auto c = ref(r);
            auto type = r.u8();
            auto value = r.u32();
            if (type == CHINT_DESC_ADD)
                ++c->desc_hints[value];
            else if (type == CHINT_DESC_REMOVE) {
                auto it = c->desc_hints.find(value);
                need(it != c->desc_hints.end(), "missing card hint");
                if (!--it->second)
                    c->desc_hints.erase(it);
            } else {
                c->cHint = type;
                c->chValue = value;
            }
            break;
        }
        case MSG_PLAYER_HINT: {
            auto p = player(r);
            auto type = r.u8();
            auto value = r.u32();
            need(type == PHINT_DESC_ADD || type == PHINT_DESC_REMOVE, "unknown player hint");
            if (value == CARD_QUESTION && p == 0)
                f.cant_check_grave = type == PHINT_DESC_ADD;
            else if (type == PHINT_DESC_ADD)
                ++f.player_desc_hints[p][value];
            else {
                auto it = f.player_desc_hints[p].find(value);
                need(it != f.player_desc_hints[p].end(), "missing player hint");
                if (!--it->second)
                    f.player_desc_hints[p].erase(it);
            }
            break;
        }
        case MSG_START: {
            need(!initialized, "duplicate initial frame");
            initialized = true;
            need(r.u8() == recipient, "start recipient mismatch");
            s.duelRule = r.u8();
            for (unsigned p = 0; p < 2; ++p)
                s.lp[p ^ recipient] = r.u32();
            for (unsigned p = 0; p < 2; ++p) {
                auto n = r.u16(), e = r.u16();
                need(n <= 255 && e <= 255, "start counts");
                for (unsigned i = 0; i < n; ++i)
                    add(p ^ recipient, LOCATION_DECK, i);
                for (unsigned i = 0; i < e; ++i)
                    add(p ^ recipient, LOCATION_EXTRA, i);
            }
            break;
        }
        case MSG_RELOAD_FIELD: {
            if (initialized) {
                ygo::ClientField empty;
                f.SwapPreparedModel(empty);
                created = 0;
            }
            initialized = true;
            s.duelRule = r.u8();
            for (unsigned wire = 0; wire < 2; ++wire) {
                auto p = wire ^ recipient;
                s.lp[p] = r.u32();
                for (unsigned i = 0; i < 7; ++i) {
                    auto exists = r.u8();
                    need(exists < 2, "monster presence");
                    if (exists) {
                        auto pos = r.u8();
                        add(p, LOCATION_MZONE, i, pos);
                        auto n = r.u8();
                        for (unsigned j = 0; j < n; ++j)
                            add(p, LOCATION_MZONE | LOCATION_OVERLAY, i, 0);
                    }
                }
                for (unsigned i = 0; i < 8; ++i) {
                    auto exists = r.u8();
                    need(exists < 2, "spell presence");
                    if (exists)
                        add(p, LOCATION_SZONE, i, r.u8());
                }
                for (auto l :
                     {LOCATION_DECK, LOCATION_HAND, LOCATION_GRAVE, LOCATION_REMOVED, LOCATION_EXTRA}) {
                    auto n = r.u8();
                    for (unsigned i = 0; i < n; ++i)
                        add(p, l, i);
                }
                auto faceup = r.u8();
                need(faceup <= f.extra[p].size(), "extra faceup count");
                f.extra_p_count[p] = faceup;
                for (unsigned i = 0; i < faceup; ++i)
                    f.extra[p][f.extra[p].size() - 1 - i]->position = POS_FACEUP_ATTACK;
            }
            auto n = r.u8();
            for (unsigned i = 0; i < n; ++i) {
                f.current_chain = chain(r);
                f.chains.push_back(f.current_chain);
            }
            f.last_chain = n != 0;
            break;
        }
        case MSG_UPDATE_DATA: {
            auto p = player(r);
            auto l = r.u8();
            auto &z = zone(p, l);
            for (auto c : z)
                queryRecord(c, r);
            break;
        }
        case MSG_UPDATE_CARD: {
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            auto &z = zone(p, l);
            need(seq < z.size(), "query sequence");
            queryRecord(z[seq], r);
            break;
        }
        case MSG_MOVE: {
            auto code = r.u32();
            auto pp = player(r);
            auto pl = r.u8();
            auto ps = r.u8();
            auto sub = r.u8();
            auto cp = player(r);
            auto cl = r.u8();
            auto cs = r.u8();
            auto pos = r.u8();
            auto reason = r.u32();
            need(pl || cl, "empty move");
            auto c = pl ? card(pp, pl, ps, sub) : make();
            if (!pl)
                c->owner = cp;
            else
                detach(c);
            if (!cl) {
                need(c->overlayed.empty(), "destroy with attached overlays");
                f.DestroyCard(c);
                break;
            }
            if (!(pl & LOCATION_OVERLAY) && pl != cl) {
                c->ClearTarget();
                if ((pl & LOCATION_ONFIELD) || (cl & LOCATION_OVERLAY))
                    c->counters.clear();
                if (c->equipTarget)
                    c->equipTarget->equipped.erase(c);
                c->equipTarget = nullptr;
            }
            attach(c, cp, cl, cs, pos);
            if (!pl || (!(pl & LOCATION_OVERLAY) && (code || cl == LOCATION_EXTRA)))
                c->code = code;
            c->cHint = 0;
            c->chValue = 0;
            c->reason = reason;
            if (cl == LOCATION_DECK)
                c->ClearData();
            for (auto x : c->overlayed)
                x->controler = cp;
            break;
        }
        case MSG_DRAW: {
            auto p = player(r);
            auto n = r.u8();
            need(n <= f.deck[p].size(), "draw count");
            for (unsigned i = 0; i < n; ++i) {
                auto code = r.u32();
                auto c = f.deck[p].back();
                detach(c);
                if (!f.deck_reversed || code)
                    c->code = code & 0x7fffffff;
                attach(c, p, LOCATION_HAND, f.hand[p].size(), 0);
            }
            break;
        }
        case MSG_NEW_TURN:
            s.turnPlayer = player(r);
            need(s.turn < std::numeric_limits<unsigned>::max(), "turn overflow");
            ++s.turn;
            break;
        case MSG_NEW_PHASE:
            s.phase = r.u16();
            break;
        case MSG_DAMAGE:
        case MSG_PAY_LPCOST:
        case MSG_RECOVER:
        case MSG_LPUPDATE: {
            auto p = player(r);
            auto n = r.u32();
            need(n <= INT32_MAX, "invalid lp");
            auto value = msg == MSG_LPUPDATE ? std::int64_t(n)
                                             : std::int64_t(s.lp[p]) +
                                                   (msg == MSG_RECOVER ? std::int64_t(n) : -std::int64_t(n));
            need(value <= INT32_MAX, "lp overflow");
            s.lp[p] = std::max<std::int64_t>(0, value);
            break;
        }
        case MSG_POS_CHANGE: {
            auto code = r.u32();
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            r.u8();
            auto pos = r.u8();
            auto c = card(p, l, seq);
            if (code)
                c->code = code;
            c->position = pos;
            if (pos & POS_FACEDOWN) {
                c->ClearTarget();
                c->counters.clear();
            }
            break;
        }
        case MSG_SET: {
            auto code = r.u32();
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            auto pos = r.u8();
            (void)code;
            (void)pos;
            card(p, l, seq);
            break;
        }
        case MSG_ATTACK: {
            f.attacker = ref(r);
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            auto sub = r.u8();
            f.attack_target = l ? card(p, l, seq, sub) : nullptr;
            break;
        }
        case MSG_BATTLE: {
            for (unsigned i = 0; i < 2; ++i) {
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                auto sub = r.u8();
                auto atk = r.u32();
                auto def = r.u32();
                r.u8();
                if (l) {
                    auto c = card(p, l, seq, sub);
                    c->attack = static_cast<int32_t>(atk);
                    c->defense = static_cast<int32_t>(def);
                    myswprintf(c->atkstring, L"%d", c->attack);
                    myswprintf(c->defstring, L"%d", c->defense);
                } else
                    need(i == 1, "missing battle attacker");
            }
            break;
        }
        case MSG_SWAP: {
            r.u32();
            auto a = ref(r);
            r.u32();
            auto b = ref(r);
            need(a != b, "self swap");
            auto ap = a->controler, al = a->location, as = a->sequence, bp = b->controler, bl = b->location,
                 bs = b->sequence;
            need((al & LOCATION_ONFIELD) && (bl & LOCATION_ONFIELD), "swap requires field cards");
            detach(a);
            detach(b);
            attach(a, bp, bl, bs, a->position);
            attach(b, ap, al, as, b->position);
            for (auto x : a->overlayed)
                x->controler = bp;
            for (auto x : b->overlayed)
                x->controler = ap;
            break;
        }
        case MSG_SHUFFLE_SET_CARD: {
            auto loc = r.u8();
            need(loc == LOCATION_MZONE || loc == LOCATION_SZONE, "shuffle field zone");
            auto n = r.u8();
            need(n <= 8, "shuffle field count");
            std::vector<ygo::ClientCard *> cards;
            std::set<ygo::ClientCard *> unique;
            for (unsigned i = 0; i < n; ++i) {
                auto c = ref(r);
                need(c->location == loc && unique.insert(c).second, "shuffle field source");
                c->code = 0;
                cards.push_back(c);
            }
            for (auto c : cards) {
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                r.u8();
                if (l) {
                    need(l == loc && c->controler == p, "shuffle field destination");
                    auto other = card(p, l, seq);
                    auto old = c->sequence;
                    zone(p, l)[old] = other;
                    zone(p, l)[seq] = c;
                    c->sequence = seq;
                    other->sequence = old;
                }
            }
            break;
        }
        case MSG_SWAP_GRAVE_DECK: {
            auto p = player(r);
            auto &deck = f.deck[p];
            auto &grave = f.grave[p];
            deck.swap(grave);
            for (auto c : grave)
                c->location = LOCATION_GRAVE;
            for (std::size_t i = 0; i < deck.size();) {
                auto c = deck[i];
                if (c->type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK)) {
                    deck.erase(deck.begin() + i);
                    attach(c, p, LOCATION_EXTRA, 0, POS_FACEDOWN);
                } else {
                    c->location = LOCATION_DECK;
                    ++i;
                }
            }
            reseq(deck);
            reseq(grave);
            break;
        }
        case MSG_FIELD_DISABLED:
            f.disabled_field = r.u32();
            if (recipient)
                f.disabled_field = (f.disabled_field << 16) | (f.disabled_field >> 16);
            break;
        case MSG_CHAINING:
            f.current_chain = chain(r);
            need(r.u8() == f.chains.size() + 1, "chain order");
            break;
        case MSG_CHAINED:
            need(r.u8() == f.chains.size() + 1 && f.current_chain.chain_card, "chain order");
            f.chains.push_back(f.current_chain);
            f.last_chain = true;
            break;
        case MSG_CHAIN_SOLVING:
        case MSG_CHAIN_SOLVED:
        case MSG_CHAIN_NEGATED:
        case MSG_CHAIN_DISABLED: {
            auto n = r.u8();
            need(n && n <= f.chains.size(), "chain index");
            if (msg == MSG_CHAIN_SOLVING || msg == MSG_CHAIN_SOLVED) {
                f.chains[n - 1].solved = true;
                f.last_chain = false;
            }
            break;
        }
        case MSG_CHAIN_END:
            f.chains.clear();
            f.current_chain = {};
            f.last_chain = false;
            break;
        case MSG_BECOME_TARGET: {
            auto n = r.u8();
            need(f.current_chain.chain_card, "target outside chain");
            for (unsigned i = 0; i < n; ++i)
                f.current_chain.target.insert(ref(r));
            break;
        }
        case MSG_EQUIP:
        case MSG_CARD_TARGET:
        case MSG_CANCEL_TARGET: {
            auto a = ref(r), b = ref(r);
            if (msg == MSG_EQUIP) {
                if (a->equipTarget)
                    a->equipTarget->equipped.erase(a);
                a->equipTarget = b;
                b->equipped.insert(a);
            } else if (msg == MSG_CARD_TARGET) {
                a->cardTarget.insert(b);
                b->ownerTarget.insert(a);
            } else {
                a->cardTarget.erase(b);
                b->ownerTarget.erase(a);
            }
            break;
        }
        case MSG_UNEQUIP: {
            auto c = ref(r);
            if (c->equipTarget)
                c->equipTarget->equipped.erase(c);
            c->equipTarget = nullptr;
            break;
        }
        case MSG_ADD_COUNTER:
        case MSG_REMOVE_COUNTER: {
            auto type = r.u16();
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            auto n = r.u16();
            auto c = card(p, l, seq);
            auto &count = c->counters[type];
            if (msg == MSG_ADD_COUNTER) {
                need(count <= INT32_MAX - n, "counter overflow");
                count += n;
            } else {
                need(count >= n, "counter underflow");
                count -= n;
                if (!count)
                    c->counters.erase(type);
            }
            break;
        }
        case MSG_SHUFFLE_DECK: {
            auto p = player(r);
            for (auto c : f.deck[p]) {
                c->code = 0;
                c->is_reversed = false;
            }
            break;
        }
        case MSG_SHUFFLE_HAND:
        case MSG_SHUFFLE_EXTRA: {
            auto p = player(r);
            auto n = r.u8();
            auto &z = msg == MSG_SHUFFLE_HAND ? f.hand[p] : f.extra[p];
            need(n == z.size() - (msg == MSG_SHUFFLE_EXTRA ? f.extra_p_count[p] : 0), "shuffle count");
            for (unsigned i = 0; i < n; ++i)
                z[i]->code = r.u32();
            break;
        }
        case MSG_REVERSE_DECK:
            f.deck_reversed = !f.deck_reversed;
            break;
        case MSG_DECK_TOP: {
            auto p = player(r);
            auto seq = r.u8();
            auto code = r.u32();
            need(seq < f.deck[p].size(), "deck top");
            auto c = f.deck[p][f.deck[p].size() - 1 - seq];
            c->code = code & 0x7fffffff;
            c->is_reversed = (code >> 31) != 0;
            break;
        }
        case MSG_CONFIRM_DECKTOP:
        case MSG_CONFIRM_EXTRATOP:
        case MSG_CONFIRM_CARDS: {
            auto p = player(r);
            if (msg == MSG_CONFIRM_CARDS)
                need(r.u8() < 2, "confirm panel flag");
            auto n = r.u8();
            for (unsigned i = 0; i < n; ++i) {
                auto code = r.u32();
                auto cp = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                ygo::ClientCard *c;
                if (msg == MSG_CONFIRM_CARDS)
                    c = card(cp, l, seq);
                else {
                    auto &z = msg == MSG_CONFIRM_DECKTOP ? f.deck[p] : f.extra[p];
                    const auto faceup = msg == MSG_CONFIRM_EXTRATOP ? f.extra_p_count[p] : 0;
                    need(faceup <= z.size() && i < z.size() - faceup, "confirm count");
                    c = z[z.size() - 1 - faceup - i];
                }
                if (code)
                    c->code = code;
            }
            break;
        }
        // Baseline cosmetic-only handlers: validate their exact shape, retain no animation.
        case MSG_SUMMONING:
        case MSG_SPSUMMONING:
        case MSG_FLIPSUMMONING: {
            auto code = r.u32();
            auto p = player(r);
            auto l = r.u8();
            auto seq = r.u8();
            r.u8();
            card(p, l, seq)->code = code;
            break;
        }
        case MSG_SUMMONED:
        case MSG_SPSUMMONED:
        case MSG_FLIPSUMMONED:
        case MSG_DAMAGE_STEP_START:
        case MSG_DAMAGE_STEP_END:
        case MSG_ATTACK_DISABLED:
        case MSG_CARD_SELECTED:
            break;
        case MSG_TOSS_COIN:
        case MSG_TOSS_DICE: {
            player(r);
            auto n = r.u8();
            for (unsigned i = 0; i < n; ++i) {
                auto value = r.u8();
                need(msg == MSG_TOSS_COIN ? value <= 1 : (value >= 1 && value <= 6), "random display result");
            }
            break;
        }
        case MSG_RANDOM_SELECTED: {
            player(r);
            auto n = r.u8();
            for (unsigned i = 0; i < n; ++i)
                ref(r);
            break;
        }
        case MSG_MISSED_EFFECT:
            r.u32();
            r.u32();
            break;
        case MSG_HAND_RES:
            r.u8();
            break;
        case MSG_REFRESH_DECK:
            player(r);
            break;
        case MSG_WAITING:
            break;
        default:
            throw std::runtime_error("unsupported player frame " + std::to_string(msg));
        }
        r.done();
    }
    void prompt(const Bytes &bytes) {
        Reader r(bytes);
        auto msg = r.u8();
        if (msg == MSG_WAITING) {
            r.done();
            s.prompt = bytes;
            return;
        }
        if (msg == MSG_SELECT_SUM) {
            s.prompt = bytes;
            f.select_mode = r.u8();
            need(f.select_mode < 2, "sum mode");
        }
        need(player(r) == 0, "prompt recipient mismatch");
        f.select_hint = pendingHint;
        pendingHint = 0;
        switch (msg) {
        case MSG_SELECT_IDLECMD:
        case MSG_SELECT_BATTLECMD: {
            auto mark = [&](ygo::ClientCard *c) {
                auto p = c->controler;
                if (c->location == LOCATION_DECK)
                    f.deck_act[p] = true;
                if (c->location == LOCATION_GRAVE)
                    f.grave_act[p] = true;
                if (c->location == LOCATION_REMOVED)
                    f.remove_act[p] = true;
                if (c->location == LOCATION_EXTRA)
                    f.extra_act[p] = true;
            };
            auto list = [&](std::vector<ygo::ClientCard *> &out, unsigned flag) {
                auto n = r.u8();
                for (unsigned i = 0; i < n; ++i) {
                    auto code = r.u32();
                    auto p = player(r);
                    auto l = r.u8();
                    auto seq = r.u8();
                    auto c = card(p, l, seq);
                    out.push_back(c);
                    c->cmdFlag |= flag;
                    if (flag == COMMAND_SPSUMMON) {
                        if (l == LOCATION_DECK)
                            c->code = code;
                        mark(c);
                        const auto left = s.duelRule >= 4 ? 0 : 6;
                        if (l == LOCATION_SZONE && c->sequence == left && (c->type & TYPE_PENDULUM) &&
                            !c->equipTarget)
                            f.pzone_act[p] = true;
                    }
                    if (flag == COMMAND_ATTACK)
                        need(r.u8() < 2, "direct attack flag");
                }
            };
            if (msg == MSG_SELECT_IDLECMD) {
                list(f.summonable_cards, COMMAND_SUMMON);
                list(f.spsummonable_cards, COMMAND_SPSUMMON);
                list(f.reposable_cards, COMMAND_REPOS);
                list(f.msetable_cards, COMMAND_MSET);
                list(f.ssetable_cards, COMMAND_SSET);
            }
            auto n = r.u8();
            for (unsigned i = 0; i < n; ++i) {
                auto code = r.u32();
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                auto desc = r.u32();
                auto c = card(p, l, seq);
                f.activatable_cards.push_back(c);
                auto flag = (code & 0x80000000) ? EDESC_OPERATION : 0;
                f.activatable_descs.emplace_back(desc, flag);
                if (flag) {
                    c->chain_code = code & 0x7fffffff;
                    f.conti_cards.push_back(c);
                    f.conti_act = true;
                } else {
                    c->cmdFlag |= COMMAND_ACTIVATE;
                    mark(c);
                }
            }
            if (msg == MSG_SELECT_BATTLECMD)
                list(f.attackable_cards, COMMAND_ATTACK);
            need(r.u8() < 2 && r.u8() < 2, "phase command flags");
            if (msg == MSG_SELECT_IDLECMD)
                need(r.u8() < 2, "shuffle command flag");
            break;
        }
        case MSG_SELECT_CHAIN: {
            auto n = r.u8();
            r.u8();
            r.u32();
            r.u32();
            for (unsigned i = 0; i < n; ++i) {
                auto flag = r.u8();
                auto forced = r.u8();
                need(forced < 2, "chain forced flag");
                auto code = r.u32();
                auto c = ref(r);
                auto desc = r.u32();
                f.activatable_cards.push_back(c);
                f.activatable_descs.emplace_back(desc, flag | (forced << 8));
                f.chain_forced |= forced != 0;
                if (flag & EDESC_OPERATION) {
                    c->chain_code = code;
                    f.conti_cards.push_back(c);
                    f.conti_act = true;
                } else {
                    c->is_selectable = true;
                    c->cmdFlag |= (flag & EDESC_RESET) ? COMMAND_RESET : COMMAND_ACTIVATE;
                    const auto p = c->controler;
                    if (c->location == LOCATION_DECK) {
                        c->code = code;
                        f.deck_act[p] = true;
                    } else if (c->location == LOCATION_GRAVE)
                        f.grave_act[p] = true;
                    else if (c->location == LOCATION_REMOVED)
                        f.remove_act[p] = true;
                    else if (c->location == LOCATION_EXTRA)
                        f.extra_act[p] = true;
                }
            }
            break;
        }
        case MSG_SELECT_SUM: {
            auto sum = r.u32();
            need(sum <= INT32_MAX / 2, "sum target");
            f.select_sumval = sum;
            f.select_min = r.u8();
            f.select_max = r.u8();
            need(f.select_min <= f.select_max, "sum range");
            auto mandatory = r.u8();
            f.must_select_count = mandatory;
            std::set<ygo::ClientCard *> unique;
            std::uint64_t totalWeights = 0;
            auto list = [&](unsigned n, bool required) {
                for (unsigned i = 0; i < n; ++i) {
                    auto code = r.u32();
                    auto p = player(r);
                    auto l = r.u8();
                    auto seq = r.u8();
                    auto c = card(p, l, seq);
                    need(unique.insert(c).second, "duplicate sum card");
                    if (code)
                        c->code = code;
                    c->opParam = r.u32();
                    totalWeights += (c->opParam & 0x80000000)
                                        ? (c->opParam & 0x7fffffff)
                                        : std::max(c->opParam & 0xffff, c->opParam >> 16);
                    need(totalWeights <= INT32_MAX / 2, "sum weights overflow");
                    c->select_seq = required ? 0 : i;
                    if (required)
                        f.selected_cards.push_back(c);
                    else
                        f.selectsum_all.push_back(c);
                    if (!(l & 0xe))
                        f.select_panalmode = true;
                }
            };
            list(mandatory, true);
            auto n = r.u8();
            list(n, false);
            std::sort(f.selectsum_all.begin(), f.selectsum_all.end(), ygo::ClientCard::client_card_sort);
            f.PrepareSelectSum();
            break;
        }
        case MSG_SELECT_COUNTER: {
            f.select_counter_type = r.u16();
            f.select_counter_count = r.u16();
            auto n = r.u8();
            unsigned total = 0;
            std::set<ygo::ClientCard *> unique;
            for (unsigned i = 0; i < n; ++i) {
                r.u32();
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                auto count = r.u16();
                auto c = card(p, l, seq);
                need(unique.insert(c).second, "duplicate counter card");
                total += count;
                c->opParam = (count << 16) | count;
                c->is_selectable = true;
                f.selectable_cards.push_back(c);
            }
            need(total >= unsigned(f.select_counter_count), "counter selection total");
            break;
        }
        case MSG_SELECT_UNSELECT_CARD: {
            auto finish = r.u8();
            auto cancel = r.u8();
            need(finish < 2 && cancel < 2, "unselect flags");
            f.select_cancelable = finish || cancel;
            f.select_ready = finish;
            f.select_min = r.u8();
            f.select_max = r.u8();
            need(f.select_min <= f.select_max, "unselect range");
            std::set<ygo::ClientCard *> unique;
            unsigned index = 0;
            for (unsigned group = 0; group < 2; ++group) {
                auto n = r.u8();
                for (unsigned i = 0; i < n; ++i) {
                    auto code = r.u32();
                    auto c = ref(r);
                    need(unique.insert(c).second, "duplicate unselect card");
                    if (code)
                        c->code = code;
                    c->select_seq = index++;
                    c->is_selectable = true;
                    c->is_selected = group != 0;
                    f.selectable_cards.push_back(c);
                }
            }
            break;
        }
        case MSG_SORT_CARD: {
            auto n = r.u8();
            f.select_max = n;
            std::set<ygo::ClientCard *> unique;
            for (unsigned i = 0; i < n; ++i) {
                auto code = r.u32();
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                auto c = card(p, l, seq);
                need(unique.insert(c).second, "duplicate sort card");
                if (code)
                    c->code = code;
                f.selectable_cards.push_back(c);
                f.sort_list.push_back(0);
            }
            break;
        }
        case MSG_ANNOUNCE_RACE:
        case MSG_ANNOUNCE_ATTRIB: {
            f.announce_count = r.u8();
            auto available = r.u32();
            unsigned count = 0;
            for (unsigned bits = available; bits; bits >>= 1)
                count += bits & 1;
            need(f.announce_count > 0 && unsigned(f.announce_count) <= count, "announce mask");
            break;
        }
        case MSG_ANNOUNCE_NUMBER: {
            auto n = r.u8();
            need(n > 0, "announce number count");
            for (unsigned i = 0; i < n; ++i)
                f.select_options.push_back(r.u32());
            break;
        }
        case MSG_ANNOUNCE_CARD: {
            auto n = r.u8();
            need(n > 0, "announce expression count");
            unsigned depth = 0;
            for (unsigned i = 0; i < n; ++i) {
                auto op = r.u32();
                f.declare_opcodes.push_back(op);
                if (op >= OPCODE_ADD && op <= OPCODE_OR) {
                    need(depth >= 2, "announce binary stack");
                    --depth;
                } else if ((op >= OPCODE_NEG && op <= OPCODE_NOT) ||
                           (op >= OPCODE_ISCODE && op <= OPCODE_ISATTRIBUTE)) {
                    need(depth >= 1, "announce unary stack");
                } else
                    ++depth;
            }
            need(depth == 1, "announce expression stack");
            break;
        }
        case MSG_ROCK_PAPER_SCISSORS:
            break;
        case MSG_SELECT_YESNO:
            r.u32();
            break;
        case MSG_SELECT_EFFECTYN: {
            auto code = r.u32();
            auto c = ref(r);
            c->code = code;
            r.u32();
            break;
        }
        case MSG_SELECT_OPTION: {
            auto n = r.u8();
            need(n > 0, "empty options");
            for (unsigned i = 0; i < n; ++i)
                f.select_options.push_back(r.u32());
            break;
        }
        case MSG_SELECT_CARD:
        case MSG_SELECT_TRIBUTE: {
            auto cancel = r.u8();
            need(cancel < 2, "cancel flag");
            f.select_cancelable = cancel;
            f.select_min = r.u8();
            f.select_max = r.u8();
            auto n = r.u8();
            need(f.select_min <= f.select_max && (msg == MSG_SELECT_TRIBUTE || f.select_max <= n),
                 "selection range");
            std::set<ygo::ClientCard *> unique;
            for (unsigned i = 0; i < n; ++i) {
                auto code = r.u32();
                auto p = player(r);
                auto l = r.u8();
                auto seq = r.u8();
                auto sub = r.u8();
                auto c = card(p, l, seq, msg == MSG_SELECT_CARD ? sub : 0);
                need(unique.insert(c).second, "duplicate prompt card");
                if (code)
                    c->code = code;
                c->select_seq = i;
                c->is_selectable = true;
                if (msg == MSG_SELECT_TRIBUTE) {
                    need(sub > 0, "tribute weight");
                    c->opParam = (sub << 16) | 1;
                    f.selectsum_all.push_back(c);
                }
                f.selectable_cards.push_back(c);
            }
            if (msg == MSG_SELECT_TRIBUTE)
                f.PrepareSelectSum(true);
            else
                f.select_ready = f.select_min == 0;
            break;
        }
        case MSG_SELECT_POSITION:
            r.u32();
            need((r.u8() & 15) != 0, "empty position choice");
            break;
        case MSG_SELECT_PLACE:
        case MSG_SELECT_DISFIELD: {
            const auto count = r.u8();
            f.select_min = count ? count : 1;
            f.select_cancelable = count == 0;
            // The core encodes each mask relative to the selecting player.
            f.selectable_field = ~r.u32();
            break;
        }
        default:
            throw std::runtime_error("unsupported target prompt " + std::to_string(msg));
        }
        r.done();
        s.prompt = bytes;
    }
    void finish() {
        need(initialized, "missing initial frame");
        need(s.duelRule >= 1 && s.duelRule <= 5, "unsupported duel rule");
        f.PrepareModelGeometry(s.duelRule);
    }
};
} // namespace
Digest HashVisibleRestore(const PlayerRestore &r) { return Sha256(visible(r)); }
PlayerRestore BuildPlayerRestore(std::uint8_t p, const std::vector<Bytes> &history, const Bytes &prompt) {
    PlayerRestore r{p, history, prompt, {}};
    r.visibleDigest = HashVisibleRestore(r);
    return r;
}
Bytes EncodePlayerRestore(const PlayerRestore &r) {
    auto b = visible(r);
    need(Sha256(b) == r.visibleDigest, "visible digest mismatch");
    b.insert(b.end(), r.visibleDigest.begin(), r.visibleDigest.end());
    return b;
}
PlayerRestore DecodePlayerRestore(const Bytes &b) {
    need(b.size() <= MaxRestoreBytes, "restore too large");
    Reader r(b);
    need(r.bytes(5) == Bytes({'Y', 'G', 'O', 'V', 1}), "restore version");
    PlayerRestore out;
    out.player = r.u8();
    need(out.player < 2, "restore player");
    auto count = r.u32();
    need(count <= 65536 && count <= (r.end - r.p) / 9, "restore frame count");
    for (unsigned i = 0; i < count; ++i) {
        need(r.u8() == 'F' && r.u32() == i, "restore frame order");
        auto n = r.u32();
        need(n > 0, "empty restore frame");
        out.frames.push_back(r.bytes(n));
    }
    need(r.u8() == 'P', "prompt tag");
    auto n = r.u32();
    need(n <= MaxPayload, "prompt size");
    out.prompt = r.bytes(n);
    auto digest = r.bytes(32);
    std::copy(digest.begin(), digest.end(), out.visibleDigest.begin());
    r.done();
    need(HashVisibleRestore(out) == out.visibleDigest, "visible digest mismatch");
    return out;
}
PlayerRestoreAssembler::PlayerRestoreAssembler(const TxKey &k, std::uint8_t p) : key_(k), recipient_(p) {
    need(p < 2, "restore recipient");
}
bool PlayerRestoreAssembler::Add(const TxKey &k, std::uint8_t p, const Bytes &b) {
    if (failed_)
        return false;
    try {
        need(SameKey(k, key_) && p == recipient_, "restore fragment binding");
        fragments_.Add(b);
        return true;
    } catch (...) {
        failed_ = true;
        return false;
    }
}
PlayerRestore PlayerRestoreAssembler::Finish() const {
    need(!failed_, "failed restore assembler");
    auto r = DecodePlayerRestore(fragments_.Finish());
    need(r.player == recipient_, "restore recipient mismatch");
    return r;
}
ClientRestore::ClientRestore(ygo::ClientField &f, PlayerViewState &s, std::uint8_t p,
                             const SessionId &session, std::uint64_t epoch)
    : live_(f), state_(s), recipient_(p), session_(session), epoch_(epoch) {
    need(p < 2, "restore recipient");
}
bool ClientRestore::Prepare(const TxKey &k, const PlayerRestore &r, const Bytes &expected) {
    if (active_)
        return false;
    try {
        need(IsCurrent(k, session_, epoch_), "stale restore transaction");
        need(r.player == recipient_, "restore recipient mismatch");
        need(r.prompt == expected, "target prompt mismatch");
        need(HashVisibleRestore(r) == r.visibleDigest, "visible digest mismatch");
        auto candidate = std::make_unique<ygo::ClientField>();
        PlayerViewState state;
        Projector projector(*candidate, state, recipient_);
        for (const auto &frame : r.frames)
            projector.frame(frame);
        projector.prompt(r.prompt);
        projector.finish();
        candidate_ = std::move(candidate);
        preparedState_ = std::move(state);
        active_ = k;
        paused_ = true;
        committed_ = false;
        error_.clear();
        return true;
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
}
bool ClientRestore::Commit(const TxKey &k, std::uint64_t epoch) noexcept {
    if (!active_ || !SameKey(*active_, k) || committed_ || !candidate_ || epoch_ == UINT64_MAX ||
        epoch != epoch_ + 1)
        return false;
    live_.SwapPreparedModel(*candidate_);
    using std::swap;
    swap(state_, preparedState_);
    epoch_ = epoch;
    committed_ = true;
    return true;
}
bool ClientRestore::Resume(const TxKey &k) noexcept {
    if (!active_ || !SameKey(*active_, k) || !committed_)
        return false;
    candidate_.reset();
    active_.reset();
    paused_ = false;
    committed_ = false;
    return true;
}
void ClientRestore::Abort(const TxKey &k) noexcept {
    if (active_ && SameKey(*active_, k) && !committed_) {
        candidate_.reset();
        active_.reset();
        paused_ = false;
    }
}
bool ClientRestore::AcceptsGameplay(const SessionId &session, std::uint64_t epoch) const noexcept {
    return !paused_ && session_ == session && epoch_ == epoch;
}
} // namespace undo
