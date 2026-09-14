#pragma once
#include <sstream>

// Independent observation of the actual controls and editor state, not a copy of
// the suspension implementation. Any dropped field changes the visible result.
static std::wstring observeEditor(Game& g) {
    std::wostringstream out;
    const auto& e = g.deckBuilder;
    for(auto c : {g.cbDBCategory,g.cbDBDecks,g.cbCardType,g.cbCardType2,g.cbAttribute,g.cbRace,g.cbLimit,g.cbSortType}) {
        out << c->getSelected() << ':' << c->isEnabled() << ':' << c->isVisible() << ':';
        for(irr::u32 i=0;i<c->getItemCount();++i) out << c->getItem(i) << ':' << c->getItemData(i) << ';';
        out << '|';
    }
    for(auto t : {g.ebDeckname,g.ebCardName,g.ebAttack,g.ebDefense,g.ebStar,g.ebScale})
        out << t->getText() << ':' << t->isEnabled() << ':' << t->isVisible() << '|';
    for(auto s : {g.scrFilter,g.scrPackCards,g.scrCardText})
        out << s->getMin() << ':' << s->getMax() << ':' << s->getPos() << ':' << s->isVisible() << ':' << s->isEnabled() << '|';
    for(auto b : {g.btnEffectFilter,g.btnMarksFilter,g.btnManageDeck,g.btnSaveDeck,g.btnSaveDeckAs,g.btnClearDeck,
                  g.btnSortDeck,g.btnShuffleDeck,g.btnDeleteDeck,g.btnLeaveGame,g.btnSideOK,g.btnSideShuffle,g.btnSideSort,g.btnSideReload})
        out << b->isPressed() << ':' << b->isEnabled() << ':' << b->isVisible() << ':' << b->getText() << '|';
    out << g.btnTestDeck->isPressed() << ':' << g.btnTestDeck->isEnabled() << ':' << g.btnTestDeck->isVisible() << ':' << g.btnTestDeck->getText() << '|';
    out << g.stTestDeckStatus->isEnabled() << ':' << g.stTestDeckStatus->isVisible() << ':' << g.stTestDeckStatus->getText() << '|';
    for(auto c : g.chkCategory) out << c->isChecked();
    for(auto b : g.btnMark) out << b->isPressed();
    for(auto w : std::array<irr::gui::IGUIElement*,5>{g.wDeckEdit,g.wFilter,g.wSort,g.wInfos,g.wCardImg})
        out << w->isVisible() << ':' << w->isEnabled() << '|';
    out << e.filter_effect << ',' << e.filter_type << ',' << e.filter_type2 << ',' << e.filter_attrib << ',' << e.filter_race << ','
        << e.filter_atktype << ',' << e.filter_atk << ',' << e.filter_deftype << ',' << e.filter_def << ',' << e.filter_lvtype << ','
        << e.filter_lv << ',' << e.filter_scltype << ',' << e.filter_scl << ',' << e.filter_marks << ',' << e.filter_lm << '|'
        << e.prev_category << ',' << e.prev_deck << ',' << e.prev_operation << ',' << e.prev_sel << ',' << e.mouse_pos.X << ',' << e.mouse_pos.Y << ','
        << e.hovered_code << ',' << e.hovered_pos << ',' << e.hovered_seq << ',' << e.is_lastcard << ',' << e.click_pos << '|'
        << e.readonly << e.showing_pack << e.editorHistoryValid << e.is_modified << g.is_building << g.is_siding << '|'
        << g.showingcode << ':' << g.wInfos->getActiveTab() << ':' << g.stText->getText() << ':' << e.result_string << '|'
        << g.exit_on_return << ':' << g.open_file_name << '|';
    for(auto card : e.results) out << card->code << ',';
    return out.str();
}
static irr::SEvent textKey(irr::EKEY_CODE code, bool control=false, wchar_t ch=0) {
    irr::SEvent e{}; e.EventType=irr::EET_KEY_INPUT_EVENT;
    e.KeyInput.PressedDown=true; e.KeyInput.Key=code; e.KeyInput.Control=control; e.KeyInput.Char=ch;
    return e;
}

template<class Editor> static void editorSuspensionCases(Game& game, Editor& editor) {
    constexpr unsigned A=89631139, B=46986414, X=23995346;
    const auto& data=dataManager.GetDataTable();
    CHECK(data.at(A).type==(TYPE_MONSTER|TYPE_NORMAL));
    CHECK(data.at(B).type==(TYPE_MONSTER|TYPE_NORMAL));
    CHECK(data.at(X).type==(TYPE_MONSTER|TYPE_FUSION));
    const undo::DeckSnapshot initial{{{A,B,A},{X,X},{B,A}}};
    const undo::DeckSnapshot reordered{{{B,A,A},{X,X},{B,A}}};
    const undo::DeckSnapshot saved{{{A,A},{X,X},{B,B,A}}};
    const undo::DeckSnapshot edited{{{A},{X,X},{B,B,A}}};
    const auto originalConfig=bytes("system.conf");
    const auto undoConfig=bytes("system-undo.conf");
    const auto oldAuto=game.gameConf.auto_search_limit;
    const auto oldSeparate=game.gameConf.separate_clear_button;
    game.gameConf.auto_search_limit=-1; game.gameConf.separate_clear_button=false;
    std::filesystem::create_directory("deck/undo-editor-suspend");
    CHECK(editor.RestoreEditorDeck(initial)); editor.ResetEditorHistory();
    game.env->setFocus(nullptr);
    game.cbDBCategory->clear();
    for(auto name : {L"pack",L"bot",L"deck",L"separator",L"undo-editor-suspend"}) game.cbDBCategory->addItem(name);
    game.cbDBCategory->setSelected(4); editor.prev_category=4;
    game.cbDBDecks->clear(); game.cbDBDecks->addItem(L"unused",17); game.cbDBDecks->addItem(L"task8-save",23);
    game.cbDBDecks->setSelected(1); editor.prev_deck=1;
    LFList localList; localList.hash=123456; localList.listName=L"owned list"; localList.content[53129443]=1;
    editor.filterList=&localList;
    button(game.btnSaveDeck);
    drag(368,180,320,180); mouse(irr::EMIE_LMOUSE_LEFT_UP,320,180);
    CHECK(editor.CaptureEditorDeck()==reordered);
    drag(320,180,320,580); mouse(irr::EMIE_LMOUSE_LEFT_UP,320,580);
    CHECK(editor.CaptureEditorDeck()==saved);
    button(game.btnSaveDeck); CHECK(!editor.is_modified); CHECK(editor.editorHistory.CanUndo());
    const auto savedBytes=bytes("deck/undo-editor-suspend/task8-save.ydk");
    mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180);
    CHECK(editor.CaptureEditorDeck()==edited); CHECK(editor.is_modified);

    // Production search followed by ClearFilter deliberately leaves applied
    // numeric filters/results different from the visible controls.
    editor.ClearSearch(); game.cbCardType->setSelected(1); change(game.cbCardType);
    game.cbCardType2->setSelected(1); game.cbSortType->setSelected(3);
    game.ebAttack->setText(L">=0"); game.ebStar->setText(L"=4");
    button(game.btnStartFilter); CHECK(editor.results.size()>7);
    CHECK(std::wstring(game.ebAttack->getText()).empty()); CHECK(editor.filter_atktype==2);
    game.ebCardName->setText(L"unsubmitted name"); game.ebDeckname->setText(L"typed save-as name");
    game.ebAttack->setText(L">=2500"); game.ebDefense->setText(L"?"); game.ebStar->setText(L"8"); game.ebScale->setText(L"3");
    game.cbAttribute->setSelected(2); game.cbRace->setSelected(2); game.cbLimit->setSelected(4);
    game.chkCategory[7]->setChecked(true); game.chkCategory[31]->setChecked(true);
    game.btnMark[2]->setPressed(true); game.btnMark[7]->setPressed(true);
    game.btnEffectFilter->setPressed(true); game.btnMarksFilter->setPressed(true);
    game.scrFilter->setPos(3);
    mouse(irr::EMIE_MOUSE_MOVED,368,580); CHECK(editor.hovered_pos==3); CHECK(editor.hovered_seq==1); CHECK(editor.hovered_code==B);
    // A fixed long effect card gives a real nonzero card-text scroll position
    // when the fixture uses the same UTF-8 locale initialization as production.
    constexpr unsigned scrollCard=6218704;
    CHECK(data.at(scrollCard).type == (TYPE_MONSTER|TYPE_FUSION|TYPE_EFFECT|TYPE_PENDULUM));
    game.ShowCardInfo(scrollCard); CHECK(game.scrCardText->getMax()>0); CHECK(game.scrCardText->isVisible());
    game.scrCardText->setPos(1);
    irr::SEvent scroll{}; scroll.EventType=irr::EET_GUI_EVENT; scroll.GUIEvent.Caller=game.scrCardText;
    scroll.GUIEvent.EventType=irr::gui::EGET_SCROLL_BAR_CHANGED; editor.OnEvent(scroll);
    game.wInfos->setActiveTab(1);
    editor.prev_operation=0; editor.prev_sel=1; editor.rnd.seed(87123);
    const auto expectedRng=editor.rnd;
    game.env->setFocus(game.ebDeckname); game.ebDeckname->OnEvent(textKey(irr::KEY_KEY_A,true));
    editor.RefreshEditorUndo(); const auto before=observeEditor(game);
    auto* nameControl=game.ebDeckname;
    auto token=editor.SuspendEditor(); CHECK(token); CHECK(editor.HasEditorSuspension());
    CHECK(game.device->getEventReceiver()==&editor); CHECK(game.is_building);
    CHECK(game.env->getFocus()==nameControl); CHECK(game.wDeckEdit->isVisible());
    CHECK(game.btnTestDeck->isVisible()); CHECK(!game.btnTestDeck->isEnabled()); CHECK(game.stTestDeckStatus->isVisible());
    CHECK(!editor.SuspendEditor());
    CHECK(editor.CommitEditorHandoff(*token,&game.dField));
    CHECK(game.device->getEventReceiver()==&game.dField); CHECK(!game.is_building);
    CHECK(game.env->getFocus()==nullptr); CHECK(!game.wDeckEdit->isVisible()); CHECK(!game.btnTestDeck->isVisible()); CHECK(!game.stTestDeckStatus->isVisible());
    CHECK(!editor.CommitEditorHandoff(*token,&game.menuHandler));
    // No ordinary editor entry/event may reset the suspension or save the shared consumer's deck.
    deckManager.current_deck={}; editor.Initialize(); editor.ResetEditorHistory(); editor.EditorDeckSaved();
    editor.BeginEditorEdit(); CHECK(!editor.editorEditStart); CHECK(!editor.LoadEditorDeck(L"./deck/undo-editor-suspend/task8-save.ydk"));
    CHECK(!editor.RestoreEditorDeck(initial)); button(game.btnSaveDeck); CHECK(deckManager.current_deck.main.empty());
    game.is_building=true; editor.RefreshEditorUndo(); CHECK(!game.btnUndoDeck->isEnabled());
    CHECK(!editor.UndoEditorEdit()); game.is_building=false;
    CHECK(bytes("deck/undo-editor-suspend/task8-save.ydk")==savedBytes);
    // Public shared resource reload paths are blocked while the editor is not building.
    CHECK(!dataManager.LoadDB("cards.cdb")); CHECK(!dataManager.LoadStrings("strings.conf"));
    game.LoadExpansions(); CHECK(editor.HasEditorSuspension()); CHECK(!game.is_building);
    // Adapter integration stand-in: the later duel consumer overwrites shared
    // deck, inspection, presentation and selectors. It never mutates the token.
    game.ShowCardInfo(X); game.wInfos->setActiveTab(0);
    game.cbDBCategory->clear(); game.cbDBCategory->addItem(L"consumer");
    game.cbDBDecks->clear(); game.cbDBDecks->addItem(L"consumer");
    game.cbCardType2->clear(); game.cbCardType2->addItem(L"consumer",99);
    game.scrFilter->setMax(0); game.scrFilter->setPos(0); game.btnLeaveGame->setText(L"consumer");
    game.cbDBDecks->setEnabled(false); game.btnManageDeck->setEnabled(false);
    localList.content.clear(); localList.hash=999; editor.rnd.seed(1); editor.filter_atk=-99;
    Editor foreign; CHECK(!foreign.ResumeEditor(*token));
    // Safe in-memory fault injection: remove the LAST result's lookup node while
    // keeping that node (and all its pointees) alive; no database file is touched.
    const auto missing=editor.results.back()->code;
    auto& mutableData=const_cast<std::unordered_map<uint32_t,CardDataC>&>(data);
    auto heldNode=mutableData.extract(missing); CHECK(!heldNode.empty());
    const auto failedView=observeEditor(game); const auto sharedDeck=editor.CaptureEditorDeck();
    CHECK(!editor.ResumeEditor(*token)); CHECK(editor.HasEditorSuspension());
    CHECK(observeEditor(game)==failedView); CHECK(editor.CaptureEditorDeck()==sharedDeck);
    CHECK(game.device->getEventReceiver()==&game.dField);
    mutableData.insert(std::move(heldNode));
    CHECK(editor.ResumeEditor(*token)); CHECK(!editor.HasEditorSuspension());
    CHECK(editor.CaptureEditorDeck()==edited); CHECK(observeEditor(game)==before);
    CHECK(editor.rnd==expectedRng); CHECK(editor.filterList!=&localList);
    CHECK(editor.filterList->hash==123456); CHECK(editor.filterList->content.at(53129443)==1);
    CHECK(game.device->getEventReceiver()==&editor); CHECK(game.env->getFocus()==nameControl); CHECK(game.ebDeckname==nameControl);
    CHECK(!key(irr::KEY_KEY_Z,true)); CHECK(editor.CaptureEditorDeck()==edited);
    // The existing control has no text Ctrl+Z implementation. Its selected text
    // and caret must still survive the no-event focus round trip.
    nameControl->OnEvent(textKey(irr::KEY_KEY_Q,false,L'q')); CHECK(std::wstring(nameControl->getText())==L"q");
    game.env->setFocus(nullptr);
    CHECK(key(irr::KEY_KEY_Z,true)); CHECK(editor.CaptureEditorDeck()==saved); CHECK(!editor.is_modified);
    // Clean with nonempty history is a separate baseline case.
    auto cleanToken=editor.SuspendEditor(); CHECK(cleanToken); CHECK(!editor.ResumeEditor(*token));
    deckManager.current_deck={}; CHECK(editor.ResumeEditor(*cleanToken));
    CHECK(editor.CaptureEditorDeck()==saved); CHECK(!editor.is_modified); CHECK(editor.editorHistory.CanUndo());
    CHECK(key(irr::KEY_KEY_Z,true)); CHECK(editor.CaptureEditorDeck()==reordered); CHECK(editor.is_modified);
    CHECK(key(irr::KEY_KEY_Z,true)); CHECK(editor.CaptureEditorDeck()==initial); CHECK(editor.is_modified);
    CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.ResumeEditor(*cleanToken));
    CHECK(bytes("deck/undo-editor-suspend/task8-save.ydk")==savedBytes);
    CHECK(bytes("system.conf")==originalConfig); CHECK(bytes("system-undo.conf")==undoConfig);

    // Rejected unstable entry must preserve both a pending press and a
    // provisionally popped drag, including the saved pre-edit transaction.
    for(bool active : {false,true}) {
        mouse(irr::EMIE_MOUSE_MOVED,368,180); mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,368,180);
        CHECK(editor.is_starting_dragging);
        if(active) { mouse(irr::EMIE_MOUSE_MOVED,300,640); CHECK(editor.is_draging); }
        const auto gestureDeck=editor.CaptureEditorDeck(); const auto editStart=editor.editorEditStart; const auto pointer=editor.draging_pointer;
        CHECK(!editor.SuspendEditor()); CHECK(editor.is_starting_dragging==!active); CHECK(editor.is_draging==active);
        CHECK(editor.editorEditStart==editStart); CHECK(editor.draging_pointer==pointer); CHECK(editor.CaptureEditorDeck()==gestureDeck);
        CHECK(key(irr::KEY_ESCAPE)); CHECK(editor.CaptureEditorDeck()==initial);
    }
    editor.BeginEditorEdit(); CHECK(!editor.SuspendEditor()); CHECK(editor.editorEditStart); editor.FinishEditorEdit(false);
    for(auto modal : {game.wQuery,game.wCategories,game.wLinkMarks,game.wDeckManage,game.wDMQuery,game.wMessage,game.wBigCard}) {
        modal->setVisible(true); CHECK(!editor.SuspendEditor()); CHECK(modal->isVisible()); modal->setVisible(false);
    }
    auto modal=game.env->addModalScreen(nullptr); game.env->setFocus(modal);
    CHECK(!editor.SuspendEditor());
    // A visible Irrlicht modal vetoes focus removal. Close it before tearing
    // down this fixture, exactly as an ordinary modal close does.
    modal->setVisible(false); game.env->setFocus(nullptr); modal->remove();
    CHECK(!editor.EditorUndoState().modal);
    editor.readonly=true; CHECK(!editor.SuspendEditor()); editor.readonly=false;
    editor.showing_pack=true; CHECK(!editor.SuspendEditor()); editor.showing_pack=false;
    game.is_siding=true; CHECK(!editor.SuspendEditor()); game.is_siding=false;
    CHECK(editor.CaptureEditorDeck()==initial);
    // Fresh content plus full-path editor flavor and independent invalid-history state.
    mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180);
    const undo::DeckSnapshot latest{{{B,A},{X,X},{B,A}}}; CHECK(editor.CaptureEditorDeck()==latest);
    game.cbDBCategory->setSelected(-1); game.cbDBDecks->setSelected(-1);
    game.cbDBCategory->setEnabled(false); game.cbDBDecks->setEnabled(false); game.btnManageDeck->setEnabled(false);
    game.exit_on_return=true; std::wcscpy(game.open_file_name,L"fixture-owned-open-file.ydk");
    editor.editorHistoryValid=false; editor.RefreshEditorUndo();
    const auto openView=observeEditor(game); auto openToken=editor.SuspendEditor(); CHECK(openToken);
    game.exit_on_return=false; game.open_file_name[0]=0; deckManager.current_deck={};
    CHECK(editor.ResumeEditor(*openToken)); CHECK(observeEditor(game)==openView); CHECK(editor.CaptureEditorDeck()==latest);
    CHECK(!key(irr::KEY_KEY_Z,true)); CHECK(editor.editorHistory.CanUndo());
    game.exit_on_return=false; editor.editorHistoryValid=true;
    game.cbDBCategory->setSelected(4); game.cbDBDecks->setSelected(1);
    game.cbDBCategory->setEnabled(true); game.cbDBDecks->setEnabled(true); game.btnManageDeck->setEnabled(true);
    // Ordinary No keeps history; Yes and later Initialize cannot resurrect it.
    button(game.btnLeaveGame); button(game.btnNo); game.wQuery->setVisible(false); CHECK(editor.editorHistory.CanUndo());
    button(game.btnLeaveGame); button(game.btnYes); CHECK(!game.is_building); CHECK(!editor.editorHistory.CanUndo());
    CHECK(!editor.ResumeEditor(*openToken));
    // This event harness does not advance the main loop's ten-frame dialog fade.
    game.wQuery->setVisible(false); game.wMainMenu->setVisible(false); editor.Initialize();
    CHECK(!editor.editorHistory.CanUndo());
    auto abandoned=editor.SuspendEditor(); CHECK(abandoned); editor.Terminate();
    CHECK(!editor.HasEditorSuspension()); CHECK(!editor.ResumeEditor(*abandoned));
    game.wMainMenu->setVisible(false); editor.Initialize(); CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.ResumeEditor(*abandoned));
    game.gameConf.auto_search_limit=oldAuto; game.gameConf.separate_clear_button=oldSeparate;
    std::cout << "PASS Task8 actual Game/DeckBuilder adapter: full dirty/clean history, presentation, focus, resources, failure and token lifecycle\n";
}
