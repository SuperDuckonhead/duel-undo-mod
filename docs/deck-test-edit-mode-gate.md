# Deck-test edit-mode executable gate

## Status and boundary

The six OpenSpec 1.1–1.6 gates below were implemented and independently approved through commit `1c33c8b843078070f3cbb52937e6aba462361139`. They establish a finite, reproducible correctness boundary for deck-test query identity, original-script rule checks, controlled source creation, deterministic replay, and recipient-safe delivery. They do **not** deliver the editor/session/UI feature or certify release readiness.

Every required fixture must pass before later work may depend on this boundary. A failure may not be replaced by skipping the engine's activation check, creating the selected card directly in its final zone, or checking script text instead of running the original failure mechanism. These gates also do not turn the disposable per-candidate core into the production catalog, worker IPC, reusable worker state, or performance design.

The gate preserves the privacy rule: an AI receives only information normally visible to its seat. It does not receive the full-card-pool search list, newly introduced card details while they remain hidden, host query identities or logs, or trial/search history.

## Evidence map

All commits in this table received a scoped independent approval. Saved reports and raw logs are under the ignored `.superpowers/sdd/deck-test-edit-mode-implementation/` directory.

| Requirement | Source and executable evidence | First meaningful RED | Final positive and negative result | Reviewed commit(s) | Material limit |
| --- | --- | --- | --- | --- | --- |
| 1.1 typed boundary and plan model | `client/gframe/undo/deck_test_model.{h,cpp}`; `tests/deck_test_model_tests.cpp` | The wished-for header did not exist. Review then exposed an introduction event whose binding stage was not tied to a selection role. | Focused model tests pass, including all four role/stage mappings, invalid role/stage, contradictory references, duplicate identities, and unchanged legacy response/checkpoint readability. | `d7f88341a9eb8e5a468513136258be06f654fca9`, fix `1e27d4d50a5a7bd03312d8e9be27153ce763fd6e` | Value types and validation only; no runtime query, engine mutation, event wire format, AI path, or UI. |
| 1.2 D01 native query loop | `client/gframe/undo/deck_test_query.{h,cpp}` and native hooks; `tests/deck_test_query_tests.cpp` | With no physical target, the expected native `MSG_SELECT_EFFECTYN` was absent. Later counterexamples exposed missing full-activation recheck and incomplete source initialization. | No-target existence check → asynchronous disposable-core evidence → same-prefix native action prompt → accepted native activation → original private `SelectMatchingCard` boundary passes. Player activation restriction, deck-range restriction, candidate Lua/RNG/error isolation, stale identity, bad prefix, rejection rollback, and OFF recomputation reject correctly. | `691031f73d6b33c8ed1e64e9da2cbd9be682f68d` | Pinned Aluber shape, player 0, one own-main-deck candidate and a finite supplied candidate span. No complete catalog, subprocess cancellation, shared state reuse, or special-summon/competing-chain acceptance proof. |
| 1.3 original FusionSpell and Gold boundaries | Native observation hooks and `PoolQueryGate::Recreate`; `tests/fusion_gold_query_tests.cpp` | Original activation ran, but no query observation existed; later REDs exposed missing per-target identity and misclassified nested extra/opponent scopes. | Two independent cores reproduce original Branded Fusion and Gold Sarc boundaries. Fusion verifies per-target material scopes, original whole-group closures/procedure, native material selection/moves and completed summon. Gold selects and removes the same existing native instance. Missing Albaz, missing LIGHT, substitute closure, duplicate response and sibling-target controls reject or remain isolated. | `202c31c867c6d2cdc9676b00d73deca718b0e20c` | No prospective source creation, Chain Material/extra-material route, catalog search, or Gold two-standby return proof. |
| 1.4 controlled first source binding | `PoolQueryGate::Introduce` / `ReplayIntroduction`, `new_card_result`; `tests/deck_test_introduction_tests.cpp` | Earlier virtual evidence became stale during replay; after milestone replay, source introduction was explicitly unimplemented. Later REDs exposed overwritten initialization errors and insufficient milestone context. | One confirmed Branded source is created in a disposable candidate immediately before the original resolution filter. The actual returned native identity is selected through the native slot, the original script moves DECK → HAND, two independent replays and a following response agree, and failures after initialization or after movement discard the whole candidate without touching live state. | `9407217f5cff1436722a15ff1a4c55a7be880404` | One own-main-deck resolution target in one retained event history. Synthetic allocation/error fixtures test identity and rollback only; original cards provide the source/rule proof. |
| 1.5 stable query signature | `PoolQueryRequest` and registered-query replay/admission; `tests/query_identity_tests.cpp`; `tests/fixtures/query-identity/c900000120.lua` | The actual two-predicate effect produced no two-query discovery. Reversed native order then returned an empty result without a signature error; later retained-history replay diverged. | Authentic predicate A evidence is refused at predicate B, B is refused at A, and reversed call order is refused with a named parameter/signature mismatch. Twenty-eight field mutations, same-code/different-handler identity, pre/post RNG states, stale later-prefix installation and missing source-event transport fail without live mutation; correct evidence still reaches native activation and original source movement. | `d5eaa20e4923d0f9a0511ceb5535b0f709edeb1a` | Exact registered D01 and synthetic two-predicate shapes only. No arbitrary Lua serialization, general later-prefix positive search/install, multi-event history, or complete adapter coverage. |
| 1.6 ordered hidden-source recipient transaction | `client/gframe/undo/test_state_patch.h`, core/private output, player/host/bot transaction code; `bot/Undo/TestStatePatch.cs`, `PreparedContinuation.cs`; `tests/hidden_source_tests.cpp`; `bot-tests/PatchTests.cs` and transaction tests | Managed replay rejected the new event; extended prepare did not exist; duplicate accounting and reveal counts failed. Native Gold lacked a pending binding; then the original visible stream failed at `Original Gold without birth patch: missing card`. Fusion binding, ordinary-ledger absence, foreign prompt refusal and preserved historical prefix each had separate REDs. | The source birth precedes original visible messages; both seats receive a localized unknown slot, then the same object is normally revealed/moved. Exact version/capability, count, sequence, recipient prompt, duplicate/out-of-order, idempotency and changed-byte checks fail closed. Prepare/Abort preserves the active process; coordinated Commit/ack/Resume publishes once; later ordinary tape replay reproduces the birth. Original Gold and Fusion movements run through real recipient and managed transactions. | `1c33c8b843078070f3cbb52937e6aba462361139` | A real-recipient, one-birth gate, not the public editor/session dispatch path. Persistent hidden shuffle/cut mapping, full source-region/effect matrix, post-commit rollback matrix and broad distributed integration remain open. |

## D01 query and binding semantics

The D01 fixture starts with no physical Branded Fusion target. The actual control reaches the target check at accepted prefix 4 and has no eligible action. Discovery reconstructs that prefix in an actual-only core and compares its native checkpoint and targeted diagnostic state with the live boundary. Discovery itself creates no precheck entity.

Search evaluates a finite caller-supplied candidate span asynchronously. Each candidate owns a disposable actual core. Only at the exact captured `Duel.IsExistingMatchingCard` call does it create a witness using the original `new_card` lifecycle: `initial_effect`, source placement, field-effect enablement and `adjust_instant`. It runs the original filter and then rechecks the complete original effect activation against the live event. Candidate failures, messages and both native/Lua RNG changes stay private and are discarded. This proves finite correctness; it is deliberately too expensive and incomplete to serve as the later production pool search.

Accepted evidence is replayed at the same response prefix. The rebuilt core must emit the native action prompt for the same handler. A real positive response is accepted, and original activation/chain processing reaches the original resolution `SelectMatchingCard`. The unresolved private boundary is not converted into a client selection prompt and does not create a source. The Task 1.4 introduction occurs only at that later original selection boundary, where the original filter accepts the confirmed entity and the original `SendtoHand`/`ConfirmCards` flow moves it.

The introduction uses controlled native initialization and the object returned by that lifecycle. Prospective plan identity `1001` maps to returned native card ID `23` in the bounded fixture; the implementation does not guess a counter. That object is in DECK when the native slot is derived and the same object is in HAND afterward with `REASON_EFFECT`. The synthetic `900000025/26` allocation fixture proves that initialization may allocate another object first; it is an identity stress test, not source/rule authorization. The original resolved Aluber and Branded scripts are the functional proof.

Preparation is atomic at the candidate owner. Validation, initialization, original filtering, native selection, original movement, event allocation and replay checks all finish before a no-throw owner swap. Initialization faults, field-adjust restrictions, selected-entity filter failure, faults after original movement, corrupt milestones and corrupt saved selections destroy the candidate. The live core pointer, checkpoint, ordered cards, counters, both RNG states, transcript, logs, histories and external output remain unchanged. Two independent recreations and a following native input establish deterministic replay for this finite history.

## Query identity and original script scopes

Query identity is pointer-free and binds all of the following: resource fingerprint; script hash; stable handler instance ID and card code; effect registration ordinal and event code; target-check or resolution stage; registered native API and semantic role; adapter/callsite ID and invocation ordinal within that phase; both source-region masks; player and bounds; normalized parameter shape, Lua type tags, deterministically normalized values and predicate/caller identity; accepted-response-prefix digest; and separate before-call and after-call state digests. Fusion identity additionally binds parent scope, selected target instance, target script and fusion-procedure registration.

`request.callState` is the state **before** the original predicate/filter is consumed. `afterCallState` is captured **after** that call. They are separate timestamps because an original predicate may change Lua or engine state. Task 2's earlier target evidence historically used the post-filter state; Task 4 introduced the pre-filter resolution identity needed to create the source before consumption. Final Task 5 semantics bind both states where the registered target predicate requires them.

Lua functions, closures, Groups, userdata and entity pointers do not cross core boundaries. On replay, each core reacquires its live handler/effect, closure and typed entity arguments. A signature mismatch is stale or uncovered evidence. It is never reinterpreted as an empty result, and an existence result from one predicate is never reused for another filter in the same effect.

The Task 5 fixture makes that rule executable. Predicates A and B run at the same native callsite with discriminators 21 and 22. A accepts only `900000121`; B accepts only `900000122`. Cross-applying authentic evidence is refused, and reversing their native call order is refused even though the physical state, accepted prefix and prompt otherwise match. Correct evidence still produces the native action prompt and original resolution. A second same-code handler instance cannot consume another instance's evidence.

## FusionSpell and Gold Sarcophagus

The effective Branded Fusion script is the expansion resource `script/c44362883.lua`, using expansion `script/procedure.lua` and `FusionSpell`. The proof follows the original target/operation functions and native calls; it does not assume that a loose script wins. The Gold Sarcophagus proof follows the original loose `script/c75500286.lua` activation and `Duel.SelectMatchingCard` / `Card.IsAbleToRemove` call.

The Fusion control reaches the native target selection after four accepted responses. The operation's outer extra-deck target query is distinct from nested extra-material and opponent-effect queries even when they use the same location mask. Each selected fusion target has its own stable target ID and scope digest. For Albion, original `GetFusionMaterial(0, HAND|DECK|MZONE)` returns the actual Albaz and Blue-Eyes instances. The original `CheckFusionMaterial`, target fusion procedure, additional check/goal closures, full two-card group and native `SelectFusionMaterial` must all agree. Original native movement applies `REASON_EFFECT | REASON_MATERIAL | REASON_FUSION`, and Albion finishes in the monster zone with `STATUS_PROC_COMPLETE`.

Missing Albaz, missing LIGHT material and a substitute pair that original Polymerization accepts but Branded's additional closure rejects are negative source/rule evidence. A duplicate native material response causes `MSG_RETRY` without movement or allocation. A sibling Albion target retains a separate, stable material scope. Chain Material and extra-material effects are explicitly absent from this control and remain required later.

Gold recreates accepted prefix 4 and binds the original resolution selection. The native selection chooses stable instance ID 3 and original `Duel.Remove` moves that same object face-up to the banished zone. The original flag/label registration runs. The finite proof does not cover the later two-standby return.

All resource hashes below were asserted through `ResourceView::Capture(runtime)->Read(logicalPath)`, which applies configured expansion/archive priority. Direct path hashes were checked only as corroboration.

| Resolved resource | SHA-256 |
| --- | --- |
| Current captured view, 13,644 scripts / 15,077 cards | `200fac9189bbfba476dd019770fd039e5625f4892945fd894306bd2b22f87620` |
| `script/c62962630.lua` | `14432383c12ce67c8b171ff9e96326130714f676f505b3472b4c898969218141` |
| expansion `script/c44362883.lua` | `df65c2875fe485cab0ba106f2f9a8926af85e5616f2c3aa9d20125becbf85469` |
| expansion `script/procedure.lua` | `df887c18619374f825fc14f5a5f05f6a090c910adbb52933bd8462c773973baa` |
| expansion `script/c87746184.lua` | `8336d726dcdd1720461111283d5a037d7689df96338300ab5843b2a3acb05db8` |
| loose `script/c75500286.lua` | `45789f7d9fea6da47b798b2d08ac1612521c0ad9d603e9120a8babcb2b867e04` |

## Recipient delivery and privacy

The host-private `CoreOutput` ledger contains raw native messages plus a source-birth marker at the native message-buffer boundary recorded before source creation. It exists only for an attached pool query or explicit private collector. Ordinary unextended `CoreDriver` creation does not allocate or copy it. The explicit collector remains attached while query observers are intentionally detached for historical replay, so original DRAW and CHAINING messages remain before the birth. Host native instance IDs, query signatures, introduction records, raw logs and diagnostics never enter a recipient frame.

`TestStatePatch/v1` is a fixed 16-byte recipient input. It contains version, transaction-local birth ordinal, recipient/owner/controller, main-deck location, recipient sequence, facedown position and expected pre-birth deck count. It contains no card code or stable native identity. The recipient inserts a new unknown object at that sequence while preserving existing order. Native seat identity is converted through the existing local-player mapping, so both recipient perspectives are tested.

The original filtered continuation then reveals or moves that slot through normal messages. The same managed `ClientCard` reference that began unknown becomes the public banished Gold source or Fusion graveyard material. Its first ordinary departure clears the uncounted-source marker without decrementing an unrelated original duplicate; a normal reveal contributes its now-known code while it remains in deck. Persistent shuffle/cut reconciliation for a still-hidden source is not established.

The extended transaction negotiates capability/version before use. Legacy Prepare command 3 and `YGOV/1` remain unchanged; extended command 9 and `YGOV/2` carry the immutable continuation. Unsupported version/capability, malformed or oversized data, wrong count/seat, a foreign recipient prompt, duplicate/out-of-order birth, and changed bytes fail closed. A semantic candidate failure or Abort disposes the candidate and leaves the active process, client field and tape unchanged. Commit remains coordinated: both participants prepare, the candidate installs once, acknowledgements advance the epoch, and Resume releases the retained old process and admits new input. A later ordinary no-patch replay consumes the recorded private birth tape entry.

The native fixture uses actual `SingleDuel::Analyze` recipient filtering and actual client/host/bot owners. The managed fixture uses real `ReplayWorker`, `GameBehavior` and duel state. Synthetic managed frames cover codec/accounting failures only; original Gold/Fusion cards and streams provide the rule, ordering and same-object proof.

## Reproduction

Run from the repository root in PowerShell. The commands below use the already bootstrapped CMake 4.4.3 and LLVM-MinGW toolchain under the sibling undo worktree cache. The installed runtime roots are read-only.

First build the adapted managed bot and its test runner. The exact build uses .NET SDK 8.0.425, .NET Framework 4.8 reference assemblies, `UndoBuild=true`, and `tools/Bot.Build.targets`:

```powershell
$sdk = 'F:/MyCardLibrary/ygopro/dev/duel-undo-mod/.cache/tools/dotnet/dotnet.exe'
$refs = 'F:/MyCardLibrary/ygopro/dev/duel-undo-mod/.cache/net48-ref/build'
$env:DOTNET_CLI_HOME = 'F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/dotnet-home'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_ADD_GLOBAL_TOOLS_TO_PATH = 'false'
& $sdk msbuild bot/WindBot.csproj /t:Build /m:1 /p:Configuration=Debug /p:Platform=AnyCPU /p:UndoBuild=true /p:BaseIntermediateOutputPath=obj/undo/ /p:IntermediateOutputPath=obj/undo/Debug/private-deps/ "/p:TargetFrameworkRootPath=$refs" /p:OutputPath=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/bot/Debug /p:CustomAfterMicrosoftCommonTargets=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/tools/Bot.Build.targets /v:minimal
& $sdk msbuild bot-tests/UndoTests.csproj /t:Build /m:1 /p:Configuration=Debug "/p:TargetFrameworkRootPath=$refs" /p:BotOutputPath=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/bot/Debug /p:OutputPath=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/bot-tests /v:minimal
```

The resulting required paths are:

```text
out/bot/Debug/WindBot-undo.exe
out/bot-tests/UndoTests.exe
F:/MyCardLibrary/ygopro/WindBot
F:/MyCardLibrary/ygopro/cards.cdb
```

Configure the native test tree with every runtime-dependent registration variable. Omitting one can silently omit tests from CTest, so confirm the registered count before relying on a run.

```powershell
$bin = 'F:/MyCardLibrary/ygopro/dev/duel-undo-mod/.cache/tools/cmake-4.4.3-windows-x86_64/bin'
& "$bin/cmake.exe" -S . -B out/tests/Debug `
  -DUNDO_POOL_RUNTIME_ROOT=F:/MyCardLibrary/ygopro `
  -DUNDO_TEST_RUNTIME_ROOT=F:/MyCardLibrary/ygopro `
  -DUNDO_BOT_TEST_EXE=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/bot/Debug/WindBot-undo.exe `
  -DUNDO_BOT_TEST_RUNTIME=F:/MyCardLibrary/ygopro/WindBot `
  -DUNDO_MANAGED_TEST_EXE=F:/MyCardLibrary/ygopro/dev/deck-test-edit-mode/out/bot-tests/UndoTests.exe
& "$bin/cmake.exe" --build out/tests/Debug -j 4
& "$bin/ctest.exe" --test-dir out/tests/Debug -N
```

The recorded configuration lists 59 tests. The six gate requirements and final Task 6 focused proof can be run with:

```powershell
& "$bin/ctest.exe" --test-dir out/tests/Debug `
  -R '^(deck_test_model_tests|deck_test_query_tests|fusion_gold_query_tests|deck_test_introduction_tests|query_identity_tests|hidden_source_tests|hidden_source_managed_tests)$' `
  --output-on-failure

& "$bin/ctest.exe" --test-dir out/tests/Debug `
  -R '^(core_driver_tests|deck_test_introduction_tests|hidden_source_tests|hidden_source_managed_tests)$' `
  --output-on-failure
```

`hidden_source_managed_tests` has a CTest fixture dependency on `hidden_source_tests`, which generates its recipient-only inputs. The shared `out/tests/results/hidden-source` directory means Debug and Release invocations must be run separately.

Run the managed suite with its runtime/database variables:

```powershell
$env:WIND_BOT_RUNTIME = 'F:/MyCardLibrary/ygopro/WindBot'
$env:WIND_BOT_DATABASE = 'F:/MyCardLibrary/ygopro/cards.cdb'
& ./out/bot-tests/UndoTests.exe --suite all
```

The affected Release comparison used a separately configured `out/tests/Release` tree with the same five runtime/bot variables, `-G 'MinGW Makefiles'`, `-DCMAKE_BUILD_TYPE=Release`, and explicit cached `clang++.exe` / `mingw32-make.exe`, then:

```powershell
& "$bin/cmake.exe" --build out/tests/Release `
  --target undo_duel_tests undo_tcp_tests undo_bot_host_tests undo_pending_response_tests native_duel_flow_tests -j 4
& "$bin/ctest.exe" --test-dir out/tests/Release `
  -R '^(undo_duel_tests|undo_duel_duplicate|undo_tcp_.*|undo_bot_host_tests|undo_pending_response_tests|native_duel_flow_tests)$' `
  --output-on-failure
```

## Recorded regression results and qualifications

The historical suite record must be read as separate runs, not merged into one clean result:

| Evidence point | Recorded result | Qualification |
| --- | --- | --- |
| Task 1 final model state | 24/24 in 17.45 s | Followed an unrelated admission failure, an unchanged focused retry, and a final full pass. The pure role/stage fix later passed focused 1/1 only. |
| Task 2 final full Debug | 25/25 in 62.73 s | Switch and Irrlicht linker warnings were observed; exact baseline compiler-warning evidence was incomplete and later supplemented from available output. |
| Task 3 final full Debug | 26/26 in 89.91 s | Original Fusion/Gold and D01 passed; inherited `libeffect.cpp` switch and Irrlicht LNK4217 warnings remained. |
| Task 4 final full Debug | **26/27** in 171.58 s | `server_admission_tests` failed at `got>0`; one unchanged focused retry passed 1/1 in 0.11 s. This is not a 27/27 full pass, and the socket cause is unproven. |
| Task 5 final full Debug | 28/28 in 259.56 s | No retry. Admission passed, but that does not establish a cause or repair for its inherited intermittent failure. |
| Task 6 expanded Debug | **40/59** in 1176.90 s | Mixed-binary-timing intermediate run: 19 failed/not run. It began before the diagnostic-order and no-pool-ledger fixes; later focused builds occurred while it ran. It is not final-source evidence for those paths. |
| Task 6 final focused Debug | 4/4 in 128.05 s | Final source: `core_driver_tests`, introduction, native hidden-source and managed companion. |
| Task 6 affected Release | 13/13 in 66.65 s | Functional comparison only. It does not explain or prove a fix for Debug timeouts/TCP receive failures. |
| Task 6 managed Debug | 18 PASS checks | Final managed source, including original 15 plus birth tape, transaction and accounting checks. |
| Current-resource rebuild audit | 3/3 in 7.51 s | Unchanged test/recorder behavior against scratch current-resource candidates; separate from pinned-fixture maintenance. |

The expanded Debug run exposed configured 90/180-second timeouts, multiple TCP receive `got>0` failures, and a secondary prepare-disconnect SegFault. Their causes remain unproven. A slow synchronous path is plausible, but the log lacks stage and `WSAGetLastError` evidence. Release success does not diagnose Debug. `server_admission_tests` passed in that expanded run; its earlier intermittent failure is still not considered fixed.

After the expanded run started, source changed in two task-local paths. Birth validation was moved to the ordered-output boundary so the existing original filter failure remains the first diagnostic. The private-output ledger guard was changed to `poolQuery_ || privateOutput_`, after a real RED showed that a pool-only guard omitted historical CHAINING output. Only the final focused 4/4 run is final-source evidence for those corrections.

The final managed compilation retained 43 inherited warnings: MSB3884 ×1, CS0168 ×1, CS0169 ×2, CS0649 ×1 and CS0414 ×38. Native evidence retains the inherited `libeffect.cpp:51` `-Wswitch` warning for `MEMBER_CODE`, `MEMBER_DESCRIPTION` and `MEMBER_ID`, plus the Irrlicht `MATERIAL_MAX_TEXTURES_USED` LNK4217 warning. Later incremental builds without warnings do not prove those diagnostics were removed.

### Rebuild fixture resource guard

The three original tracked rebuild gates are pinned to resource fingerprint `bff81471fcd422aa17c700f4d770a65f49886369900bfb2bf7da73517f9a1858` (13,616 scripts / 15,049 cards). The current frozen runtime is `200fac9189bbfba476dd019770fd039e5625f4892945fd894306bd2b22f87620` (13,644 / 15,077). The unchanged guard rejects the original pinned inputs before gameplay. Independent source/input analysis established that the unchanged baseline would reject that same mismatched input; no baseline binary rerun was performed, and no complete old resource snapshot was available.

The current-resource audit used the unchanged recorder and behavior assertions to write six candidates under `out/tests/results/duel`. Decoding every old/new pair showed the resource digest as the only decoded difference; the outer checksums changed accordingly. The unchanged three rebuild suites then passed 3/3 against those scratch candidates. This is current-resource functional evidence. It is not a pass of the original pinned-input tests and is not deliberate tracked fixture maintenance, which remains required by task 11.4. The strict resource guard remains part of the gate.

## Work still required

This boundary does not remove any later requirement. The following work remains open:

- editor/session ownership, mode transitions, launch entry, paging, preview, confirmation and UI;
- the complete event-replay transport, production card catalog/index, adapter coverage, worker IPC/cancellation, worker state reuse and performance bounds;
- general host source-install/session orchestration (including task 6.5), main/extra-deck multi-plan binding, multiple introductions and broader history;
- persistent hidden-source shuffle/cut remapping and privacy-safe accounting;
- the mandatory D01–D07 and S01–S04 fixture matrix, including competing triggers, costs/targets/materials, Chain Material/extra-material paths and Gold's delayed return;
- Debug timeout/TCP diagnostics, tracked rebuild-fixture maintenance, full fault/replay/history integration, list-wide AI validation, performance acceptance and release acceptance.

The two approved review notes also remain: the Fusion identity/completion code is dense and should be made easier to review when that area next changes, and the hidden-source fixture output directory should become build-configuration-specific before concurrent Debug/Release runs.
