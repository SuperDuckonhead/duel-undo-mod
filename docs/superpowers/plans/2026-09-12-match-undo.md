# Match undo implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Work in the existing dedicated source checkout; installed runtime files are changed only after verified packaging.

**Goal:** Support native two-player Match rooms with independent undo history in each unfinished game.

**Architecture:** Keep native score, siding validation, readiness and next-game turn choice. Retain the old per-game host state for authenticated final replay/siding packets, then replace it before the next game starts. A host-authenticated `RoundStart` advances the existing room epoch, clears client per-game state and fences every old gameplay/undo input. No undo crosses a game boundary.

**Tech stack:** Existing C++17/ocgcore/Irrlicht/libevent, Windows PowerShell 5.1, CMake and native Game integration harnesses.

**Spec:** `openspec/changes/duel-undo-mod/design.md` section 14 and the Match requirement in `specs/duel-undo/spec.md`.

## Constraints

- User explicitly approved implementation of the discussed Match scope on 2026-09-12; no additional design approval is pending.
- PvP consent LAN and explicit loopback-free rooms expose native Match. The existing AI menu starts Single games and remains unchanged; artificial bot+Match configurations must fail explicitly.
- Reuse native Match score/win/draw/timeout/turn-choice/siding paths. Keep Single, practice, AI, original card editing, Ctrl+Z and replay behavior.
- Each game gets a fresh initial deck/seed/core/history/UI snapshots and timer state. Keep match score and sided decks outside undo history. Disable undo after win, during siding and after the match ends; no side-editor undo or cross-game rollback.
- Retain same room session and advance epoch monotonically across undo commits and game boundaries. Never reset epoch to zero for the second game.
- Keep human CTOS_UPDATE_DECK and CTOS_TP_RESULT native. RoundStart travels only server-to-client on authenticated ordered TCP. Card compatibility and host frozen resources retain the native-network v2 design; restore profile increments to 2 so older clients cannot silently join.
- Preserve two-party restore/abort/old-input/privacy guarantees. No remote physical-device acceptance claims or GitHub release publication in this task.

## Task 1 — Wire and authoritative host

Files: `undo/protocol.{h,cpp}`, `undo/room_wire.{h,cpp}`, `undo_duel.{h,cpp}`; minimal `single_duel` hook only if native reuse requires it; tests `room_wire_tests.cpp`, `protocol_tests.cpp`, new `match_duel_tests.cpp/.cmake`.

Interfaces:

```cpp
// WireKind::RoundStart = 15; RestoreProfileVersion = 2.
struct RoundTransition { uint64_t epoch; uint8_t number; };
Envelope EncodeRoundStart(const SessionId&, uint64_t previousEpoch,
                          uint64_t nextEpoch, uint8_t number);
RoundTransition DecodeRoundStart(const Envelope&);
// Key = {roomSession, previousEpoch, 0, 0, zeroDigest};
// payload = 8-byte LE nextEpoch + one-byte number (2 or 3).
// nextEpoch must equal previousEpoch+1 without overflow.
```

- [x] Add failing codec cases for roundtrip, wrong kind/key, malformed size, round bounds, non-increasing epoch and overflow. Verify prior restore-profile peers are incompatible.
- [x] Implement the strict codec and profile bump. Keep Hello 99-byte layout and envelope framing unchanged.
- [x] Add real host regressions: win/surrender/timeout scoring, legal and illegal siding, second/third game and seat change, independent seeded deck construction, per-game replay, no-history at game start, consent/commit/abort in game two, old epoch responses/requests ignored.
- [x] Record existing host failures. Implement `Impl` construction with epoch argument; replace per-game `Impl` at valid next `TPResult`, copying only room config/session/send and fixed participant/departure identities. Set incoming streams to installed epoch.
- [x] Export final replay before clearing Match-only history. Preserve old core/epoch until native side/turn packets are delivered; send RoundStart before new game MSG_START. Route surrender win through native Analyze for score/loser choice; keep native timer and end processing.
- [x] Run host, native timer, pending response, privacy, replay and AI regressions.

## Task 2 — Client lifecycle and native controls

Files: `room_client.cpp` (header only if needed), `duelclient.cpp` only if dispatch requires it; new `tests/room_match_client.h` included by `room_client_integration_tests.cpp`.

Consumes Task 1 RoundStart codec. First game is implicitly number 1, epoch 0. After normal win, record game-ended state; allow authenticated native replay, CHANGE_SIDE, WAITING_SIDE, ERROR_MSG, DUEL_START and SELECT_TP lifecycle packets in the correct terminal/siding phases. Full STOC_DUEL_END stays permanently terminal.

- [x] Add failing actual RoomClient/native-handler tests: win/replay→siding error/correction→native start/turn choice→RoundStart→new MSG_START/prompt; stale/manual/queued confirmation/request/consent excluded; early/duplicate/wrong-session/wrong-epoch/out-of-order controls cannot reopen a game.
- [x] Accept RoundStart only after win, CHANGE_SIDE and native DUEL_START, before match end and with exact next round/epoch. Under the Game mutex clear prepared models, visible history, context/response buffers; under the publication mutex clear commands, pending requests, consent and response flags. Reset stream to new epoch and keep inputs paused until the new authoritative prompt/status.
- [x] Preserve original side editor and next-game UI dispatch; no synthetic gameplay processing or disabling normal frame/action waits. Round-end/transition must not leave a stale fade or selection.
- [x] Run client lifecycle, shortcut/consent, timer-before-prompt, native UI and terminal replay regressions.

## Task 3 — Expose Match and full real pairs

Files: `menu_handler.cpp`, `netserver.cpp`, `undo/room_policy.h` where needed; `tests/room_policy_tests.cpp`, `room_client_integration_tests.cpp`, `room_client_pair.h` or dedicated Match pair fixture, `tools/Test-RoomClientIntegration.ps1` if a new switch is needed.

- [x] Replace the existing expected-Match-rejection regressions with real admission/start tests, proving the old menu and host guards fail first. Keep Tag/observer limits and reject bot+Match explicitly.
- [x] Remove only the human Match guards, retaining the original combo box, host rule options and native packet dispatch.
- [x] Run two actual Game processes through 2:0 and 2:1 matches, legal/illegal siding, second-game first/second seat changes, undo consent rejection/success/failure and continued play in later games. Verify old-game inputs are rejected and each saved replay represents one final game branch.
- [x] Verify actual native frame/action waits around win/save/side/next-game controls, including Ctrl+Z disabled in siding; rerun Single timed/free pair, AI and single-practice/replay.

## Task 4 — Review, delivery and install

Files: docs, spec/tasks and a new Match verification record; existing package tools unchanged unless a verified defect requires a fix.

- [x] Independent review of round boundaries, original Match semantics, input identities, resource/card privacy and UI lifecycle; resolve blocking findings.
- [x] Run applicable native/client/pair regressions with correct fixtures; validate OpenSpec and diff. Record known environment limitations without marking physical-device acceptance complete.
- [ ] Commit tested source; fresh short-path clone, offline verified dependency bootstrap, complete Release build, provenance and runtime startup verification.
- [ ] Validate exact ZIP install/uninstall/restore in an owned fixture. Back up installed 2187cb3 files from their independently verified package, install only the new manifest files when closed, verify original/shared/config files preserved. Deliver same ZIP for both computers and record exact source/hash.

## Execution decisions

Use the existing clean dedicated `codex/duel-undo` checkout at baseline `f4bf0efc9ef5cd34261a2e362ead2558ed0e1d0a`, separate from runtime. This continues the established workspace choice. Keep all previous release/build/backup evidence. A fresh release checkout validates the final package.
