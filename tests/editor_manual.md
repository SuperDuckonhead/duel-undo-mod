# Editor integration and manual acceptance ledger

Date: 2026-09-10. Isolated repository: `F:/MyCardLibrary/ygopro/dev/duel-undo-mod`.

## Evidence classification

`tools/Test-EditorIntegration.ps1` compiles the actual test main against freshly built full-client objects and libraries, replacing only the normal executable entry point. It calls real `Game::Initialize`, initializes Irrlicht/OpenGL, creates the actual GUI controls, and sends mouse/key/GUI events to real `DeckBuilder::OnEvent`. It is an actual in-process integration test, not a vector-assignment simulation and not visual/manual GUI approval. Test setup assigns initial decks; all tested user mutations use the production event handler. Direct adapter checks are explicitly listed separately below.

The executable initializes at 1024x640 with OpenGL 4.6 on NVIDIA GeForce RTX 5070 Ti. All file writes are under `out/baseline-runtime` or `out/editor-integration`; the runtime's original assets are read through the already prepared asset links. Dedicated files use `undo-editor-*` names. No original installed deck/config/replay is written.

## Baseline failures observed

Before production implementation, the first actual event test pressed the middle main-deck card at (368,180), moved to (300,640), and released at that invalid location. The initial main order was `[a,b,a]` with duplicate extra cards and a populated side deck. The test confirmed the drag had started and main count was temporarily 2, then failed `deckManager.current_deck.main == before.main`: the original fallback appended the removed card. This failure came from initialized Game/OnEvent, not simulated assignments.

Additional red tests during implementation found:

- Failed real combo selection preserved memory but left the UI selected on the missing deck: `game.cbDBDecks->getSelected()==0` failed (`out/editor-integration-extra-red.log`). The adapter now rolls back the current selection.
- Saving during an unfinished drag persisted the provisional pop: `editor.CaptureEditorDeck()==initial` failed (`out/editor-drag-save-red.log`). Save/save-as/clear now cancel the gesture first; loads also end a pending gesture.
- Focus predicate test initially failed to compile because `undo/editor_input.h` did not exist. The completed predicate test passes all six guard cases.

## Automated acceptance

| Task | Verified with actual event integration |
| --- | --- |
| E2 | Full ordered main/extra/side and duplicate cards; invalid destination and incompatible main-to-extra drop restore exact old positions; Escape cancels each zone; main-to-side, extra-to-side, side-to-main, within-main reorder; right-delete/middle-copy in all zones; search right-add/search drag-in/drag discard; drag right-click cross-zone; full destination and copy limit rejection; sort and no-op sort; shuffle/no-op history; clear only after Yes and not after No; add → sort → cross-zone → three undos restores all intermediate snapshots. |
| E3 | Actual Ctrl+Z handler returns false for both real search and deck-name edit-box focus; button and shortcut share the same method; modal, dragging, readonly, siding, and empty-history guards; button enabled state; multiple undos; failed restore disables undo and preserves history/current memory. |
| E4 | Same-file successful save retains history, changes dirty baseline, and undo becomes dirty; undo/sort/undo leave exact saved file bytes unchanged; saving an unfinished drag cancels it first; genuine Windows readonly-file save failure preserves history/dirty; missing-directory save/save-as failures preserve content/history/selection; failed load and failed combo switch retain state; failed new preserves state; successful new/load/reload/new-name save-as clear history; cancelled exit retains history and confirmed exit clears it. |
| Process isolation | A second initialized Game process loads the same saved file, records its own memory/file contents, waits while A edits/undoes/sorts/undoes, and confirms both its memory and shared file bytes are unchanged. |
| Database boundary | Calling actual `Game::LoadExpansions` during editing ends the editor session first and clears undo history/results/drag pointer/hover selection. Two command-line extra-DB load sites also end the editor before `LoadDB`. |

Additional direct adapter checks (not claimed as UI tests): oversized 75-card snapshot restores without normal limits; missing ID in the last zone fails atomically and retains the candidate undo history; `CancelEditorDrag` exactly restores each zone. The application MainLoop calls this same cancellation method when `device->isWindowActive()` becomes false.

Counts remain derived from `current_deck` by the existing drawing path; successful undo clears hover/click/drag information and recomputes hover, dirty and button state.

## Commands and latest results

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-Client.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File tools/Test-EditorIntegration.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File tools/Build.ps1 -Target Tests -Configuration Debug
```

- Product build: success, `out/client/Release/ygopro-undo.exe`; `out/editor-client-release.log`.
- Actual client event integration and second process: both PASS; `out/editor-integration.log`, observer stdout/stderr under `out/editor-integration/Release`.
- Debug CTest: 8/8 passed; `out/editor-headless-debug.log`. Editor history and editor input are two of these tests.
- Final dedicated `.ydk` SHA-256 for the recorded run: `54FC14AABADC092C10D794ADBAF90F4494DEE0F72181C51878EAC75CF3E3A0FC`. The integration executable compares complete file bytes across every undo-only interval, which also guarantees their SHA-256 is unchanged. Explicit save changes the file.
- Optional CTest registration: configure `-DUNDO_RUN_EDITOR_INTEGRATION=ON` after building the Release client and preparing the runtime; `editor_client_integration` executes the same two-process test.

## Pending actual visual/manual acceptance

The computer-use kernel failed before app interaction in this environment. No visual pass is claimed. Before release, manually confirm:

1. The 撤回 button and tooltip are legible at supported window scales, deck counts and hover card update immediately, and it fits beside 清空.
2. Real OS focus loss (Alt+Tab/window deactivation) cancels drag in the MainLoop, preserving exact old positions. The cancellation function itself is automated; the real OS focus transition is not.
3. Search/deck-name text keeps the text widget's native Ctrl+Z behavior, modal windows prevent deck undo, and side-decking displays no active deck undo entry.
4. Repeat add → sort → move → three undos, then save → undo with a dedicated deck through real mouse/keyboard input.
5. Navigate editor → main menu → single duel → editor and confirm editor undo remains separate from the future duel undo interface. Editor exit/reentry/database reset is automated; the single-duel navigation is pending the duel feature and visual gate.

File-management delete/move/rename undo and redo remain outside the requested editor feature.