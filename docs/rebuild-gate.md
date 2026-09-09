# Reconstruction gate evidence

## AI callback reconstruction (W1)

Status: W1 representative callback reconstruction passes. This section does not complete Gate A or Gate B. C2/C3 engine reconstruction and the actual client/AI integration gate need their own evidence.

`ReplaySession` owns a separate `WindBot-undo.exe --undo-replay-worker` process for every active or candidate session. Redirected binary standard streams are a private W1 harness transport, not the W2 named-pipe/host transaction protocol. Calls are serialized; failures poison the candidate, and a 30-second callback timeout terminates its process. Disposing a candidate leaves the original worker usable.

The child initializes the actual database, deck registry, `GameClient`, `GameBehavior`, `GameAI`, and registered executor. `Dispatch` calls `GameBehavior.OnPacket`; generated CTOS packets come from the actual `YGOClient` sink, including chats. Historical responses are never returned as decisions. Replay drives only actual STOC message entries after fixed `BotInit/v1`; internal random and output entries must be consumed by those callbacks in strict order. Stopping inside a callback, missing/extra events, changed responses, and unsupported external/clock events fail closed. Successful replay checks `RequireEnd` before recording a new branch. Diagnostic state fingerprints include instance fields, selection containers, object references, callback progress, and the consumed transcript; they are not used to restore state or substitute for behavioral checks.

The isolated sink owns no `NetworkClient` or socket and rejects `Connect`/`Initialize`. Every CTOS overload, including byte arrays and chat, reaches the same sink. Tests assert actual network-send counts remain zero. Delay-only `OnDeckError`, `OnDuelEnd`, and teammate-surrender sleeps are skipped in isolated mode; the actual callbacks and their chat/RNG consumption remain. The baseline `OnReplay` file-writing block was already commented out. Logger timestamps affect only discarded diagnostic output; no decision-path clock input was found. Worker culture is fixed to invariant culture.

`BotInit` deep-copies executor, seed, deck bytes, resource digest, and serialized options. W1 fixes Hand=0, Debug=false, normal single-duel initialization, explicit Chat and UsePreErrataEffects. It pins the selected deck/dialog, `bots.json`, one database, adapted assembly, assembly configuration, and x86/x64 SQLite libraries by SHA-256 and read-only file leases. SQLite is explicitly opened read-only. Program.Rand and DecksManager use the same recorded seeded RNG inside that process; every Random override recomputes and checks its output. Config is initialized before configuration-dependent executors are constructed. This test view is a single frozen cards.cdb; production merged expansion/database views still require host integration and cannot be inferred from these tests.

### Verified fixtures, 2026-09-10

These are public handcrafted protocol fixtures for the real imported callbacks, not core-generated full duels or evidence that every card interaction is covered. They read the installed runtime resources directly without copying them into Git. Deck counts in MSG_START come from the actual selected .ydk.

| Installed AI entry | Actual exercised behavior | Evidence |
| --- | --- | --- |
| 燃血鬥士 / ChainBurn / kiwi.zh-TW | SelectIdleCmd invokes PotOfDualityeff, sets persistent no_sp; next BackJack summon is refused with response 7. A fresh control returns summon response 0. | Independent candidate has same diagnostic state, same next output, and zero real sends; original continues after candidate disposal. |
| 我太帅了 / Dragun / smart.zh-CN | Visible draw/move puts Tour Guide in the monster zone; SelectEffectYn invokes TourGuideFromTheUnderworldEffect and queues Sangan. The next SelectCard consumes the queue and returns index 1. | Independent candidate returns exactly `01 01 01`, matching the active process. |
| 燃血鬥士 / ChainBurn / kiwi.zh-TW | Actual inherited Executor rock-paper-scissors and Dialogs random selection; next eight callbacks after restore. | Every generated packet and RNG event agrees. Replay/end callbacks produce no network writes. Forced deck-error chat remains recorded with Chat=false. |
| 永远之魂 / Monarch506 / soul.zh-CN | Actual constructor reads the fixed UsePreErrataEffects option, initializes and replays startup. | State digest matches. This is initialization coverage only, not a decision-coverage claim. |

Fault coverage rejects response tampering, missing RNG, wrong event kind/order, extra undriven clock events, partial-callback targets, malformed packets, changed seed/options/resources, and reuse of a nonfresh candidate. Deep-copy checks cover tape inputs/outputs, replay expectations, and BotInit caller mutation. A valid earlier target inside a longer tape resumes with the same next output and state as the original. All five public Random overrides and protected Sample are checked; sink connect/attach/mixed ownership attempts are rejected. Unknown test suites exit nonzero.

### Commands and results

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-BotTests.ps1 -Configuration Debug
& .\out\bot-tests\UndoTests.exe --suite all
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-BotTests.ps1 -Configuration Release
& .\out\bot-tests\UndoTests.exe --suite all
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-Bot.ps1 -Configuration Debug -OutputDirectory out/w1-original-debug
```

Debug and Release adapted builds and all suites return 0. The default-name Debug regression build returns 0 and emits WindBot.exe/Bot.exe. The adapted builds have the same 43 existing warnings as the baseline (MSB3884 ×1, CS0168 ×1, CS0169 ×2, CS0414 ×38, CS0649 ×1), with no new warning category. Build-BotTests uses the pinned portable MSBuild/.NET Framework 4.8 reference assemblies and `/p:UndoBuild=true`; output is `out/bot/{Configuration}/WindBot-undo.exe`, with the harness and build dependencies in `out/bot-tests`. The selected database, bots.json, three decks and three dialogs retained their hashes across the Release test run.

### Explicitly unverified support

The installed list inspected here has 62 entries / 61 distinct executor names. ChainBurn and Dragun have only the focused callback coverage above; Monarch506 has initialization coverage. No installed list item is yet certified for complete undo gameplay, and full-support release remains blocked on list-wide validation plus the engine/client gates. The other 58 distinct executors have no W1 behavioral fixture result:

Albaz, Altergeist, Apophis, Archfiend, BE2025, Blue-Eyes, BlueEyesMaxDragon, Brave, Burn, Chaos408, ChaosRitual, DarkMagician, Dogmatika, Dragunity, Enneacraft, Exosister, FamiliarPossessed, Frog, GrenMajuThunderBoarder, HeroBeat1103, Horus, Kashtira, Labrynth, Level VIII, Lightsworn, LightswornShaddoldinosour, Maliss, MalissOCG, MokeyMokey, MokeyMokeyKing, Neko, Orcust, Phantasm, Pumpking, PureWinds, Qliphort, RadiantTyphoon, Rainbow, Rank V, Rank8, Ryzeal, SacredBeast, Salamangreat, SkyStriker, ST1732, SuperheavySamurai, Swordsoul, Tearlaments, ThunderDragon, TimeThief, Toadally Awesome, Trickstar, Witchcraft, Yosenju, Yubel, Zefra, Zexal Weapons, Zoodiac.

No GUI duel, host prepare/commit transaction, live socket reconnection, merged expansion view, all-AI certification, or W2 protocol is claimed by W1.

## Engine candidate reconstruction (C3)

C3's scoped technical reconstruction evidence passes for the six real-card cases below. It uses the reviewed C2 `CoreDriver` and immutable `ResourceView`, rather than an alternate engine, resource interpreter, or handwritten card-effect reversal. `Rebuild` owns a fresh actual ocgcore/Lua handle, processes exactly the retained accepted-response prefix, checks each before-prompt and all target position fields, and returns only a validated awaiting-response candidate. Wrong prefix/metadata, rejected response, changed checkpoint, missing resource, processing failure and finished state throw; the local unique owner destroys the candidate. No live-session argument or GUI, socket, replay-file, chat, sound or timer sink is exposed by this service.

The test resource fingerprint is `bff81471fcd422aa17c700f4d770a65f49886369900bfb2bf7da73517f9a1858`: 13,616 resolved executable scripts and 15,049 normalized cards read from the original installed runtime. Its eager immutable bytes/maps are shared between live and candidates. Candidate reads never fall back to disk; no runtime library or private deck was copied. The six tracked host-only binary fixtures contain only public authored initial placements, recorded accepted choices and verification checkpoints. Full details and exact bytes are in `tests/rebuild-cases.md` and `tests/fixtures/duel/README.md`.

| Case | Actual script / input behavior | Rebuild evidence |
| --- | --- | --- |
| no-shuffle | Both opening options enabled; actual alternating Blue-Eyes/Dark Magician insertion order and five-card initial hand | Three matching replays and subsequent turn inputs; saved order retained |
| initial-shuffle | Real host mtrandom shuffle performed once before saved reverse card insertion | Three matching replays; rebuilding never calls host shuffle again |
| draw-random | Cup of Ace 37812118 executes TossCoin and draws two cards | Same cards, side, subsequent prompt and accepted transcript across three replays |
| lp-discard-cost | Cosmic Cyclone 8267140 pays 1000 LP; restore at target prompt with LP 7000; Tribute to the Doomed 79759861 discards Dark Magician and destroys Blue-Eyes | Three replays preserve paid cost at target and reproduce discard, banish, destruction and following choices |
| multiple-chain-choices | Jar of Greed 83968380 on both sides creates two links; target precedes second choice | Both real operations draw one card and both traps reach graveyards across three replays |
| once-per-turn | Red-Eyes Darkness Metal Dragon 88264978 summons Blue-Eyes while another legal dragon and spare zones remain | Restore before activation and after consumed prefix 8, each three times; effect unavailable in the used turn and available again on turn three under identical following inputs |

Every replay compares player, full prompt, observable canonical bytes and accepted transcript digest at target and every subsequent recorded boundary. Clocks and AI cursor are external recovery state and intentionally do not participate in `SamePosition`. The canonical query is **not** a serialization of private Lua or every private core variable. The actual post-restore once-per-turn availability and subsequent-turn reset are behavioral evidence for the effect count; no broader private-state serialization claim is made.

Each isolation case retains the original handle, history, transcript and logs while repeatedly creating and disposing candidates. `Current()` queries the actual original core without advancing it. The original then accepts another real input and matches a rebuilt control. Faults cover all four checkpoint fields before replay and at target, out-of-range keep, bad player/origin, MSG_RETRY, ignored discarded suffix, missing scenario, null/unfixed resources, wrong fingerprint, finished-before-prompt, real Lua processing failure, strict fixture decoding and disk mutation after resource capture. A newly captured changed view is rejected. Installed normalized executable content is re-captured after each suite and must retain the same fingerprint.

Candidate output isolation here is structural: only `CoreDriver`'s private in-memory transcript and logs are reachable. No application sink callbacks are installed, and the original state/log/transcript assertions are real-handle checks. This harness does not claim OS-wide intercepted network/file/UI event counters; live production sink wiring still requires the C4/N2 integration checks. W1 above separately measures its actual isolated AI output sink's network-send count as zero.

### Combined technical gate and remaining gates

The C2 actual engine/resource/RNG foundation, C3 six-card replay/isolation/fault cases, and W1 actual representative executor/callback replay provide the scoped technical foundation for production integration. This is not full installed-AI or end-to-end release acceptance: W1's explicitly unverified executors remain unverified, its callback fixtures are not full core-generated duels, and the merged host/AI transaction still needs integration evidence. AI remains in scope; no unsupported executor is silently relabeled as supported.

Gate A GUI evidence remains open because the external automation path failed. No GUI duel, host prepare/commit, player-filtered recovery stream, production history wiring, full installed-AI compatibility, long-match latency bound, or release readiness is claimed by this section. C4/N2 must retain these checks and scopes rather than treating checkpoint equality as a substitute for integration or AI behavior.

### C3 commands, elapsed time and resource strategy

```powershell
.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe -S . -B out/tests/C2 -DUNDO_TEST_RUNTIME_ROOT=F:/MyCardLibrary/ygopro
.cache/tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe --build out/tests/C2 --target duel_rebuild_tests -j 4
.cache/tools/cmake-4.4.3-windows-x86_64/bin/ctest.exe --test-dir out/tests/C2 -R '^duel_rebuild_' --output-on-failure
```

The existing `C2` cache is Debug with the pinned LLVM-MinGW Clang 23.1.1 toolchain. The same commands with `out/tests/C2Release` use its Release cache. Final Debug: **3/3 passed, 157.96 s** (deterministic 102.57, isolation 40.01, faults 15.36). Final Release: **3/3 passed, 12.14 s** (5.11, 3.66, 3.36). Per-fixture Release rebuild plus entire following-input replay measured 37–98 ms after the shared resource capture; this is a short-fixture observation, not a production latency guarantee. Recorded response counts are 6–19, without silently truncating any fixture history. Resource capture and final unchanged-content re-capture are included in suite times. Logs: `out/c3-debug-tests.log`, `out/c3-release-tests.log`, and each test directory's `Testing/Temporary/LastTest.log`.

All three replays of each fixture agree; no unexplained engine divergence remained. The first intentional mismatch is rejected at its exact response index. The initial TDD contract failed to compile without `rebuilder.h`; the metadata fault test then failed at the changed retained-record player before metadata validation was implemented. A fixture-development assumption was corrected after observing that spell zone selection occurs before Cosmic Cyclone cost payment; the final target is after payment at prefix 6. No card script was altered to make an outcome match.

After R1's diagnostic-only captured-source-root change, the C3 Release target was rebuilt and its affected faults suite passed 1/1 (`out/c3-diagnostics-faults.log`). The unchanged fixture digest was accepted; the full-suite timings above describe the pre-diagnostic binary.
