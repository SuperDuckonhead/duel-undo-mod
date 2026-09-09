# Integrated undo host measurement

Status: short smoke passed; formal 20-success/20-failure measurements are pending the production pending-response fix and source freeze. This document does not claim completed R2 or release acceptance.

The executable uses the actual UndoDuel/CoreDriver, immutable resources, private ChainBurn controller/worker and ClientRestore N3 candidate. Human protocol envelopes enter the actual host on its owning thread. An empty real listener supplies the baseline server lifecycle. This measures host transactions, not GUI rendering or real client TCP latency. elapsedMs starts before the before-request sample and includes intermediate OS sampling and JSON flush/logging overhead; it is not pure core reconstruction time.

Each requested scale selects the first real human idle command boundary at or above the requested number of accepted responses. All earlier responses are legally accepted by the core; automatic responses do not become artificial Manual targets. The short smoke requested 10 and reached 34. The fixture records its actual target, core seed and resource digest. The human uses 40 Axe Raider, 10,000,000 LP and draw count zero; the actual ChainBurn deck is unchanged. A legal end-turn response is Manual A. Successful undo installs the actual N3 candidate and real AI replay candidate, completes Ready/CommitAck/Resume, then replays A. Failed preparation submits Ready(false), completes AbortAck, verifies the original epoch/PID/checkpoint and accepts a real continuation; a separately labelled cleanup-success rewinds and replays A to restore the baseline. Warm-up and cleanup transactions are excluded from the headline 20+20 samples.

Resource samples cover the host and every OS descendant, including the private controller, active/candidate/retained workers and console hosts. The short smoke observed four stable descendants and six during candidate preparation, returning to four after each transaction and zero after duel teardown. Host handle counts are recorded after every continuation and before/after each whole branch. Exact equality to the first warm sample is not assumed: the repeated diagnostics below found a fixed additional handle. Per-process Windows lifetime peak working set/pagefile counters are labelled as lifetime values. Aggregate peaks are maxima of explicit protocol-stage samples, not continuous peak measurements; summed working sets can count shared pages more than once.

Provenance includes actual compiler dependency coverage, the compiled source inputs, headers, installed toolchain/CMake files, native libraries, bot build outputs, generated build recipes and linked artifacts. Input/recipe digests are compared before/after compilation and execution. HEAD must also remain equal to its initial value; dirty state is captured initially, preventing an old commit identifier from being combined with a later clean state. This does not fingerprint the Windows implementation or ambient machine load. Runtime core resources are identified by ResourceView digest. The actual original bots.json, AI_ChainBurn.ydk and kiwi.zh-TW.json are held with read-only sharing for the entire run and each raw-byte SHA-256 is recorded. Fixtures also record the merged native-to-managed card-view SHA-256 and actual resolved executor/deck/dialog/name/Hand/chat/errata selection. The script uses guarded isolated output paths and preserves previous evidence.

## Evidence before formal measurement

- `out/host-measure-smoke-build.log`, `out/host-measure-smoke.log`, `out/host-measure-smoke.jsonl`: focused short smoke, one measured success and failure plus warm-up/cleanup; exit 0. Settled success 475.186 ms, failed preparation 484.738 ms (console timings include final assertions; JSON stage timings are authoritative).
- `out/host-measure-tree.log` / `.jsonl`: diagnosis of the real controller/worker/console descendant hierarchy. An initial incorrect one-child assumption failed and was corrected to measure the full actual tree.
- `out/tests/HostPerformance/runs/16db029c82c14ae29d47ebb898691873`: formal attempt correctly rejected before transaction execution because CMakeLists changed during compilation. No mixed-input benchmark was published.

Formal command after production freeze:

```powershell
./tools/Measure-UndoHost.ps1 -Cases '10,100,1000' -UndoCount 20
```
Further release diagnostics (not published formal timing):

- `out/tests/HostPerformance/runs/9a60656b62374d0ca79ab29a2c8c1e5f`: verified-build formal attempt stopped after 20 successful transactions and four failures because a post-continuation host-handle equality assertion observed a change. All earlier settled samples had host handles 168, four descendants and no candidate/retained PID. Inputs and raw samples remain preserved.
- `out/host-measure-handles.log`: independent focused repetition reproduced 168→169 after the fifth failure/cleanup/A continuation. The extra handle persisted for 200 ms of polling.
- `out/host-measure-handles-continuation.log` / `.jsonl`: bounded 20+20 diagnostic continued recording counts; the single additional handle remained fixed through the final 15 failures. Descendants returned to zero. First-branch host handles were 138 before initialization and 156 after teardown. This evidence does not establish a leak-free handle result or identify the additional handle's owner/type.
- `out/host-measure-teardown.log` / `.jsonl`: three real short branches (requested 10/11/12, each actual target 34), each two success/two failure trials, exit 0. Before/after host handles: 138→155, 155→155, 155→155; descendants zero after every branch. This separates initial process setup from repeated host teardown at this smaller repetition count.

The toolchain uses libc++ Win32 threading, so no winpthread explanation is asserted. The final three-scale run must preserve per-continuation and per-branch counts. A timing result is not automatically a zero-resource-growth acceptance result.
- `out/host-measure-resource-build.log`, `out/host-measure-resource.log` / `.jsonl`: final resource-binding smoke, one success/one failure plus warm-up/cleanup, exit 0; original resource leases coexist with actual private bot workers.
