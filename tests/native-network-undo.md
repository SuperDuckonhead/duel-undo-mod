# Native networking with undo — 2026-09-12

Implements the approved plan in `docs/superpowers/plans/2026-09-12-native-network-undo.md` from baseline `e88d105`.

The ordinary host response, confirmation and one-second timer use `SingleDuel` again. Its only response extension is `SubmitResponse`, with the original implementation retained for ordinary duels. Undo owns history, input identities, candidate reconstruction and coordinated replacement. Native chat no longer depends on a prompt. Clients compare explicit message/restore profiles and logical numeric card data; the host separately freezes full resources for reconstruction and AI.

## Verified before packaging

- Default CTest: 22/22 passed (`all-ctest.log`).
- Runtime-enabled CTest: 40/43 passed in the controlled C4 runtime (`full-ctest.log`). The three fixed reconstruction fixtures correctly rejected that runtime's different practice-script set. All three suites passed against their documented matching read-only installed resources: `rebuild-original-runtime.log`, `rebuild-isolation-original-runtime.log`, `rebuild-faults-original-runtime.log`. Expected digest remains `bff81471fcd422aa17c700f4d770a65f49886369900bfb2bf7da73517f9a1858`; no fixture or production check was weakened. All 43 suites therefore passed with their appropriate resources.
- Protocol/admission/resource cases cover v1 rejection, numeric card changes rejected, script/EXE/restriction/text differences accepted, and clients with missing/malformed scripts. Fourteen TCP resource cases include real host configurations in `StartServer` for both room modes (`real-config-tests.log`).
- Native host tests: original confirmation grace, duplicate confirmation, retry, clock consumption/reset/timeout, rejection, failed/successful restore, stale inputs, privacy and pending responses (`out/native-flow-host-tests.log`, 8/8).
- Actual ChainBurn/Dragun host tests: AI readiness/preparation clock holds, replacement, failure, missing acknowledgement and broken control channel (`out/native-flow-bot-tests.log`).
- Real TCP native chat during play, preparation and resume (`chat-green.log`).
- Unsent native confirmation and response survive consent/abort in order exactly once; stale/closed inputs rejected (`queued-confirm-green.log`).
- Actual Game timed consent pair, loopback free pair, AI menu, single practice/replay: all pass (`timed-pair-final.log`, `free-pair-final.log`, `ai-final.log`, `single-integration-final.log`). Includes two undo generations, alternative branch, rejection, preparation failure, continued play, end/save/replay and editor isolation. Tested client SHA-256: `062da76babe6f10bd2683caa2b88277e97affa7e98578561ccb358e134c2464b`.
- Actual MainLoop two summons/two undos, button/Ctrl+Z and list hovers (`action-mainloop.log`). Five native UI scenarios, shortcut/consent gestures and save/edit/cancel pass; see `native-room-ui.md`.
- Actual timer callback contains standard/nonstandard exceptions, ends the room and exits libevent; ordinary ticks continue (`out/duel-timer-callback-{red,green,ctest}.log`).
- Isolated package tooling: 78 layout checks and 28 release-provenance checks (`package-layout.log`, `prepare-release-tests.log`).

Unprefixed logs above are under ignored `out/native-network-validation/`. They are developer evidence, not files users must install.

## Regressions found and resolved

Recorded failures proved the prior timer bypassed native ticks, native chat was dropped, consent could discard an unsent confirmation, and early selection could hide its window before status arrived. Review found retry clearing historical prompt views and uncaught timer exceptions. Actual paired Game tests exposed a remaining old resource-equality guard at room creation; a real-config TCP regression now covers it. All fixes passed targeted and integrated reruns.

## Delivery boundary

Both computers need the new v2-capability package; older handshakes are explicitly incompatible. Public unmodified game servers do not gain undo support. This remains a test build for two-player Single rooms, not Match/Tag/spectating.

Physical two-device LAN/Tailscale acceptance and cold AI startup on the second device remain unverified. Local two-process tests do not replace them. No shared card/script/image/deck resources are included in the incremental ZIP.

Fresh-build, ZIP, backup and installation evidence is recorded separately after packaging; this source-stage record does not assert those operations have completed.
