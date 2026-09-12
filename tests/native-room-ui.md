# Native room input and presentation regression

Run from the source checkout after building the Release client:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-RoomNativeUI.ps1 -Scenario all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-DuelUndoShortcut.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-ReplaySaveMainLoop.ps1 -Scenario edit-save
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-ReplaySaveMainLoop.ps1 -Scenario cancel
```

The native UI harness runs the actual `Game::MainLoop`, `RoomClient` receiver,
legacy message handler, Irrlicht controls and Windows mouse events. Incoming
server packets are controlled in the test; the frame/action signals retain their
production defaults. Each run owns its hidden window and isolated writable
runtime. It does not start or manipulate the installed game. Shared native
objects are hashed before/after linking; the executable, test, driver and private
transport configuration object have a binding receipt beside the run logs.

## Behavior covered

- `confirm`: a real `MSG_CONFIRM_CARDS` waits in the original handler on
  `actionSignal`; the native confirm click wakes it while response submissions
  are paused.
- `fade`: a native YES click begins a deferred response fade. A later time
  packet invalidates the captured prompt token. The old fade finishes and
  disappears without sending a response for the new prompt.
- `resize`, `clock`: an ordinary response gate must not stop native resize or
  the visible countdown. A real Preparing status still freezes resize,
  countdown and response fade during the restore transaction.
- `early-status`: a YES/NO prompt arrives before its boundary Status. Clicking
  early must leave that choice intact. After Status, a new native click sends
  exactly one response with the original session, epoch and prompt identity.

The pre-existing shortcut harness additionally covers text/modal ownership,
held-key repeats, native consent approve/decline, transaction replacement during
a held click, stale displayed consent, obscured controls and restored sizes.

## 2026-09-12 evidence

- Before the UI changes, actual MainLoop regressions failed for confirmation,
  resize and countdown in `out/room-native-ui-233e4832dbcf415698367760c0385ffd`.
  The corrected fade regression failed for its intended frozen-fade condition
  in `out/room-native-ui-afecd328de2f406c8fa0dee4543048f4`.
- Merely opening all UI on `PresentationFrozen == false` exposed the early
  choice issue: the query disappeared before Status. The regression failed in
  `out/room-native-ui-8e1478052acb4e9abf5d8352b792ee2d`.
- Native focus tracing showed that local confirmation also needs Irrlicht's
  focus-loss notification from the previous control. Swallowing it cancels the
  focus change and prevents the native button from clicking. Focus/hover
  notifications now retain their original dispatch.
- All five native UI scenarios passed in
  `out/room-native-ui-7c6ec52c3d0a45409c5422c14337575e`.
- The original shortcut/consent suite passed in
  `out/duel-shortcut-17dea62f81e1441b9a52b831e374a6e8`.
- Actual MainLoop replay filename editing, save, replay reopening and return to
  the menu passed in `out/replay-save-mainloop-6533f3f2cb3949e0995918e8e3472fa6`.
  Cancel with no replay file written and normal menu return passed in
  `out/replay-save-mainloop-9fdeffcf3b9e4ffcb17ff0131942d21e`.

These checks cover the input/presentation boundary on this computer. They do
not replace complete duel integration or acceptance on two physical devices.
