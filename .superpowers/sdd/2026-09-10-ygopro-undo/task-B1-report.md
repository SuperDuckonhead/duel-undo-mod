# Task B1 tooling report

## Scope

Implemented only the assigned baseline tooling, fixture tests, and public binary measurement. Source provenance and `sources.lock.json` remain outside this task; this report does not claim that provenance is complete.

## TDD evidence

Red run, before either tool existed:

```text
pwsh -NoProfile -File tests/baseline_tools_tests.ps1
exit 1
FAIL: measurement emits deterministic metadata for all required binaries
FAIL: source lock accepts complete entries and intentional nested destinations
5 passed, 2 failed
```

The two positive cases failed because their target scripts were absent. Negative cases returning nonzero at that point were not counted as implementation evidence.

The initial Windows PowerShell 5.1 compatibility run then caught two real portability issues: child stderr handling in the test runner and JSON array handling. A later red run (6 passed, 1 failed) caught the Windows PowerShell UTF-8 BOM before the writer was made BOM-free with stable LF endings. After correcting those, fresh green runs produced:

```text
pwsh -NoProfile -File tests/baseline_tools_tests.ps1
7 passed, 0 failed

powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/baseline_tools_tests.ps1
7 passed, 0 failed
```

## Implemented behavior

- `Measure-InstalledBaseline.ps1` reads exactly `ygopro.exe`, `Bot.exe`, and `WindBot/WindBot.exe`, emits stable relative paths plus file version, SHA-256, and PE machine, and creates the output directory when needed.
- PE parsing checks the DOS header length and signature, signed PE offset bounds, full PE signature, and machine field bounds before decoding.
- `Test-SourceLock.ps1` requires a non-empty component array and string values for `name`, `url`, `commit`, `evidenceUrl`, `evidenceNote`, and `destination`.
- Commits must be lowercase, full 40-character Git SHAs. URLs must be absolute HTTP(S) URLs.
- Destinations reject rooted, UNC, drive-relative, traversal, dot-segment, backslash, duplicate-separator, and trailing-separator forms. Exact duplicate destinations are rejected, while intentional nesting such as `client` and `client/ocgcore` is accepted.
- Tests use disposable fixtures and exercise successful measurements, malformed PE files, valid nested locks, malformed JSON/schema, invalid commits/URLs, and unsafe paths.

## Public installed-binary measurement

`docs/baseline/installed-binaries.json` was generated read-only from `F:/MyCardLibrary/ygopro`. It contains executable metadata only and excludes user data.

## Concern

No source lock was created or validated by this task. A later integration step must run `tools/Test-SourceLock.ps1 -Path sources.lock.json` after the provenance workstream supplies evidence-backed commits.
