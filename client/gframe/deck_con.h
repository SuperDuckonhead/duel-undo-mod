#ifndef DECK_CON_H
#define DECK_CON_H

#include <string>
#include <vector>
#include <random>
#include <memory>
#include "undo/editor_history.h"
#include "undo/editor_input.h"
#include <IEventReceiver.h>
#include <vector2d.h>

namespace ygo {

struct CardDataC;
struct LFList;
struct EditorSuspension;

// A capability for one pending suspension. Copies cannot replay a consumed token
// or refer to a different DeckBuilder, including one allocated at the same address.
class EditorSuspensionToken {
public:
	EditorSuspensionToken() = default;
private:
	friend class DeckBuilder;
	std::weak_ptr<const char> owner;
	uint64_t generation{};
};

class DeckBuilder: public irr::IEventReceiver {
public:
	DeckBuilder();
	DeckBuilder(const DeckBuilder&) = delete;
	DeckBuilder& operator=(const DeckBuilder&) = delete;
	// UI/event-thread lifecycle. The same Game and native widget tree must remain
	// alive; shared card/string reload is pinned until resume or ordinary exit.
	std::optional<EditorSuspensionToken> SuspendEditor();
	// Call only after the later room/start operation has succeeded.
	bool CommitEditorHandoff(const EditorSuspensionToken& token, irr::IEventReceiver* receiver);
	bool ResumeEditor(const EditorSuspensionToken& token);
	bool HasEditorSuspension() const { return bool(suspension); }
	bool OnEvent(const irr::SEvent& event) override;
	undo::DeckSnapshot CaptureEditorDeck() const;
	bool RestoreEditorDeck(const undo::DeckSnapshot& snapshot);
	void BeginEditorEdit();
	void FinishEditorEdit(bool accepted);
	bool UndoEditorEdit();
	void ResetEditorHistory();
	void EditorDeckSaved();
	bool LoadEditorDeck(const wchar_t* file, bool pack = false);
	void CancelEditorDrag();
	undo::EditorInputState EditorUndoState() const;
	void RefreshEditorUndo();
	undo::EditorHistory editorHistory;
	std::optional<undo::DeckSnapshot> editorEditStart;
	bool editorHistoryValid{true};
	void Initialize();
	void Terminate();
	void GetHoveredCard();
	void FilterCards();
	void StartFilter();
	void ClearFilter();
	void InstantSearch();
	void ClearSearch();
	void SortList();

	void RefreshDeckList();
	void RefreshReadonly(int catesel);
	void RefreshPackListScroll();
	void ChangeCategory(int catesel);
	void ShowDeckManage();
	void ShowBigCard(int code, float zoom);
	void ZoomBigCard(irr::s32 centerx = -1, irr::s32 centery = -1);
	void CloseBigCard();

	bool push_main(const CardDataC* pointer, int seq = -1);
	bool push_extra(const CardDataC* pointer, int seq = -1);
	bool push_side(const CardDataC* pointer, int seq = -1);
	void pop_main(int seq);
	void pop_extra(int seq);
	void pop_side(int seq);
	bool check_limit(const CardDataC* pointer);

	unsigned long long filter_effect{};
	unsigned int filter_type{};
	unsigned int filter_type2{};
	unsigned int filter_attrib{};
	unsigned int filter_race{};
	unsigned int filter_atktype{};
	int filter_atk{};
	unsigned int filter_deftype{};
	int filter_def{};
	unsigned int filter_lvtype{};
	unsigned int filter_lv{};
	unsigned int filter_scltype{};
	unsigned int filter_scl{};
	unsigned int filter_marks{};
	int filter_lm{};
	irr::core::vector2di mouse_pos;
	int hovered_code{};
	int hovered_pos{};
	int hovered_seq{ -1 };
	int is_lastcard{};
	int click_pos{};
	bool is_draging{};
	bool is_starting_dragging{};
	int dragx{};
	int dragy{};
	int bigcard_code{};
	float bigcard_zoom{};
	size_t pre_mainc{};
	size_t pre_extrac{};
	size_t pre_sidec{};
	const CardDataC* draging_pointer{};
	int prev_category{};
	int prev_deck{};
	irr::s32 prev_operation{};
	int prev_sel{ -1 };
	bool is_modified{};
	bool readonly{};
	bool showing_pack{};
	std::mt19937 rnd;

	const LFList* filterList{};
	std::vector<const CardDataC*> results;
	wchar_t result_string[8]{};
	std::vector<std::wstring> expansionPacks;
private:
	bool MatchesSuspension(const EditorSuspensionToken& token) const;
	std::shared_ptr<const char> suspensionOwner{std::make_shared<const char>(0)};
	uint64_t suspensionGeneration{};
	std::shared_ptr<EditorSuspension> suspension;
	std::shared_ptr<const LFList> restoredFilterList;
};

}

#endif //DECK_CON_H
