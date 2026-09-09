# Integrated undo host measurement

Status: formal integrated host measurements completed on clean candidate c624b01996fb343bf79c63571285ebc6bbaa1399. Each requested scale completed 20 successful and 20 failed preparation transactions with actual continuation. This scoped result does not claim whole R2, GUI, LAN, natural-win or release acceptance.

The executable uses the actual UndoDuel/CoreDriver, immutable resources, private ChainBurn controller/worker and ClientRestore N3 candidate. Human protocol envelopes enter the actual host on its owning thread. An empty real listener supplies the baseline server lifecycle. This measures host transactions, not GUI rendering or real client TCP latency. elapsedMs starts before the before-request sample and includes intermediate OS sampling and JSON flush/logging overhead; it is not pure core reconstruction time.

Each requested scale selects the first real human idle command boundary at or above the requested number of accepted responses. All earlier responses are legally accepted by the core; automatic responses do not become artificial Manual targets. The short smoke requested 10 and reached 34. The fixture records its actual target, core seed and resource digest. The human uses 40 Axe Raider, 10,000,000 LP and draw count zero; the actual ChainBurn deck is unchanged. A legal end-turn response is Manual A. Successful undo installs the actual N3 candidate and real AI replay candidate, completes Ready/CommitAck/Resume, then replays A. Failed preparation submits Ready(false), completes AbortAck, verifies the original epoch/PID/checkpoint and accepts a real continuation; a separately labelled cleanup-success rewinds and replays A to restore the baseline. Warm-up and cleanup transactions are excluded from the headline 20+20 samples.

Resource samples cover the host and every OS descendant, including the private controller, active/candidate/retained workers and console hosts. The short smoke observed four stable descendants and six during candidate preparation, returning to four after each transaction and zero after duel teardown. Host handle counts are recorded after every continuation and before/after each whole branch. Exact equality to the first warm sample is not assumed: the repeated diagnostics below found a fixed additional handle. Per-process Windows lifetime peak working set/pagefile counters are labelled as lifetime values. Aggregate peaks are maxima of explicit protocol-stage samples, not continuous peak measurements; summed working sets can count shared pages more than once.

Provenance includes actual compiler dependency coverage, the compiled source inputs, headers, installed toolchain/CMake files, native libraries, bot build outputs, generated build recipes and linked artifacts. Input/recipe digests are compared before/after compilation and execution. HEAD must also remain equal to its initial value; dirty state is captured initially, preventing an old commit identifier from being combined with a later clean state. This does not fingerprint the Windows implementation or ambient machine load. Runtime core resources are identified by ResourceView digest. The actual original bots.json, AI_ChainBurn.ydk and kiwi.zh-TW.json are held with read-only sharing for the entire run and each raw-byte SHA-256 is recorded. Fixtures also record the merged native-to-managed card-view SHA-256 and actual resolved executor/deck/dialog/name/Hand/chat/errata selection. The script uses guarded isolated output paths and preserves previous evidence.

## Evidence before formal measurement

- `out/host-measure-smoke-build.log`, `out/host-measure-smoke.log`, `out/host-measure-smoke.jsonl`: focused short smoke, one measured success and failure plus warm-up/cleanup; exit 0. Settled success 475.186 ms, failed preparation 484.738 ms (console timings include final assertions; JSON stage timings are authoritative).
- `out/host-measure-tree.log` / `.jsonl`: diagnosis of the real controller/worker/console descendant hierarchy. An initial incorrect one-child assumption failed and was corrected to measure the full actual tree.
- `out/tests/HostPerformance/runs/16db029c82c14ae29d47ebb898691873`: formal attempt correctly rejected before transaction execution because CMakeLists changed during compilation. No mixed-input benchmark was published.

Formal command, completed with exit 0:

```powershell
./tools/Measure-UndoHost.ps1 -Cases '10,100,1000' -UndoCount 20
```
Further release diagnostics (not published formal timing):

- `out/tests/HostPerformance/runs/9a60656b62374d0ca79ab29a2c8c1e5f`: verified-build formal attempt stopped after 20 successful transactions and four failures because a post-continuation host-handle equality assertion observed a change. All earlier settled samples had host handles 168, four descendants and no candidate/retained PID. Inputs and raw samples remain preserved.
- `out/host-measure-handles.log`: independent focused repetition reproduced 168→169 after the fifth failure/cleanup/A continuation. The extra handle persisted for 200 ms of polling.
- `out/host-measure-handles-continuation.log` / `.jsonl`: bounded 20+20 diagnostic continued recording counts; the single additional handle remained fixed through the final 15 failures. Descendants returned to zero. First-branch host handles were 138 before initialization and 156 after teardown. This evidence does not establish a leak-free handle result or identify the additional handle's owner/type.
- `out/host-measure-teardown.log` / `.jsonl`: three real short branches (requested 10/11/12, each actual target 34), each two success/two failure trials, exit 0. Before/after host handles: 138→155, 155→155, 155→155; descendants zero after every branch. This separates initial process setup from repeated host teardown at this smaller repetition count.

The toolchain uses libc++ Win32 threading, so no winpthread explanation is asserted. The final three-scale run below preserves per-continuation and per-branch counts. A timing result is not automatically a zero-resource-growth acceptance result.
- `out/host-measure-resource-build.log`, `out/host-measure-resource.log` / `.jsonl`: final resource-binding smoke, one success/one failure plus warm-up/cleanup, exit 0; original resource leases coexist with actual private bot workers.

## Formal result

Run `out/tests/HostPerformance/runs/ae2a4b2902ea4bb19dc06bc7c88d779a` completed successfully. The published [host.json](fixtures/host-performance/host.json), [input manifest](fixtures/host-performance/host.json.inputs.json), [transaction log](fixtures/host-performance/host.json.run.log), configure/build logs are archived byte-for-byte. The JSON contains all stage and continuation samples. The original JSONL remains in the run directory.

| Requested responses | Actual Manual target | Actual AI callback cursor | Success n | Success median / p95 / max ms | Failure n | Failure median / p95 / max ms |
| --- | --- | --- | --- | --- | --- | --- |
| 10 | 34 | 347 | 20 | 479.54 / 496.17 / 505.35 | 20 | 466.70 / 481.22 / 485.83 |
| 100 | 102 | 1,333 | 20 | 485.19 / 504.59 / 507.65 | 20 | 477.34 / 485.35 / 487.21 |
| 1,000 | 1,002 | 24,283 | 20 | 1,773.16 / 1,790.71 / 1,804.98 | 20 | 598.33 / 617.18 / 637.76 |

Median averages the central two sorted samples; p95 uses nearest rank (19th of 20). These are the JSON `settled` elapsed times, including instrumentation overhead. Each failure is a real Human Ready(false) after the actual N3 candidate and private AI candidate are ready. The core candidate is asynchronous and may still be rebuilding at that point; Abort cancels and drains it. In particular, 598.33 ms is not a complete 1,002-response failed core reconstruction time. It measures this specific prepared-client/AI rejection path. Sixty additional `cleanup-success` and six warm-up transactions are separately labelled and excluded from the 120 headline samples.

| Actual target | Path | Stage-observed aggregate peak private / working-set MiB | Settled aggregate private range MiB |
| --- | --- | --- | --- |
| 34 | success | 1,078.98 / 1,034.45 | 541.16–857.45 |
| 34 | failure | 1,298.88 / 1,181.65 | 616.52–1,079.91 |
| 102 | success | 1,098.22 / 1,053.68 | 561.86–876.33 |
| 102 | failure | 1,104.52 / 1,055.22 | 601.72–882.00 |
| 1,002 | success | 1,125.35 / 1,058.30 | 628.90–886.94 |
| 1,002 | failure | 1,210.48 / 1,108.12 | 697.72–963.75 |

Every settled transaction returned to four descendants with candidate and retained PIDs zero; sampled candidate/retained phases had six descendants. Each whole branch returned to zero descendants. The four stable descendants include the controller, active worker and their console hosts, not four AI workers. The host samples include the test harness, resource snapshot and independent N3 model; aggregate memory covers it and all descendants. Memory ranges and OS lifetime peaks are recorded rather than treated as zero-growth guarantees.

| Actual target | Post-continuation host handles: warm baseline; observed range; last | Before / after whole branch host handles | Final descendants |
| --- | --- | --- | --- |
| 34 | 171; 171–172; 172 | 141 / 159 | 0 |
| 102 | 170; 170–170; 170 | 159 / 159 | 0 |
| 1,002 | 170; 168–168; 168 | 159 / 157 | 0 |

The counts include three held external-resource read leases. There is initial process setup retention and one unidentified additional handle in the short branch, followed by no increase across the next branch and a decrease after the long branch. This bounded run shows no accumulating post-teardown handle trend across its three branches; it does not identify every retained handle or establish a universal leak-free result. Candidate/retained worker destruction and original-PID survival on failed preparation were asserted on every trial.

### Memory trend by process

Handle release is not a substitute for memory analysis. In particular, the long success series rose from 636.02 to 856.56 MiB of aggregate private memory (+220.54 MiB). The following table includes all 60 successes, 60 failures and 60 separately labelled cleanup successes. Each cell is first→last settled private MiB within that 20-transaction series.

| Actual target | Series (n=20) | Native host | Control process | Active worker | Aggregate |
| --- | --- | --- | --- | --- | --- |
| 34 | success | 120.72→121.70 | 275.46→333.89 | 217.84→217.85 | 616.39→675.82 |
| 34 | failure | 121.70→123.28 | 371.62→271.67 | 219.22→219.23 | 714.92→616.52 |
| 34 | cleanup-success | 121.96→123.28 | 408.06→297.20 | 219.25→219.26 | 751.65→642.07 |
| 102 | success | 124.72→125.67 | 275.87→376.00 | 219.97→219.88 | 622.95→723.92 |
| 102 | failure | 125.67→125.93 | 412.25→412.45 | 220.62→222.07 | 760.92→762.79 |
| 102 | cleanup-success | 125.67→125.93 | 374.62→387.11 | 221.11→221.37 | 723.76→736.75 |
| 1,002 | success | 147.31→184.41 | 292.93→476.31 | 193.44→193.50 | 636.02→856.56 |
| 1,002 | failure | 184.66→185.69 | 385.39→377.18 | 219.28→218.91 | 791.68→784.12 |
| 1,002 | cleanup-success | 184.94→185.69 | 470.54→330.50 | 193.46→193.48 | 851.28→712.00 |

The host PID was 175476 throughout. The high-memory persistent auxiliary PID in each branch was respectively 165788, 172124 and 172880; these are identified as control processes using the known controller/worker launch hierarchy and their persistence across active-worker replacements. The other auxiliary processes were console hosts. All actor PIDs and raw private-byte counters remain in the archived JSON. An active-worker column compares different PIDs after successful installations; it is not a single surviving worker's heap-growth measurement.

The long success rise splits into native host +37.11 MiB, controller +183.38 MiB and active worker +0.06 MiB. The controller was not monotonic: its last five success samples were 301.92, 297.78, 464.99, 328.02 and 476.31 MiB. Later cleanup samples ranged 210.50–828.62 MiB and ended at 330.50 MiB. Native host success-tail samples were 183.91, 184.16, 184.16, 184.41 and 184.41 MiB; it subsequently reached 185.69 MiB, identical across the last five failure samples and last five cleanup samples. The short and medium native-host last-five failure/cleanup samples likewise remained at 123.28 and 125.93 MiB respectively. Aggregate memory ended the long cleanup series at 712.00 MiB, below its success-series endpoint, with substantial intermediate fluctuations.

These observed rises, drops and final native-host plateaus are compatible with managed collection/native allocator retention, but this run did not capture GC or heap-allocation evidence and does not establish that cause. No forced collection was added. All managed descendants exited after every whole branch, releasing those processes. **Native host privateBytes was not sampled after branch destruction**: only host handles and descendant counts were recorded there. Therefore this data cannot show native private memory returning to its pre-branch baseline. The bounded later samples did not show continued native-private growth or a monotonic controller increase; they do not establish universal leak freedom or close an overall memory-stability acceptance gate. Overall R2 four-mode, manual and LAN acceptance remains separate and pending.
The initial plan anticipated a parallel clean build, but that separate checkout stopped before compilation because of a line-ending issue. No sustained parallel compiler workload occurred during these transactions. Initial preparation/file-hash activity and other ambient machine activity were not controlled. No idle-machine timing claim is made.

Provenance: source HEAD `c624b01996fb343bf79c63571285ebc6bbaa1399`, initial dirty false, unchanged through compilation/execution. Actual compiler coverage: 75 dependency files, 1,427 unique inputs. Input manifest SHA-256 `dd39aa018d14d7a494334550d9e18a79dd8e081491a4e80c0172b37ab2bb02d5`; executable SHA-256 `bdaac7e338180eab29c4b8bb9e3594a874e74896f7dc502ad10ec4cb80534110`. Archived result SHA-256 `95c4cf073d4235f9eef28ae2f9cf050e404ffcbb9209db35c450dcb630d78958`; archived provenance-file SHA-256 `32a8dc872ba7b9ccc077ff5a7b06f1c8ac1f29ab33a3070f4dab1b28ec2de97b`.

No production, original runtime resource, W3 test or shared build file was changed by this bounded measurement work. The implementation consists of `tests/undo_host_measure_tests.cpp`, `tests/undo_host_measure_tests.cmake`, and `tools/Measure-UndoHost.ps1`; this document and its archived evidence report the result. Main CMake registration and commits are parent-owned.