# Editor deck-test session implementation plan

This implements OpenSpec `deck-test-edit-mode` tasks 2.3–2.7 from
`F:/MyCardLibrary/ygopro/openspec/changes/deck-test-edit-mode/`.
The existing isolated branch and uncommitted upload work are the starting point.

The deliverable is a usable ordinary test duel from the editor, including return
and retry. Full-pool expansion, its switch, and trial-card collection remain in
their later OpenSpec tasks.

1. Preserve and verify the existing immutable memory upload. Exercise the real
   client/server path with ordered unsaved main/extra cards; preserve side cards,
   source files, preferences, and editor undo. Use the existing upload harness.
2. Add `CaptureDeckTestRoomConfig` in `undo/room_config.*` and test exact
   `Deck=MokeyMokeyKing` selection, missing configuration, fixed rules, and
   loopback-only admission. Override `UndoDuel::StartDuel` for this configuration
   alone to start host first through the existing core/replay initialization.
3. Add `DeckBuilder::PollDeckTest` in a focused session source, called by the main
   UI loop. Freeze resources asynchronously, create the owned loopback listener,
   connect/upload/ready automatically, hand off the retained editor, and start.
   Connection completion is observed before destroying duel UI or restoring the
   editor. Only successfully acquired resources may be stopped. Startup failures
   and normal ends use the same restore path.
4. Add a real Game/AI integration harness before implementing the lifecycle. Run
   it against the incomplete entry and observe failure, then verify successful
   first turn, unchanged hand order, surrender/return, repeat with latest edits,
   unsupported input, missing bot, retry, and unchanged source/config files.
5. Build Release client, run focused native/session/editor regression checks,
   independently review the changes, and record evidence before checking off
   OpenSpec tasks. Provide a runnable local build with the matching private bot.

Validation commands (from this worktree):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target Tests -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target Client -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-RoomClientIntegration.ps1 -Configuration Release -DeckTestUpload
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-DeckTestSession.ps1 -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-EditorIntegration.ps1 -Configuration Release
& ./out/tests/Release/deck_test_upload_tests.exe F:/MyCardLibrary/ygopro
```

Verified on 2026-09-19:

- Release client and matching private bot built; client PE imports use Windows
  system libraries only. Native CTest suite: 24/24 passed.
- Real host upload/fixed-first tests passed, including same-seed normal-core
  comparison and a real core-creation failure with fatal-status publication.
- Real TCP memory upload, full editor integration, and the ordinary RoomClient
  allocation/abort/commit/resume/token regression passed.
- Eight actual editor/AI session scenarios passed. The in-duel failure scenario
  kills only the current test process's unique staged bot, then clicks the real
  End Phase button to trigger the next communication. Automatic restoration and
  the subsequent retry both pass. This does not add an idle-process watchdog.
- Independent lifecycle review found no remaining blocker. OpenSpec tasks
  2.3–2.7 are complete; overall change progress is 14/76, not full delivery.

Session evidence and native screenshots are under
`out/deck-test-session-0b8c824017e6446a8cbbebda59aa7b72/`.
Build/regression logs are `out/deck-test-client-final-build.log`,
`out/deck-test-final-native-tests.log`, and `out/deck-test-final-editor-tests.log`.
The runnable preview is `F:/MyCardLibrary/ygopro/deck-test-preview/ygopro-undo.exe`
(SHA256 `5512D9E6751A1BAF6DBFDF24B83A44ECDE57EF0AE76F8714A1F7A17DD5A22F49`).
