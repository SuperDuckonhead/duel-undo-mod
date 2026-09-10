# Resource scope and uninstall regression (2026-09-11)

This change excludes `single/` from ordinary LAN, loopback and AI room captures.
Practice keeps the historical full snapshot. Replay capture uses the saved
scenario name and accepts a legacy normal-duel snapshot only when its entire
original resource digest matches. Engine, card data, effect scripts, archive
resolution, preferences and rules remain validated.

## Observed regressions and verification

- The new room regression first failed the compatibility assertion when only
  practice scripts differed. After the scope fix, real TCP challenge and
  confirmation succeed in LAN and loopback modes. Unrelated oversized practice
  files and a malformed `single` path do not prevent normal room admission.
- Changes to card scripts, card data, script priority, archive ordering and ban
  lists still fail admission. Practice reconstruction uses frozen scenario and
  framework bytes after the disk copies change. Normal recordings ignore
  practice-only differences; practice and legacy recordings retain their exact
  digest checks, including body-v1 Single recordings.
- The new normal replay regression initially failed with `Initial resource
  digest mismatch`; the mode-aware replay capture resolves it without changing
  the wire or replay format.
- The first new test harness crashed because its local DataManagers had no
  Irrlicht filesystem. That invalid harness failure was corrected before the
  compatibility regression was counted. The new and exercised integration
  test entries suppress Windows crash-dialog UI and report process failures.

Release CMake build and CTest: **22/22 passed**. The actual client Release build
also passed and retained only Windows system DLL imports.

Actual Game integrations passed in Windows PowerShell 5.1:

| Integration | Coverage |
| --- | --- |
| Single + Replay | Four option combinations, manual/automatic choices, retry, A/undo/A/undo/B, LP/card restoration, save/playback and restart model |
| Timed pair | Two actual Game processes, 180-second room, LAN consent/decline, continued input, failed restore rollback, two epochs and continued new branch |
| AI room | Private adapted bot, human/bot turns, undo, normal finish, saved network replay and editor isolation |

Local logs are retained under `out/scope-uninstall-validation/`: `ctest.log`,
`client-build.log`, `single-integration.log`, `room-timed-pair.log`, and
`room-ai.log`. Personal resources and staging outputs are not source assets.

## Uninstall and packaging

The standalone uninstall tool is constrained by fixed destination names and
validated installed hashes. It previews before confirmation, keeps config and
logs, backs up exact bytes, retains unknown/changed files and reports partial
completion. Partial removal retains both uninstall tools and metadata so it
can be retried. File disposition is set on the same exclusive native handle
used to hash and back up each file; it never closes a verified handle and then
deletes a replacement by pathname.

The focused suite passed **511 checks in each of Windows PowerShell 5.1 and
PowerShell 7**, including
protected files, malformed/duplicate manifests, path escapes, reparse points,
locks, concurrent replacement attempts, backup failure, legacy local-fix
binding, cancellation, partial retries and real CMD self-removal. The launcher
uses CRLF and friendly interactive status output. The real installed alpha.1
was previewed without removing files.

PowerShell 7's automatic JSON date conversion initially rejected valid build
timestamps. Parsing now requests string dates when that shell supports the
option; Windows PowerShell 5.1 retains its original parsing and manifest hashes
always cover the original file bytes.

Packaging layout tests passed **78 checks**; release preparation tests passed
**28 checks**. Both tools are hashed release payloads. Independent review's two
uninstall findings (final-delete race and loss of partial-retry tools) were
corrected and re-reviewed with no remaining blockers.

Two physical-device LAN/Tailscale acceptance is still pending. These local
results do not close existing AI cold-start or room-creation investigations.
