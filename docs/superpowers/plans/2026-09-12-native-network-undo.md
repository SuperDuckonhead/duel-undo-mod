# Native-network undo implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Work in the existing isolated source checkout; the installed runtime is not a build workspace.

**Goal:** Preserve native normal-duel response and timer behavior while adding only the compatibility, history and coordinated restore extensions required by undo.

**Architecture:** The host remains authoritative. Clients exchange explicit compatible message/restore capabilities and the logical card data used by native selection UI, without requiring matching executable bytes, scripts or all restriction lists. The host freezes its resources for execution/rebuild; original response and timer methods own ordinary play, and undo adds acceptance hooks, an input-generation fence and a transactional replacement.

**Tech Stack:** C++17, existing YGOPro/ocgcore, Irrlicht, libevent, adapted WindBot, Windows PowerShell 5.1, existing CMake tests.

**Spec:** `openspec/changes/duel-undo-mod/design.md`, section 13; `specs/duel-undo/spec.md` networking requirements.

## Constraints

- Keep the original menu, card selection, mouse event delivery, guessing, turn choice, room deck options and finish/replay UI.
- Do not require matching complete executable, effect-script collection or restriction-list collection to join.
- Preserve card-data compatibility for native client-side selection; this does not validate whole database file bytes or translated strings.
- Freeze the host's full duel resources and retain exact replay/rebuild validation. Remote clients must not freeze scripts merely to join.
- Require explicit compatibility versions. Reject older incompatible undo handshakes, not silently changing the meaning of v1.
- Preserve consent, history generation/prompt identities, rollback, hidden information filtering and adapted AI restoration.
- Keep ordinary timing/response state in native SingleDuel; pause only for restore/AI readiness, then resume the same authoritative state. Do not create a second ordinary countdown.
- No public-server adaptation, matchmaking rewrite, resource copying or actual two-device acceptance claims.
- Preserve the installed package and resources until tests and a fresh candidate build pass.

## Task 1: Host/client compatibility separation

Files: `undo/protocol.{h,cpp}`, `undo/room_config.{h,cpp}`, `duelclient.cpp`, `data_manager.{h,cpp}` / `resource_view.{h,cpp}` where needed; protocol/admission/resource scope tests.

Interfaces: `CaptureRoomConfig` remains the host snapshot API. Add `CaptureClientRoomConfig(DataManager&, RoomMode)` returning a `RoomConfig` containing only its compatible capability. Its `resources` is empty. Host resources remain available exclusively through `RoomConfig::resources`; never equate a client compatibility digest with the host rebuild digest. The bot's engine/resource identity remains the host's local frozen identity.

- [x] Add regressions that compare identical logical card data with different scripts and restriction lists, and a client capture whose script path is missing/malformed; these should successfully negotiate. A changed numeric card row and changed compatibility version must fail.
- [x] Run the focused tests and record the expected old-policy failures.
- [x] Implement a versioned compatibility contract using explicit game/restore identities plus logical card data. Keep host full resource capture; switch joining clients to `CaptureClientRoomConfig`. Update every consumer and fixture to distinguish compatibility and reconstruction identity.
- [x] Run protocol, admission, server admission, resource scope, rebuild and replay tests; confirm practice and legacy replay validation remain strict.

## Task 2: Native response and timer integration

Files: `single_duel.{h,cpp}`, `undo_duel.{h,cpp}`, host/timing/response tests.

Interfaces: Native `SingleDuel::GetResponse`, `WaitforResponse`, `TimeConfirm`, `TimerTick` remain the ordinary response/timer authority. Add protected hooks only where needed to submit to the owned core and record accepted/rejected responses without copying these methods. Adapted undo core lifetime/rebuild ownership remains in the existing undo layer.

- [x] Add behavior regressions for native time-confirm-before-response ordering, native clock consumption/new-turn reset, invalid `MSG_RETRY` history rejection, and the same behavior before/after undo.
- [x] Record baseline failures demonstrating the native state/response path is bypassed.
- [x] Route validated human/automatic/bot responses through the native response method; keep epoch/prompt validation at the extension boundary. Connect native waiting/confirmation/countdown to undo pause/resume and checkpoint capture. Remove duplicate normal timer accounting.
- [x] Verify rejected, timed-out and committed restore restore the proper native time/action state; old inputs and time confirmations remain invalid. Keep AI readiness holds and candidate core isolation.
- [x] Run focused real host tests including ordinary play, restore, consent rejection, failed prepare, concurrent input and AI.

## Task 3: Client ordinary flow and complete integration

Files: `room_client.cpp`, `duelclient.h`, `netserver.{h,cpp}` only when necessary; `tests/room_client_timer.h`, integration tests.

- [x] Review normal message interception and preserve existing native message handlers. Transport identity is checked before dispatch; it must not introduce a second timing gate or block original controls outside actual restore.
- [x] Verify chat/room messages do not unnecessarily depend on a duel prompt. Preserve state-bound surrender and time-confirm identity.
- [x] Run actual Game single/replay, normal consent/free/timed pairs and adapted AI integrations. Test first operation, effect/chain responses, reject/accept/abort undo, repeated undo/new branch, and normal finish/save. Fix failures at their source and retain meaningful regressions.

## Task 4: Review and build

Files: docs, specification/tasks, regression evidence, existing release tooling.

- [x] Independent review of the compatibility scope, native response/timer path and failure/old-input protections; resolve blocking findings.
- [x] Run the complete native test suite and appropriate package/uninstaller checks; record exact commands and results without claiming unavailable physical-device verification.
- [ ] Commit reviewed sources. Build in a clean short-path checkout using the documented Windows PowerShell 5.1 entry; validate provenance and package hashes.
- [ ] Produce a same-version ZIP for both PCs, verify package install/uninstall/restore in fixtures. Back up verified existing mod files and install only the candidate payload when the game is closed. Preserve original/shared resources and personal configuration.

## Execution decisions

- User approved the design in the preceding conversation and explicitly requested implementation on 2026-09-12. No additional design approval is pending.
- Use the existing clean `codex/duel-undo` source checkout with its prepared build dependencies and controlled staging runtimes. A separate clean checkout validates the final build; installed files remain untouched during development.
- Existing compatibility tests describing full script/rule equality are historical policy evidence; update them to the newly approved contract, while retaining the host/replay identity tests.
