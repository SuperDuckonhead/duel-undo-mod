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