#include "undo_prompt.h"
#include "game.h"
#include "client_card.h"
#include <stdexcept>
#include <unordered_map>
namespace ygo {
namespace {
using Element = irr::gui::IGUIElement;
template<class T> std::shared_ptr<T> retain(T* p) {
    if(p) p->grab();
    return {p, [](T* value) { if(value) value->drop(); }};
}
struct Slot {
    Element* current{};
    void* address{};
    void (*assign)(void*, Element*) noexcept{};
};
template<class T> void slot(std::vector<Slot>& slots, T*& value) {
    slots.push_back({value, &value, [](void* p, Element* replacement) noexcept {
        *static_cast<T**>(p) = static_cast<T*>(replacement);
    }});
}
// One stable index for every Game pointer into the retained prompt roots.
std::vector<Slot> slots(Game& g) {
    std::vector<Slot> out;
#define W(name) slot(out, g.name)
    W(wQuery); W(stQMessage); W(btnYes); W(btnNo);
    W(wOptions); W(stOptions); W(btnOptionp); W(btnOptionn); W(btnOptionOK); W(scrOption);
    for(auto& p:g.btnOption) slot(out,p);
    W(wPosSelect); W(btnPSAU); W(btnPSAD); W(btnPSDU); W(btnPSDD);
    W(wCardSelect); W(scrCardList); W(btnSelectOK);
    for(auto& p:g.btnCardSelect) slot(out,p);
    for(auto& p:g.stCardPos) slot(out,p);
    W(wCardDisplay); W(scrDisplayList); W(btnDisplayOK);
    for(auto& p:g.btnCardDisplay) slot(out,p);
    for(auto& p:g.stDisplayPos) slot(out,p);
    W(wANNumber); W(cbANNumber); W(btnANNumberOK);
    for(auto& p:g.btnANNumber) slot(out,p);
    W(wANCard); W(ebANCard); W(lstANCard); W(btnANCardOK);
    W(wANAttribute); for(auto& p:g.chkAttribute) slot(out,p);
    W(wANRace); for(auto& p:g.chkRace) slot(out,p);
    W(wHand); for(auto& p:g.btnHand) slot(out,p);
    W(stHintMsg); W(btnBP); W(btnM2); W(btnEP); W(btnShuffle); W(btnCancelOrFinish);
    W(wCmdMenu); W(btnActivate); W(btnSummon); W(btnSPSummon); W(btnMSet);
    W(btnSSet); W(btnRepos); W(btnAttack); W(btnShowList); W(btnReset);
#undef W
    return out;
}
std::vector<Element*> roots(Game& g) {
    return {g.wQuery,g.wOptions,g.wPosSelect,g.wCardSelect,g.wCardDisplay,
        g.wANNumber,g.wANCard,g.wANAttribute,g.wANRace,g.wHand,g.stHintMsg,
        g.btnBP,g.btnM2,g.btnEP,g.btnShuffle,g.btnCancelOrFinish,g.wCmdMenu};
}
struct Node {
    std::string type;
    std::shared_ptr<irr::io::IAttributes> attributes;
    std::shared_ptr<irr::gui::IGUIFont> font;
    std::vector<std::pair<std::wstring,irr::u32>> combo;
    std::vector<Node> children;
    int slot{-1};
    bool sub{}, focused{};
};
Node capture(Game& g, Element* e, const std::vector<Slot>& bindings) {
    Node n;
    n.type=e->getTypeName(); n.sub=e->isSubElement(); n.focused=g.env->getFocus()==e;
    auto* attributes=g.env->getFileSystem()->createEmptyAttributes(g.driver);
    n.attributes={attributes,[](irr::io::IAttributes* p){p->drop();}};
    e->serializeAttributes(attributes);
    for(const auto& fading:g.fadingList) if(fading.guiFading==e) {
        attributes->setAttribute("Rect", fading.fadingSize);
        attributes->setAttribute("Visible", fading.isFadein);
    }
    for(size_t i=0;i<bindings.size();++i) if(bindings[i].current==e) n.slot=int(i);
    if(e->getType()==irr::gui::EGUIET_BUTTON)
        n.font=retain(static_cast<irr::gui::IGUIButton*>(e)->getOverrideFont());
    if(e->getType()==irr::gui::EGUIET_STATIC_TEXT)
        n.font=retain(static_cast<irr::gui::IGUIStaticText*>(e)->getOverrideFont());
    if(e->getType()==irr::gui::EGUIET_COMBO_BOX) {
        auto* c=static_cast<irr::gui::IGUIComboBox*>(e);
        for(irr::u32 i=0;i<c->getItemCount();++i) n.combo.emplace_back(c->getItem(i),c->getItemData(i));
    }
    for(auto* child:e->getChildren()) n.children.push_back(capture(g,child,bindings));
    return n;
}
// Card row identities are visible coordinates, not pointers into the old field.
struct CardRef { unsigned player{}, location{}, sequence{}, overlay{}; bool xyz{}; };
using VisibleCards=std::unordered_map<const ClientCard*,CardRef>;
VisibleCards visibleCards(const ClientField& f) {
    VisibleCards out;
    for(unsigned p=0;p<2;++p) {
        auto zone=[&](const std::vector<ClientCard*>& cards,unsigned location) {
            for(size_t seq=0;seq<cards.size();++seq) if(auto* c=cards[seq]) {
                out.emplace(c,CardRef{p,location,unsigned(seq),0,false});
                for(size_t sub=0;sub<c->overlayed.size();++sub)
                    out.emplace(c->overlayed[sub],CardRef{p,location,unsigned(seq),unsigned(sub),true});
            }
        };
        zone(f.deck[p],LOCATION_DECK); zone(f.hand[p],LOCATION_HAND); zone(f.mzone[p],LOCATION_MZONE);
        zone(f.szone[p],LOCATION_SZONE); zone(f.grave[p],LOCATION_GRAVE); zone(f.remove[p],LOCATION_REMOVED);
        zone(f.extra[p],LOCATION_EXTRA);
    }
    return out;
}
ClientCard* resolve(const ClientField& f,const CardRef& r) {
    if(r.player>1) throw std::runtime_error("Prompt card player");
    const std::vector<ClientCard*>* list=nullptr;
    switch(r.location) {
#define L(name,member) case name:list=&f.member[r.player];break
        L(LOCATION_DECK,deck); L(LOCATION_HAND,hand); L(LOCATION_MZONE,mzone);
        L(LOCATION_SZONE,szone); L(LOCATION_GRAVE,grave); L(LOCATION_REMOVED,remove); L(LOCATION_EXTRA,extra);
#undef L
        default:throw std::runtime_error("Prompt card location");
    }
    auto* c=list->at(r.sequence);
    if(!c) throw std::runtime_error("Prompt card absent from candidate");
    if(r.xyz) c=c->overlayed.at(r.overlay);
    if(!c) throw std::runtime_error("Prompt overlay absent from candidate");
    return c;
}
template<class Range> std::vector<CardRef> refs(const Range& range,const VisibleCards& live) {
    std::vector<CardRef> out;
    for(auto* c:range) { auto found=live.find(c); if(found!=live.end()) out.push_back(found->second); }
    return out;
}
struct CardFlags { CardRef ref; bool selectable{},selected{},highlighting{}; unsigned selectSequence{}; };
struct Rows {
    std::vector<CardFlags> flags;
    std::vector<int> ancard,optionsIndex,sort;
    std::vector<CardRef> selectable,selected,display,sumAll,sumCards;
    std::optional<CardRef> highlighting;
    size_t selectedOption{};
    int listCommand{},hint{};
    bool panelMode{},ready{},continuous{};
};
Rows captureRows(const ClientField& f) {
    Rows r;
    const auto live=visibleCards(f);
    r.ancard=f.ancard; r.optionsIndex=f.select_options_index; r.sort=f.sort_list;
    auto flags=[&](const auto& cards) {
        for(auto* c:cards) {
            // Legacy Clear leaves some inactive UI caches intact. Never
            // dereference their pointers unless still owned by a visible zone.
            auto found=live.find(c);
            if(found!=live.end()) r.flags.push_back({found->second,c->is_selectable,c->is_selected,c->is_highlighting,c->select_seq});
        }
    };
    flags(f.selectable_cards); flags(f.selected_cards); flags(f.selectsum_all); flags(f.selectsum_cards);
    r.selectable=refs(f.selectable_cards,live); r.selected=refs(f.selected_cards,live);
    r.display=refs(f.display_cards,live); r.sumAll=refs(f.selectsum_all,live); r.sumCards=refs(f.selectsum_cards,live);
    auto highlighted=live.find(f.highlighting_card);
    if(highlighted!=live.end()) r.highlighting=highlighted->second;
    r.selectedOption=f.selected_option; r.listCommand=f.list_command; r.hint=f.select_hint;
    r.panelMode=f.select_panalmode; r.ready=f.select_ready; r.continuous=f.select_continuous;
    return r;
}
struct PreparedRows {
    Rows values;
    std::vector<std::pair<ClientCard*,CardFlags>> flags;
    std::vector<ClientCard*> selectable,selected,display,sumAll;
    std::set<ClientCard*> sumCards;
    ClientCard* highlighting{};
    PreparedRows(const Rows& r,const ClientField& field):values(r) {
        for(const auto& flag:r.flags) flags.emplace_back(resolve(field,flag.ref),flag);
        auto list=[&](const std::vector<CardRef>& input,std::vector<ClientCard*>& out) {
            for(const auto& ref:input) out.push_back(resolve(field,ref));
        };
        list(r.selectable,selectable); list(r.selected,selected); list(r.display,display); list(r.sumAll,sumAll);
        for(const auto& ref:r.sumCards) sumCards.insert(resolve(field,ref));
        if(r.highlighting) highlighting=resolve(field,*r.highlighting);
    }
    void install(ClientField& f) noexcept {
        for(const auto& entry:flags) {
            auto* c=entry.first; const auto& flag=entry.second;
            c->is_selectable=flag.selectable; c->is_selected=flag.selected;
            c->is_highlighting=flag.highlighting; c->select_seq=flag.selectSequence;
        }
        f.ancard.swap(values.ancard); f.select_options_index.swap(values.optionsIndex); f.sort_list.swap(values.sort);
        f.selectable_cards.swap(selectable); f.selected_cards.swap(selected); f.display_cards.swap(display);
        f.selectsum_all.swap(sumAll); f.selectsum_cards.swap(sumCards);
        f.selected_option=values.selectedOption; f.list_command=values.listCommand; f.select_hint=values.hint;
        f.select_panalmode=values.panelMode; f.select_ready=values.ready; f.select_continuous=values.continuous;
        f.highlighting_card=highlighting;
        if(highlighting) highlighting->is_highlighting=true;
    }
};
using ImageMap=std::unordered_map<irr::gui::IGUIButton*,std::pair<int,bool>>;
using SavedImages=std::vector<std::pair<size_t,std::pair<int,bool>>>;
SavedImages captureImages(const ImageMap& images,const std::vector<Slot>& bindings) {
    SavedImages out;
    for(size_t i=0;i<bindings.size();++i) {
        if(bindings[i].current->getType()!=irr::gui::EGUIET_BUTTON) continue;
        auto found=images.find(static_cast<irr::gui::IGUIButton*>(bindings[i].current));
        if(found!=images.end()) out.emplace_back(i,found->second);
    }
    return out;
}
}
struct PromptSnapshot {
    std::vector<Node> roots;
    SavedImages pending, cards, facedown;
    int panelSlot{-1};
    Rows rows;
};
std::shared_ptr<const PromptSnapshot> CaptureUndoPrompt(Game& g) {
    auto result=std::make_shared<PromptSnapshot>();
    result->rows=captureRows(g.dField);
    auto bindings=slots(g);
    for(size_t i=0;i<bindings.size();++i)
        if(bindings[i].current==g.dField.panel) result->panelSlot=int(i);
    for(auto* root:roots(g)) result->roots.push_back(capture(g,root,bindings));
    result->pending=captureImages(g.btnImagePending,bindings);
    result->cards=captureImages(g.btnCardImgInfo,bindings);
    result->facedown=captureImages(g.btnFacedownImgInfo,bindings);
    return result;
}
namespace {
class Bank final : public PreparedPrompt {
public:
    Game& g;
    std::vector<Slot> bindings;
    std::vector<Element*> oldRoots, newRoots, replacements;
    std::vector<bool> visible;
    Element* focus{};
    Element* panel{};
    ImageMap pending,cards,facedown;
    std::unique_ptr<PreparedRows> rowState;
    bool installed{};
    explicit Bank(Game& game):g(game),bindings(slots(g)),oldRoots(roots(g)),replacements(bindings.size()) {
        newRoots.reserve(oldRoots.size()); visible.reserve(oldRoots.size());
    }
    ~Bank() override {
        // The old bank retains every old focus/child through installation.
        for(auto* e:installed?oldRoots:newRoots) e->remove();
    }
    void fill(const Node& n,Element* e) {
        e->deserializeAttributes(n.attributes.get());
        if(n.slot>=0) replacements.at(n.slot)=e;
        if(n.focused) focus=e;
        if(e->getType()==irr::gui::EGUIET_BUTTON)
            static_cast<irr::gui::IGUIButton*>(e)->setOverrideFont(n.font.get());
        if(e->getType()==irr::gui::EGUIET_STATIC_TEXT)
            static_cast<irr::gui::IGUIStaticText*>(e)->setOverrideFont(n.font.get());
        if(e->getType()==irr::gui::EGUIET_COMBO_BOX) {
            auto* c=static_cast<irr::gui::IGUIComboBox*>(e); c->clear();
            for(const auto& item:n.combo) c->addItem(item.first.c_str(),item.second);
            c->setSelected(n.attributes->getAttributeAsInt("Selected"));
        }
        std::vector<Element*> internal;
        for(auto* c:e->getChildren()) if(c->isSubElement()) internal.push_back(c);
        size_t index=0;
        for(const auto& child:n.children) {
            Element* c=nullptr;
            if(child.sub) {
                if(index>=internal.size()) throw std::runtime_error("Prompt internal widget shape changed");
                c=internal[index++];
                if(child.type!=c->getTypeName()) throw std::runtime_error("Prompt internal widget type changed");
            } else {
                c=g.env->addGUIElement(child.type.c_str(),e);
                if(!c) throw std::runtime_error("Cannot construct prompt widget");
            }
            fill(child,c);
        }
    }
    ImageMap remap(const ImageMap& current,const SavedImages& saved) {
        auto out=current;
        for(const auto& b:bindings) if(b.current->getType()==irr::gui::EGUIET_BUTTON)
            out.erase(static_cast<irr::gui::IGUIButton*>(b.current));
        for(const auto& image:saved)
            out[static_cast<irr::gui::IGUIButton*>(replacements.at(image.first))]=image.second;
        return out;
    }
    void prepare(const PromptSnapshot& snapshot,const ClientField& candidate) {
        rowState=std::make_unique<PreparedRows>(snapshot.rows,candidate);
        if(snapshot.roots.size()!=oldRoots.size()) throw std::runtime_error("Prompt root count changed");
        for(size_t i=0;i<oldRoots.size();++i) {
            const auto& node=snapshot.roots[i];
            auto* e=g.env->addGUIElement(node.type.c_str(),oldRoots[i]->getParent());
            if(!e) throw std::runtime_error("Cannot construct prompt root");
            newRoots.push_back(e); // reserved; destructor owns partial preparation
            fill(node,e); visible.push_back(e->isVisible()); e->setVisible(false);
        }
        for(auto* e:replacements) if(!e) throw std::runtime_error("Unmapped Game prompt pointer");
        if(snapshot.panelSlot>=0) panel=replacements.at(snapshot.panelSlot);
        pending=remap(g.btnImagePending,snapshot.pending);
        cards=remap(g.btnCardImgInfo,snapshot.cards);
        facedown=remap(g.btnFacedownImgInfo,snapshot.facedown);
    }
    void Install() noexcept override {
        for(auto* e:oldRoots) e->setVisible(false);
        for(size_t i=0;i<bindings.size();++i) bindings[i].assign(bindings[i].address,replacements[i]);
        g.btnImagePending.swap(pending); g.btnCardImgInfo.swap(cards); g.btnFacedownImgInfo.swap(facedown);
        rowState->install(g.dField);
        g.dField.panel=panel;
        g.env->installPreparedFocus(focus);
        for(size_t i=0;i<newRoots.size();++i) newRoots[i]->setVisible(visible[i]);
        installed=true;
    }
};
}
std::unique_ptr<PreparedPrompt> PrepareUndoPrompt(Game& g,const PromptSnapshot& snapshot,const ClientField* candidate) {
    auto bank=std::make_unique<Bank>(g);
    bank->prepare(snapshot,candidate?*candidate:g.dField);
    return bank;
}
}
