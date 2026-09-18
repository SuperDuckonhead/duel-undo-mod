#include "game.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "undo/deck_test_upload.h"
#include <algorithm>
#include <array>
#include <tuple>

namespace ygo {
namespace {
// Centralize the value-only editor state. References are used only to assign
// these values, never retained in a snapshot.
auto editorValues(DeckBuilder& e) {
	return std::tie(e.filter_effect, e.filter_type, e.filter_type2, e.filter_attrib, e.filter_race,
		e.filter_atktype, e.filter_atk, e.filter_deftype, e.filter_def, e.filter_lvtype, e.filter_lv,
		e.filter_scltype, e.filter_scl, e.filter_marks, e.filter_lm, e.mouse_pos,
		e.hovered_code, e.hovered_pos, e.hovered_seq, e.is_lastcard, e.click_pos,
		e.prev_category, e.prev_deck, e.prev_operation, e.prev_sel, e.is_modified,
		e.readonly, e.showing_pack, e.editorHistoryValid, e.rnd);
}
template<class... T> auto valueCopy(const std::tuple<T...>& values) {
	return std::apply([](const auto&... v) { return std::make_tuple(v...); }, values);
}
using EditorValues = decltype(valueCopy(editorValues(std::declval<DeckBuilder&>())));

std::shared_ptr<irr::gui::IGUIElement> retain(irr::gui::IGUIElement* element) {
	if(!element) return {};
	element->grab();
	return {element, [](irr::gui::IGUIElement* p) { p->drop(); }};
}
struct WidgetState {
	std::shared_ptr<irr::gui::IGUIElement> control;
	irr::gui::IGUIElement* parent;
	std::wstring text;
	bool enabled, visible, checked{};
	int selected{}, minimum{}, maximum{}, position{};
	std::vector<std::pair<std::wstring, irr::u32>> items;
	explicit WidgetState(irr::gui::IGUIElement* p)
		: control(retain(p)), parent(p->getParent()), text(p->getText() ? p->getText() : L""), enabled(p->isEnabled()), visible(p->isVisible()) {
		switch(p->getType()) {
		case irr::gui::EGUIET_COMBO_BOX: {
			auto c = static_cast<irr::gui::IGUIComboBox*>(p);
			selected = c->getSelected();
			for(irr::u32 i=0; i<c->getItemCount(); ++i) items.emplace_back(c->getItem(i), c->getItemData(i));
			break;
		}
		case irr::gui::EGUIET_SCROLL_BAR: {
			auto c = static_cast<irr::gui::IGUIScrollBar*>(p);
			minimum=c->getMin(); maximum=c->getMax(); position=c->getPos(); break;
		}
		case irr::gui::EGUIET_BUTTON: checked=static_cast<irr::gui::IGUIButton*>(p)->isPressed(); break;
		case irr::gui::EGUIET_CHECK_BOX: checked=static_cast<irr::gui::IGUICheckBox*>(p)->isChecked(); break;
		default: break;
		}
	}
	void Restore() const {
		auto p=control.get();
		if(p->getType()==irr::gui::EGUIET_COMBO_BOX) {
			auto c=static_cast<irr::gui::IGUIComboBox*>(p);
			bool same=c->getItemCount()==items.size();
			for(size_t i=0; same && i<items.size(); ++i)
				same=items[i].first==c->getItem(i) && items[i].second==c->getItemData(i);
			if(!same) {
				c->clear();
				for(const auto& item: items) c->addItem(item.first.c_str(),item.second);
			}
			c->setSelected(selected);
		} else if(text!=p->getText()) {
			// Leave the retained native edit control alone when unchanged: calling
			// setText/recreating it would lose native selection/caret continuity.
			p->setText(text.c_str());
		}
		if(p->getType()==irr::gui::EGUIET_SCROLL_BAR) {
			auto c=static_cast<irr::gui::IGUIScrollBar*>(p);
			c->setMin(minimum); c->setMax(maximum); c->setPos(position);
		} else if(p->getType()==irr::gui::EGUIET_BUTTON)
			static_cast<irr::gui::IGUIButton*>(p)->setPressed(checked);
		else if(p->getType()==irr::gui::EGUIET_CHECK_BOX)
			static_cast<irr::gui::IGUICheckBox*>(p)->setChecked(checked);
		p->setEnabled(enabled); p->setVisible(visible);
	}
};
std::array<irr::gui::IGUIElement*,10> editorWindows(Game& g) {
	return {g.wDeckEdit,g.wFilter,g.wSort,g.wInfos,g.wCardImg,g.btnLeaveGame,g.btnTestDeck,g.stTestDeckStatus,g.scrFilter,g.scrPackCards};
}
bool materialize(const std::vector<uint32_t>& codes, std::vector<const CardDataC*>& out) {
	const auto& data=dataManager.GetDataTable();
	out.reserve(codes.size());
	for(auto code: codes) {
		auto it=data.find(code);
		if(it==data.end()) return false;
		out.push_back(&it->second);
	}
	return true;
}
}

struct EditorSuspension {
	std::shared_ptr<const undo::TestDuelConfig> testConfig;
	undo::DeckSnapshot deck;
	undo::EditorHistory history;
	EditorValues values;
	std::vector<uint32_t> results;
	std::array<wchar_t,8> resultLabel{};
	std::shared_ptr<const LFList> limitList;
	std::vector<WidgetState> widgets;
	std::shared_ptr<irr::gui::IGUIElement> focus;
	int showingCode{}, infoTab{}, textScroll{};
	bool exitOnReturn{}, handedOff{};
	std::array<wchar_t,256> openFile{};
	explicit EditorSuspension(DeckBuilder& e) : deck(e.CaptureEditorDeck()), history(e.editorHistory), values(valueCopy(editorValues(e))) {}
};
bool DeckBuilder::CaptureDeckTestConfig() {
	if(!HasDeckTestPreparation() || suspension->testConfig)return false;
	HostInfo info{};
	const int selected=mainGame->cbBotRule?mainGame->cbBotRule->getSelected():-1;
	info.rule=5;info.mode=MODE_SINGLE;
	info.duel_rule=selected>=0 && selected<3?selected+3:std::clamp(mainGame->gameConf.default_rule,3,5);
	info.start_lp=8000;info.start_hand=5;info.draw_count=1;info.time_limit=0;
	info.no_check_deck=true;info.no_shuffle_deck=true;
	try {
		suspension->testConfig=std::make_shared<const undo::TestDuelConfig>(suspensionGeneration,
			suspension->deck[0],suspension->deck[1],info,mainGame->ebNickName->getText());
		return true;
	}catch(const std::bad_alloc&){return false;}
}
std::shared_ptr<const undo::TestDuelConfig> DeckBuilder::DeckTestConfig() const {
	return HasDeckTestPreparation()?suspension->testConfig:nullptr;
}

bool DeckBuilder::MatchesSuspension(const EditorSuspensionToken& token) const {
	return suspension && token.owner.lock()==suspensionOwner && token.generation==suspensionGeneration;
}
std::optional<EditorSuspensionToken> DeckBuilder::SuspendEditor() {
	if(!mainGame || this!=&mainGame->deckBuilder || suspension || showing_pack) return std::nullopt;
	const auto state=EditorUndoState();
	if(state.readOnly || state.dragging || state.modal || state.siding) return std::nullopt;
	try {
		auto saved=std::make_shared<EditorSuspension>(*this);
		if(filterList) saved->limitList=std::make_shared<LFList>(*filterList);
		for(auto card: results) saved->results.push_back(card->code);
		std::copy(std::begin(result_string),std::end(result_string),saved->resultLabel.begin());
		auto& g=*mainGame;
		const auto windows=editorWindows(g);
		std::vector<irr::gui::IGUIElement*> controls(windows.begin(),windows.end());
		const std::vector<irr::gui::IGUIElement*> extra={
			g.cbDBCategory,g.cbDBDecks,g.ebDeckname,g.btnManageDeck,g.btnUndoDeck,g.btnClearDeck,g.btnSortDeck,
			g.btnShuffleDeck,g.btnSaveDeck,g.btnSaveDeckAs,g.btnDeleteDeck,g.btnSideOK,g.btnSideShuffle,g.btnSideSort,g.btnSideReload,
			g.cbCardType,g.cbCardType2,g.cbAttribute,g.cbRace,g.cbLimit,g.cbSortType,
			g.ebAttack,g.ebDefense,g.ebStar,g.ebScale,g.ebCardName,g.btnEffectFilter,g.btnMarksFilter,g.btnStartFilter,g.btnClearFilter,
			g.scrCardText,g.wMainMenu};
		controls.insert(controls.end(),extra.begin(),extra.end());
		for(auto control: controls) saved->widgets.emplace_back(control);
		for(auto control: g.chkCategory) saved->widgets.emplace_back(control);
		for(auto control: g.btnMark) saved->widgets.emplace_back(control);
		saved->focus=retain(g.env->getFocus());
		saved->showingCode=g.showingcode; saved->infoTab=g.wInfos->getActiveTab(); saved->textScroll=g.scrCardText->getPos();
		saved->exitOnReturn=g.exit_on_return;
		std::copy(std::begin(g.open_file_name),std::end(g.open_file_name),saved->openFile.begin());
		// All owning allocations precede the first change to live editor state.
		// Preparation leaves the editor rendered while the pending capability
		// blocks input. Successful handoff owns the later presentation change.
		suspension=std::move(saved);
		++suspensionGeneration;
		g.btnUndoDeck->setEnabled(false);
		RefreshDeckTestEntry();
		EditorSuspensionToken token; token.owner=suspensionOwner; token.generation=suspensionGeneration;
		return token;
	} catch(const std::bad_alloc&) { return std::nullopt; }
}
bool DeckBuilder::CommitEditorHandoff(const EditorSuspensionToken& token, irr::IEventReceiver* receiver) {
	if(!MatchesSuspension(token) || !receiver || suspension->handedOff) return false;
	suspension->handedOff=true;
	mainGame->env->installPreparedFocus(nullptr);
	for(auto window: editorWindows(*mainGame)) window->setVisible(false);
	mainGame->is_building=false;
	mainGame->btnUndoDeck->setEnabled(false);
	mainGame->device->setEventReceiver(receiver);
	RefreshDeckTestEntry();
	return true;
}
bool DeckBuilder::ResumeEditor(const EditorSuspensionToken& token) {
	if(!MatchesSuspension(token)) return false;
	const bool ownsDeckTestPreparation = HasDeckTestPreparation();
	Deck restored;
	std::vector<const CardDataC*> restoredResults;
	undo::EditorHistory history;
	try {
		// No publication (or token consumption) if even the last code is missing.
		if(!materialize(suspension->deck[0],restored.main) || !materialize(suspension->deck[1],restored.extra)
			|| !materialize(suspension->deck[2],restored.side) || !materialize(suspension->results,restoredResults)) return false;
		if(suspension->showingCode && !dataManager.GetDataTable().count(suspension->showingCode)) return false;
		for(const auto& widget: suspension->widgets)
			if(widget.control->getParent()!=widget.parent) return false;
		history=suspension->history;
	} catch(const std::bad_alloc&) { return false; }
	// Keep the resource guard and widget owners alive until presentation is ready.
	const auto& saved=*suspension;
	auto& g=*mainGame;
	std::swap(deckManager.current_deck,restored);
	std::swap(editorHistory,history);
	results.swap(restoredResults);
	editorValues(*this)=saved.values;
	restoredFilterList=saved.limitList; filterList=restoredFilterList.get();
	std::copy(saved.resultLabel.begin(),saved.resultLabel.end(),std::begin(result_string));
	std::copy(saved.openFile.begin(),saved.openFile.end(),std::begin(g.open_file_name));
	g.exit_on_return=saved.exitOnReturn;
	// Recreate inspection from a code, never from a retained texture/string pointer.
	g.ClearCardInfo();
	if(saved.showingCode) g.ShowCardInfo(saved.showingCode,true);
	for(const auto& widget: saved.widgets) widget.Restore();
	if(saved.showingCode && g.scrCardText->isVisible())
		g.SetStaticText(g.stText,g.stText->getRelativePosition().getWidth()-25,g.guiFont,g.showingtext,saved.textScroll);
	g.wInfos->setActiveTab(saved.infoTab);
	is_draging=false; is_starting_dragging=false; draging_pointer=nullptr;
	g.is_building=true; g.is_siding=false;
	// The original controls are retained. Restore focus without a synthetic
	// focus-loss event erasing the native edit box's selected text.
	if(saved.focus && saved.focus->isVisible() && saved.focus->isEnabled()) g.env->installPreparedFocus(saved.focus.get());
	else g.env->installPreparedFocus(nullptr);
	g.device->setEventReceiver(this);
	suspension.reset();
	if(ownsDeckTestPreparation) deckTestPreparation.reset();
	RefreshEditorUndo();
	RefreshDeckTestEntry();
	return true;
}
}
