#include "game.h"
#include "deck_manager.h"
#include "data_manager.h"
#include "test_support.h"
#include <iostream>
#include <windows.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>
using namespace ygo;
static void mouse(irr::EMOUSE_INPUT_EVENT kind, int x, int y) {
    irr::SEvent e{}; e.EventType = irr::EET_MOUSE_INPUT_EVENT;
    e.MouseInput.Event = kind; e.MouseInput.X = x; e.MouseInput.Y = y;
    mainGame->deckBuilder.OnEvent(e);
}
static bool key(irr::EKEY_CODE code, bool control = false) {
    irr::SEvent e{}; e.EventType = irr::EET_KEY_INPUT_EVENT;
    e.KeyInput.Key = code; e.KeyInput.PressedDown = true; e.KeyInput.Control = control;
    return mainGame->deckBuilder.OnEvent(e);
}
static void button(irr::gui::IGUIButton* control) {
    irr::SEvent e{}; e.EventType = irr::EET_GUI_EVENT;
    e.GUIEvent.Caller = control; e.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
    mainGame->deckBuilder.OnEvent(e);
}
static void change(irr::gui::IGUIComboBox* control) {
    irr::SEvent e{}; e.EventType = irr::EET_GUI_EVENT;
    e.GUIEvent.Caller = control; e.GUIEvent.EventType = irr::gui::EGET_COMBO_BOX_CHANGED;
    mainGame->deckBuilder.OnEvent(e);
}
static void drag(int x, int y, int toX, int toY) {
    mouse(irr::EMIE_MOUSE_MOVED,x,y);
    mouse(irr::EMIE_LMOUSE_PRESSED_DOWN,x,y);
    mouse(irr::EMIE_MOUSE_MOVED,toX,toY);
    CHECK(mainGame->deckBuilder.is_draging);
}
static std::string bytes(const char* path) {
    std::ifstream in(path, std::ios::binary); CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void signal(const char* name) { std::ofstream(name) << "ready"; }
static void waitFor(const char* name) {
    for(int i=0;i<500 && !std::filesystem::exists(name);++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(std::filesystem::exists(name));
}
int main(int argc, char** argv) {
    try {
        static Game game; mainGame = &game;
        CHECK(game.Initialize());
        game.wMainMenu->setVisible(false);
        game.cbDBCategory->clear();
        game.cbDBCategory->addItem(L"pack"); game.cbDBCategory->addItem(L"bot");
        game.cbDBCategory->addItem(L"deck"); game.cbDBCategory->setSelected(2);
        game.cbDBDecks->clear(); game.cbDBDecks->addItem(L"undo-editor-integration"); game.cbDBDecks->setSelected(0);
        game.deckBuilder.Initialize();
        auto& editor = game.deckBuilder;
        LFList unrestricted; editor.filterList = &unrestricted;
        const auto& data = dataManager.GetDataTable();
        const CardDataC *a = nullptr, *b = nullptr, *extra = nullptr;
        for(const auto& entry : data) {
            auto p = &entry.second;
            if((p->type & TYPE_TOKEN) || p->alias) continue;
            if(p->type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK)) extra = p;
            else if(!a) a = p; else b = p;
            if(a && b && extra) break;
        }
        CHECK(a && b && extra);
        const undo::DeckSnapshot initial{{{a->code,b->code,a->code},{extra->code,extra->code},{b->code}}};
        if(argc>1 && std::string(argv[1])=="--observe") {
            waitFor("editor-observer-ready");
            CHECK(editor.LoadEditorDeck(L"./deck/undo-editor-integration.ydk"));
            const auto memory=editor.CaptureEditorDeck();
            const auto disk=bytes("deck/undo-editor-integration.ydk");
            signal("editor-observer-loaded");
            waitFor("editor-observer-done");
            CHECK(editor.CaptureEditorDeck()==memory);
            CHECK(bytes("deck/undo-editor-integration.ydk")==disk);
            signal("editor-observer-passed");
            std::cout << "PASS second process memory and disk unchanged by first process undo\n";
            game.device->closeDevice(); return 0;
        }
        auto seed = [&] {
            game.wQuery->setVisible(false); game.wDeckManage->setVisible(false); game.wDMQuery->setVisible(false);
            game.wMessage->setVisible(false); game.wBigCard->setVisible(false); game.wACMessage->setVisible(false);
            game.env->setFocus(nullptr); game.is_siding = false; editor.readonly = false;
            editor.CancelEditorDrag(); CHECK(editor.RestoreEditorDeck(initial)); editor.ResetEditorHistory();
            editor.results = {b,extra,a}; game.scrFilter->setPos(0);
        };
        auto undoTo = [&](const undo::DeckSnapshot& before) {
            CHECK(editor.editorHistory.CanUndo()); CHECK(key(irr::KEY_KEY_Z,true));
            CHECK(editor.CaptureEditorDeck() == before);
        };
        seed();
        // An actual press/move/release used to append the middle card on invalid drop.
        drag(368,180,300,640); CHECK(deckManager.current_deck.main.size() == 2);
        mouse(irr::EMIE_LMOUSE_LEFT_UP,300,640);
        CHECK(editor.CaptureEditorDeck() == initial); CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.is_modified);
        // Escape and focus-loss cancellation preserve duplicates and the exact old position.
        for(auto xy : {std::pair<int,int>{368,180},{320,480},{320,580}}) {
            seed(); drag(xy.first,xy.second,300,640); CHECK(key(irr::KEY_ESCAPE));
            CHECK(editor.CaptureEditorDeck() == initial); CHECK(!editor.editorHistory.CanUndo());
            drag(xy.first,xy.second,300,640); editor.CancelEditorDrag();
            CHECK(editor.CaptureEditorDeck() == initial); CHECK(!editor.editorHistory.CanUndo());
        }
        // Incompatible main/extra destination rejects atomically.
        seed(); drag(368,180,320,480); mouse(irr::EMIE_LMOUSE_LEFT_UP,320,480);
        CHECK(editor.CaptureEditorDeck() == initial); CHECK(!editor.editorHistory.CanUndo());
        // Main to side, extra to side, side to main, and within-main order each consume one undo.
        for(auto route : {std::array<int,4>{368,180,320,580},{320,480,320,580},{320,580,320,180},{368,180,320,180}}) {
            seed(); drag(route[0],route[1],route[2],route[3]); mouse(irr::EMIE_LMOUSE_LEFT_UP,route[2],route[3]);
            CHECK(editor.CaptureEditorDeck() != initial); undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
        }
        // Right delete and middle copy are complete operations in all three areas.
        for(auto xy : {std::pair<int,int>{320,180},{320,480},{320,580}}) {
            seed(); mouse(irr::EMIE_MOUSE_MOVED,xy.first,xy.second); mouse(irr::EMIE_RMOUSE_LEFT_UP,xy.first,xy.second);
            undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
            seed(); mouse(irr::EMIE_MOUSE_MOVED,xy.first,xy.second); mouse(irr::EMIE_MMOUSE_LEFT_UP,xy.first,xy.second);
            undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
        }
        seed(); mouse(irr::EMIE_MOUSE_MOVED,820,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,820,180);
        undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
        seed(); drag(820,180,500,180); mouse(irr::EMIE_LMOUSE_LEFT_UP,500,180); undoTo(initial);
        seed(); drag(368,180,820,180); mouse(irr::EMIE_LMOUSE_LEFT_UP,820,180); undoTo(initial);
        seed(); drag(368,180,320,580); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,580); undoTo(initial);
        // Full destination and card-copy limit failures create no history.
        seed(); auto full = initial; full[2].assign(15,b->code); CHECK(editor.RestoreEditorDeck(full)); editor.ResetEditorHistory();
        drag(368,180,320,580); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,580);
        CHECK(editor.CaptureEditorDeck() == full); CHECK(!editor.editorHistory.CanUndo());
        seed(); auto limited = initial; limited[0].push_back(a->code); CHECK(editor.RestoreEditorDeck(limited)); editor.ResetEditorHistory();
        mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_MMOUSE_LEFT_UP,320,180);
        CHECK(editor.CaptureEditorDeck() == limited); CHECK(!editor.editorHistory.CanUndo());
        // Sort, no-op sort, deterministic shuffle, and confirmed-only clear.
        seed(); button(game.btnSortDeck); auto sorted=editor.CaptureEditorDeck(); CHECK(sorted != initial);
        button(game.btnSortDeck); undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
        seed(); editor.rnd.seed(2); button(game.btnShuffleDeck);
        if(editor.CaptureEditorDeck()!=initial) undoTo(initial); else CHECK(!editor.editorHistory.CanUndo());
        seed(); button(game.btnClearDeck); CHECK(editor.CaptureEditorDeck()==initial); CHECK(!editor.editorHistory.CanUndo());
        button(game.btnNo); game.wQuery->setVisible(false); CHECK(!editor.editorHistory.CanUndo());
        button(game.btnClearDeck); button(game.btnYes); game.wQuery->setVisible(false);
        CHECK(deckManager.current_deck.main.empty() && deckManager.current_deck.extra.empty() && deckManager.current_deck.side.empty());
        undoTo(initial);
        // Add -> sort -> cross-zone -> three undos return every full snapshot.
        seed(); mouse(irr::EMIE_MOUSE_MOVED,820,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,820,180); auto added=editor.CaptureEditorDeck();
        button(game.btnSortDeck); sorted=editor.CaptureEditorDeck(); CHECK(sorted!=added);
        drag(320,180,320,580); mouse(irr::EMIE_LMOUSE_LEFT_UP,320,580);
        undoTo(sorted); undoTo(added); undoTo(initial); CHECK(!editor.editorHistory.CanUndo());
        // Keyboard is handed back to each real text widget; button and key share guards.
        seed(); mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180); auto edited=editor.CaptureEditorDeck();
        for(auto text : {game.ebCardName,game.ebDeckname}) {
            game.env->setFocus(text); CHECK(!key(irr::KEY_KEY_Z,true)); CHECK(editor.CaptureEditorDeck()==edited);
            editor.RefreshEditorUndo(); CHECK(!game.btnUndoDeck->isEnabled());
        }
        game.env->setFocus(nullptr);
        game.wQuery->setVisible(true); CHECK(!key(irr::KEY_KEY_Z,true)); button(game.btnUndoDeck); CHECK(editor.CaptureEditorDeck()==edited); game.wQuery->setVisible(false);
        editor.readonly=true; CHECK(!key(irr::KEY_KEY_Z,true)); editor.readonly=false;
        game.is_siding=true; CHECK(!key(irr::KEY_KEY_Z,true)); game.is_siding=false;
        drag(320,180,300,640); CHECK(!key(irr::KEY_KEY_Z,true)); CHECK(key(irr::KEY_ESCAPE)); CHECK(editor.CaptureEditorDeck()==edited);
        button(game.btnUndoDeck); CHECK(editor.CaptureEditorDeck()==initial); CHECK(!editor.editorHistory.CanUndo());
        editor.RefreshEditorUndo(); CHECK(!game.btnUndoDeck->isEnabled());
        // Restore bypasses present limits, resolves ALL ids first, and failed undo retains history.
        seed(); auto oversized=initial; oversized[0].assign(75,a->code); CHECK(editor.RestoreEditorDeck(oversized)); CHECK(editor.CaptureEditorDeck()==oversized);
        seed(); auto invalid=initial; invalid[2].push_back(0); editor.editorHistory.Record(invalid,initial);
        CHECK(!editor.UndoEditorEdit()); CHECK(editor.CaptureEditorDeck()==initial); CHECK(editor.editorHistory.CanUndo()); CHECK(!editor.editorHistoryValid);
        // Disk changes only on explicit save; successful same-file save retains undo and changes dirty baseline.
        seed(); button(game.btnSaveDeck); const auto originalFile=bytes("deck/undo-editor-integration.ydk");
        if(argc>1 && std::string(argv[1])=="--with-observer") {
            signal("editor-observer-ready"); waitFor("editor-observer-loaded");
            mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180); undoTo(initial);
            button(game.btnSortDeck); undoTo(initial);
            signal("editor-observer-done"); waitFor("editor-observer-passed");
        }
        mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180); edited=editor.CaptureEditorDeck();
        CHECK(editor.is_modified); CHECK(bytes("deck/undo-editor-integration.ydk")==originalFile);
        button(game.btnSaveDeck); CHECK(!editor.is_modified); CHECK(editor.editorHistory.CanUndo()); const auto savedFile=bytes("deck/undo-editor-integration.ydk"); CHECK(savedFile!=originalFile);
        undoTo(initial); CHECK(editor.is_modified); CHECK(bytes("deck/undo-editor-integration.ydk")==savedFile);
        button(game.btnSortDeck); undoTo(initial); CHECK(bytes("deck/undo-editor-integration.ydk")==savedFile);
        // Saving during an unfinished drag must not persist the provisional pop.
        seed(); drag(368,180,300,640); button(game.btnSaveDeck);
        CHECK(editor.CaptureEditorDeck()==initial); CHECK(!editor.is_draging); CHECK(!editor.editorHistory.CanUndo());
        // Failed save and save-as preserve dirty, history, memory and selection.
        mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180); edited=editor.CaptureEditorDeck();
        const bool dirtyBefore=editor.is_modified;
        const auto attributes=GetFileAttributesW(L"./deck/undo-editor-integration.ydk");
        CHECK(attributes!=INVALID_FILE_ATTRIBUTES);
        CHECK(SetFileAttributesW(L"./deck/undo-editor-integration.ydk",attributes|FILE_ATTRIBUTE_READONLY));
        button(game.btnSaveDeck);
        const bool failedSaveKeptState=editor.is_modified==dirtyBefore && editor.editorHistory.CanUndo() && editor.CaptureEditorDeck()==edited;
        CHECK(SetFileAttributesW(L"./deck/undo-editor-integration.ydk",attributes));
        CHECK(failedSaveKeptState);
        game.cbDBDecks->addItem(L"missing-parent/forbidden"); game.cbDBDecks->setSelected(1);
        button(game.btnSaveDeck); CHECK(editor.is_modified==dirtyBefore); CHECK(editor.editorHistory.CanUndo()); CHECK(editor.CaptureEditorDeck()==edited);
        game.cbDBDecks->setSelected(0); game.ebDeckname->setText(L"missing-parent/new"); auto count=game.cbDBDecks->getItemCount();
        button(game.btnSaveDeckAs); CHECK(game.cbDBDecks->getSelected()==0); CHECK(game.cbDBDecks->getItemCount()==count); CHECK(editor.is_modified==dirtyBefore); CHECK(editor.editorHistory.CanUndo());
        CHECK(!editor.LoadEditorDeck(L"missing-parent/no-such-deck")); CHECK(editor.CaptureEditorDeck()==edited); CHECK(editor.editorHistory.CanUndo()); CHECK(editor.is_modified==dirtyBefore);
        // Failed real combo selection restores the visible current deck selection too.
        game.chkIgnoreDeckChanges->setChecked(true);
        game.cbDBDecks->setSelected(1); change(game.cbDBDecks);
        CHECK(game.cbDBDecks->getSelected()==0); CHECK(editor.CaptureEditorDeck()==edited); CHECK(editor.editorHistory.CanUndo());
        game.chkIgnoreDeckChanges->setChecked(false);
        // New-file failure preserves the edited deck; successful new clears history.
        button(game.btnNewDeck); game.ebDMName->setText(L"missing-parent/new"); button(game.btnDMOK);
        game.wDMQuery->setVisible(false); CHECK(editor.CaptureEditorDeck()==edited); CHECK(editor.editorHistory.CanUndo());
        std::filesystem::remove("deck/undo-editor-new.ydk");
        game.lstCategories->setSelected(2);
        button(game.btnNewDeck); game.ebDMName->setText(L"undo-editor-new"); button(game.btnDMOK);
        game.wDMQuery->setVisible(false);
        CHECK(editor.CaptureEditorDeck()==undo::DeckSnapshot{}); CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.is_modified);
        // Successful reload and new-name save-as reset the editor session.
        CHECK(editor.LoadEditorDeck(L"./deck/undo-editor-integration.ydk")); CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.is_modified);
        mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180);
        game.ebDeckname->setText(L"undo-editor-integration-copy"); button(game.btnSaveDeckAs);
        CHECK(!editor.editorHistory.CanUndo()); CHECK(!editor.is_modified); CHECK(std::wstring(game.cbDBDecks->getText())==L"undo-editor-integration-copy");
        seed(); mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180);
        button(game.btnLeaveGame); button(game.btnNo); game.wQuery->setVisible(false); CHECK(editor.editorHistory.CanUndo()); CHECK(game.is_building);
        button(game.btnLeaveGame); button(game.btnYes); CHECK(!game.is_building); CHECK(!editor.editorHistory.CanUndo());
        game.wMainMenu->setVisible(false); editor.Initialize(); seed();
        mouse(irr::EMIE_MOUSE_MOVED,320,180); mouse(irr::EMIE_RMOUSE_LEFT_UP,320,180); CHECK(editor.editorHistory.CanUndo());
        game.LoadExpansions(); CHECK(!game.is_building); CHECK(!editor.editorHistory.CanUndo());
        CHECK(editor.results.empty()); CHECK(editor.draging_pointer==nullptr); CHECK(editor.hovered_seq==-1);
        std::cout << "PASS E2/E3/E4 actual Game + DeckBuilder::OnEvent integration; visual GUI approval still pending\n";
        game.device->closeDevice();
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
