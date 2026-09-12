#pragma once

#include "duelclient.h"
#include "image_manager.h"
#include "undo/strings_zh.h"
#include <array>

static bool overlaps(const irr::core::recti& a, const irr::core::recti& b) {
    return a.UpperLeftCorner.X < b.LowerRightCorner.X && a.LowerRightCorner.X > b.UpperLeftCorner.X
        && a.UpperLeftCorner.Y < b.LowerRightCorner.Y && a.LowerRightCorner.Y > b.UpperLeftCorner.Y;
}

static const char* entryStateName(DeckTestEntryState state) {
    switch(state) {
    case DeckTestEntryState::Ready: return "Ready";
    case DeckTestEntryState::InactiveEditor: return "InactiveEditor";
    case DeckTestEntryState::PendingPreparation: return "PendingPreparation";
    case DeckTestEntryState::Siding: return "Siding";
    case DeckTestEntryState::ReadOnlyPack: return "ReadOnlyPack";
    case DeckTestEntryState::ReadOnlyDeck: return "ReadOnlyDeck";
    case DeckTestEntryState::Dragging: return "Dragging";
    case DeckTestEntryState::BlockingDialog: return "BlockingDialog";
    }
    return "Unknown";
}

static void resizeEntryFixture(Game& game, float scale) {
    const irr::core::dimension2du expected(static_cast<irr::u32>(GAME_WINDOW_WIDTH * scale),
        static_cast<irr::u32>(GAME_WINDOW_HEIGHT * scale));
    game.SetWindowsScale(scale);
    for(int i=0; i<100 && game.driver->getScreenSize()!=expected; ++i) {
        game.device->run();
        Sleep(10);
    }
    CHECK(game.driver->getScreenSize()==expected);
    game.window_size=expected;
    game.xScale=expected.Width/static_cast<float>(GAME_WINDOW_WIDTH);
    game.yScale=expected.Height/static_cast<float>(GAME_WINDOW_HEIGHT);
    game.OnResize();
    game.deckBuilder.RefreshDeckTestEntry();
}

static void captureEntryFrame(Game& game, const wchar_t* name) {
    game.driver->beginScene(true,true,irr::video::SColor(0,0,0,0));
    game.DrawBackImage(imageManager.tBackGround_deck);
    game.DrawDeckBd();
    game.DrawGUI();
    game.driver->endScene();
    auto* image=game.driver->createScreenShot();
    CHECK(image);
    CHECK(image->getDimension()==game.driver->getScreenSize());
    const auto path=undo::ExecutableRoot()/name;
    CHECK(game.driver->writeImageToFile(image,path.wstring().c_str()));
    image->drop();
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path)>0);
}

static void expectEntry(Game& game, DeckTestEntryState state, bool visible, bool enabled, const wchar_t* hint) {
    static unsigned checkIndex=0;
    const auto currentCheck=++checkIndex;
    auto& editor=game.deckBuilder;
    editor.RefreshDeckTestEntry();
    const auto actual=editor.GetDeckTestEntryState();
    if(actual!=state) {
        std::ostringstream detail;
        const auto input=editor.EditorUndoState();
        detail << "entry check=" << currentCheck << " expected=" << entryStateName(state) << " actual=" << entryStateName(actual)
            << " building=" << game.is_building << " siding=" << game.is_siding
            << " readonly=" << editor.readonly << " pack=" << editor.showing_pack
            << " suspension=" << editor.HasEditorSuspension() << " dragging=" << input.dragging
            << " modal=" << input.modal
            << " windows[q=" << game.wQuery->isVisible() << ",categories=" << game.wCategories->isVisible()
            << ",marks=" << game.wLinkMarks->isVisible() << ",manage=" << game.wDeckManage->isVisible()
            << ",dm=" << game.wDMQuery->isVisible() << ",message=" << game.wMessage->isVisible()
            << ",big=" << game.wBigCard->isVisible() << "] focus=";
        for(auto* focus=game.env->getFocus(); focus; focus=focus->getParent())
            detail << static_cast<int>(focus->getType()) << ':' << focus->getID() << ',';
        throw std::runtime_error(detail.str());
    }
    CHECK(game.btnTestDeck->isVisible()==visible);
    CHECK(game.btnTestDeck->isEnabled()==enabled);
    CHECK(game.btnTestDeck->getToolTipText()==hint);
}

static void nativeType(Game& game, wchar_t ch) {
    SendMessageW(game.hWnd,WM_KEYDOWN,static_cast<WPARAM>(ch),0);
    SendMessageW(game.hWnd,WM_CHAR,static_cast<WPARAM>(ch),0);
    SendMessageW(game.hWnd,WM_KEYUP,static_cast<WPARAM>(ch),0);
    game.device->run();
}

static void settleWindowFade(Game& game, irr::gui::IGUIElement* window) {
    for(int frame=0; frame<24; ++frame) {
        const bool pending=std::any_of(game.fadingList.begin(),game.fadingList.end(),
            [&](const FadingUnit& unit) { return unit.guiFading==window; });
        if(!pending) break;
        game.driver->beginScene(true,true,irr::video::SColor(0,0,0,0));
        game.DrawGUI();
        game.driver->endScene();
    }
    CHECK(std::none_of(game.fadingList.begin(),game.fadingList.end(),
        [&](const FadingUnit& unit) { return unit.guiFading==window; }));
    CHECK(!window->isVisible());
}

static void settleWindowShown(Game& game, irr::gui::IGUIElement* window) {
    for(int frame=0; frame<24; ++frame) {
        const bool pending=std::any_of(game.fadingList.begin(),game.fadingList.end(),
            [&](const FadingUnit& unit) { return unit.guiFading==window; });
        if(!pending) break;
        game.driver->beginScene(true,true,irr::video::SColor(0,0,0,0));
        game.DrawGUI();
        game.driver->endScene();
    }
    CHECK(std::none_of(game.fadingList.begin(),game.fadingList.end(),
        [&](const FadingUnit& unit) { return unit.guiFading==window; }));
    CHECK(window->isVisible());
}

template<class Editor> static void editorTestEntryCases(Game& game, Editor& editor) {
    constexpr unsigned A=89631139, B=46986414, X=23995346;
    const auto& data=dataManager.GetDataTable();
    CHECK(data.at(A).type==(TYPE_MONSTER|TYPE_NORMAL));
    CHECK(data.at(B).type==(TYPE_MONSTER|TYPE_NORMAL));
    CHECK(data.at(X).type==(TYPE_MONSTER|TYPE_FUSION));
    const undo::DeckSnapshot clean{{{A,B,A},{X,X},{B,A}}};
    // This harness does not run MainLoop's fade advancement. Begin this focused
    // case from the same settled editor-window state used by the legacy cases.
    game.wQuery->setVisible(false);
    CHECK(editor.RestoreEditorDeck(clean)); editor.ResetEditorHistory();
    editor.results={&data.at(A),&data.at(B),&data.at(X)};
    game.env->setFocus(nullptr);
    editor.readonly=false; editor.showing_pack=false; game.is_building=true; game.is_siding=false;
    CHECK(BUTTON_TEST_DECK==329);
    CHECK(std::wstring(game.btnTestDeck->getText())==undo::DeckTestText);
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    for(const auto& variant : std::array<std::pair<float,const wchar_t*>,4>{{
        {0.8f,L"deck-test-entry-080-enabled.png"},
        {1.0f,L"deck-test-entry-100-enabled.png"},
        {1.25f,L"deck-test-entry-125-enabled.png"},
        {1.5f,L"deck-test-entry-150-enabled.png"}}}) {
        resizeEntryFixture(game,variant.first);
        const auto entry=game.btnTestDeck->getAbsolutePosition();
        CHECK(entry==game.Resize(205,85,295,120));
        CHECK(!overlaps(entry,game.wCardImg->getAbsolutePosition()));
        CHECK(!overlaps(entry,game.btnLeaveGame->getAbsolutePosition()));
        CHECK(!overlaps(entry,game.wDeckEdit->getAbsolutePosition()));
        CHECK(!overlaps(entry,game.Resize(309,136,410,157)));
        const auto label=game.env->getSkin()->getFont()->getDimension(undo::DeckTestText);
        CHECK(label.Width<=static_cast<irr::u32>(entry.getWidth()));
        CHECK(label.Height<=static_cast<irr::u32>(entry.getHeight()));
        captureEntryFrame(game,variant.second);
    }
    resizeEntryFixture(game,1.0f);

    const auto deckFile=bytes("deck/undo-editor-integration.ydk");
    const auto botExecutable=game.pending_bot_executable;
    CHECK(!DuelClient::Room()); CHECK(!editor.HasEditorSuspension());
    const auto center=game.btnTestDeck->getAbsolutePosition().getCenter();
    nativeClick(game,center.X,center.Y);
    CHECK(editor.HasDeckTestPreparation()); CHECK(editor.HasEditorSuspension());
    CHECK(editor.CaptureEditorDeck()==clean); CHECK(!editor.editorHistory.CanUndo());
    CHECK(game.device->getEventReceiver()==&editor); CHECK(game.is_building);
    CHECK(game.btnTestDeck->isVisible()); CHECK(!game.btnTestDeck->isEnabled());
    CHECK(!DuelClient::Room()); CHECK(game.pending_bot_executable==botExecutable);
    CHECK(bytes("deck/undo-editor-integration.ydk")==deckFile);
    button(game.btnTestDeck); CHECK(editor.HasDeckTestPreparation());
    CHECK(!editor.RequestDeckTestPreparation());
    deckManager.current_deck={};
    CHECK(editor.ResumeDeckTestPreparation()); CHECK(!editor.HasEditorSuspension());
    CHECK(editor.CaptureEditorDeck()==clean); CHECK(!editor.editorHistory.CanUndo());
    CHECK(bytes("deck/undo-editor-integration.ydk")==deckFile);
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    editor.BeginEditorEdit(); CHECK(editor.push_main(&data.at(B))); editor.FinishEditorEdit(true);
    const auto dirty=editor.CaptureEditorDeck();
    CHECK(dirty!=clean); CHECK(editor.editorHistory.CanUndo()); CHECK(editor.is_modified);
    game.ebDeckname->setText(L"pending-input-guard"); game.env->setFocus(game.ebDeckname);
    button(game.btnTestDeck);
    CHECK(editor.HasDeckTestPreparation()); CHECK(game.env->getFocus()==game.ebDeckname);
    const auto pendingView=observeEditor(game);
    nativeType(game,L'Q');
    const auto sortCenter=game.btnSortDeck->getAbsolutePosition().getCenter();
    nativeClick(game,sortCenter.X,sortCenter.Y);
    CHECK(observeEditor(game)==pendingView); CHECK(editor.CaptureEditorDeck()==dirty);
    button(game.btnTestDeck); CHECK(editor.HasDeckTestPreparation());
    deckManager.current_deck={};
    CHECK(editor.ResumeDeckTestPreparation());
    CHECK(editor.CaptureEditorDeck()==dirty); CHECK(editor.editorHistory.CanUndo()); CHECK(editor.is_modified);
    CHECK(game.env->getFocus()==game.ebDeckname); CHECK(std::wstring(game.ebDeckname->getText())==L"pending-input-guard");

    CHECK(editor.RequestDeckTestPreparation());
    CHECK(editor.CommitDeckTestPreparation(&game.dField));
    CHECK(editor.HasDeckTestPreparation()); CHECK(!game.is_building);
    CHECK(game.device->getEventReceiver()==&game.dField);
    CHECK(!game.wDeckEdit->isVisible()); CHECK(!game.btnTestDeck->isVisible());
    CHECK(!editor.CommitDeckTestPreparation(&game.menuHandler));
    CHECK(editor.ResumeDeckTestPreparation());
    CHECK(game.device->getEventReceiver()==&editor); CHECK(game.is_building);
    CHECK(editor.CaptureEditorDeck()==dirty); CHECK(editor.editorHistory.CanUndo());
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    auto rejectPreserves=[&](DeckTestEntryState state,const wchar_t* hint) {
        const auto before=editor.CaptureEditorDeck();
        const auto canUndo=editor.editorHistory.CanUndo();
        const auto dirtyBefore=editor.is_modified;
        const auto category=game.cbDBCategory->getSelected();
        const auto deck=game.cbDBDecks->getSelected();
        const std::wstring name=game.ebDeckname->getText();
        expectEntry(game,state,state!=DeckTestEntryState::Siding && state!=DeckTestEntryState::InactiveEditor,false,hint);
        button(game.btnTestDeck);
        CHECK(!editor.HasEditorSuspension()); CHECK(editor.CaptureEditorDeck()==before);
        CHECK(editor.editorHistory.CanUndo()==canUndo); CHECK(editor.is_modified==dirtyBefore);
        CHECK(game.cbDBCategory->getSelected()==category); CHECK(game.cbDBDecks->getSelected()==deck);
        CHECK(std::wstring(game.ebDeckname->getText())==name);
        CHECK(bytes("deck/undo-editor-integration.ydk")==deckFile);
    };

    mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,320,180);
    CHECK(editor.is_starting_dragging); const auto pendingDeck=editor.CaptureEditorDeck();
    const auto pendingEdit=editor.editorEditStart; const auto pendingPointer=editor.draging_pointer;
    rejectPreserves(DeckTestEntryState::Dragging,undo::DeckTestDraggingText);
    CHECK(editor.is_starting_dragging); CHECK(editor.editorEditStart==pendingEdit); CHECK(editor.draging_pointer==pendingPointer);
    CHECK(editor.CaptureEditorDeck()==pendingDeck); CHECK(key(irr::KEY_ESCAPE)); CHECK(editor.CaptureEditorDeck()==dirty);

    drag(320,180,300,640);
    const auto activeDeck=editor.CaptureEditorDeck(); const auto activeEdit=editor.editorEditStart; const auto activePointer=editor.draging_pointer;
    rejectPreserves(DeckTestEntryState::Dragging,undo::DeckTestDraggingText);
    CHECK(editor.is_draging); CHECK(editor.editorEditStart==activeEdit); CHECK(editor.draging_pointer==activePointer);
    CHECK(editor.CaptureEditorDeck()==activeDeck);
    captureEntryFrame(game,L"deck-test-entry-100-drag-disabled.png");
    CHECK(key(irr::KEY_ESCAPE)); CHECK(editor.CaptureEditorDeck()==dirty);
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    button(game.btnClearDeck); CHECK(game.wQuery->isVisible()); settleWindowShown(game,game.wQuery);
    rejectPreserves(DeckTestEntryState::BlockingDialog,undo::DeckTestDialogText);
    captureEntryFrame(game,L"deck-test-entry-100-dialog-disabled.png");
    button(game.btnNo); settleWindowFade(game,game.wQuery);
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);
    for(auto window : {game.wMessage,game.wBigCard}) {
        window->setVisible(true); rejectPreserves(DeckTestEntryState::BlockingDialog,undo::DeckTestDialogText);
        window->setVisible(false); expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);
    }
    auto* modal=game.env->addModalScreen(nullptr); game.env->setFocus(modal);
    rejectPreserves(DeckTestEntryState::BlockingDialog,undo::DeckTestDialogText);
    modal->setVisible(false); game.env->setFocus(nullptr); modal->remove();
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    editor.RefreshReadonly(0);
    rejectPreserves(DeckTestEntryState::ReadOnlyPack,undo::DeckTestPackText);
    captureEntryFrame(game,L"deck-test-entry-100-pack-disabled.png");
    editor.RefreshReadonly(2);
    editor.readonly=true; editor.showing_pack=false;
    rejectPreserves(DeckTestEntryState::ReadOnlyDeck,undo::DeckTestReadOnlyText);
    editor.readonly=false; expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    const bool deckEditVisible=game.wDeckEdit->isVisible();
    const bool filterVisible=game.wFilter->isVisible(); const bool sortVisible=game.wSort->isVisible();
    game.is_siding=true; game.wDeckEdit->setVisible(false); game.wFilter->setVisible(false); game.wSort->setVisible(false);
    game.btnSideOK->setVisible(true); game.btnSideShuffle->setVisible(true); game.btnSideSort->setVisible(true); game.btnSideReload->setVisible(true);
    rejectPreserves(DeckTestEntryState::Siding,undo::DeckTestSidingText);
    captureEntryFrame(game,L"deck-test-entry-100-siding-hidden.png");
    game.is_siding=false; game.wDeckEdit->setVisible(deckEditVisible); game.wFilter->setVisible(filterVisible); game.wSort->setVisible(sortVisible);
    game.btnSideOK->setVisible(false); game.btnSideShuffle->setVisible(false); game.btnSideSort->setVisible(false); game.btnSideReload->setVisible(false);
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);

    game.is_building=false; rejectPreserves(DeckTestEntryState::InactiveEditor,undo::DeckTestInactiveText);
    game.is_building=true; expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);
    game.wACMessage->setVisible(true); expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint); game.wACMessage->setVisible(false);

    const auto oldCategory=game.cbDBCategory->getSelected(); const auto oldDeck=game.cbDBDecks->getSelected();
    const bool categoryEnabled=game.cbDBCategory->isEnabled(); const bool deckEnabled=game.cbDBDecks->isEnabled();
    const bool manageEnabled=game.btnManageDeck->isEnabled();
    game.cbDBCategory->setSelected(-1); game.cbDBDecks->setSelected(-1);
    game.cbDBCategory->setEnabled(false); game.cbDBDecks->setEnabled(false); game.btnManageDeck->setEnabled(false);
    editor.readonly=false; editor.showing_pack=false;
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);
    button(game.btnTestDeck); CHECK(editor.HasDeckTestPreparation()); CHECK(editor.ResumeDeckTestPreparation());
    game.cbDBCategory->setSelected(oldCategory); game.cbDBDecks->setSelected(oldDeck);
    game.cbDBCategory->setEnabled(categoryEnabled); game.cbDBDecks->setEnabled(deckEnabled); game.btnManageDeck->setEnabled(manageEnabled);

    editor.Terminate(); CHECK(!game.btnTestDeck->isVisible()); CHECK(!editor.HasDeckTestPreparation());
    game.wMainMenu->setVisible(false); editor.Initialize();
    CHECK(editor.RestoreEditorDeck(clean)); editor.ResetEditorHistory();
    expectEntry(game,DeckTestEntryState::Ready,true,true,undo::DeckTestHint);
    std::cout << "PASS Task9 actual test-deck entry: native click, stale guards, pending/commit lifecycle and rendered states\n";
}
