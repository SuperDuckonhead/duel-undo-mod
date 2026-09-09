# Native test harness

The top-level CMake project provides `add_undo_test(name source)`. Each
registered executable uses C++17, can include headers from `tests/` and
`client/gframe/`, and is registered with CTest under the same name. Engine
integration tests must link the pinned client core and Lua sources; isolated
reimplementations of engine behavior do not qualify.

Run the focused native suite through the repository build runner:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target Tests -Configuration Debug
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target Tests -Configuration Release
```

`harness_smoke` verifies a passing `CHECK`. `harness_check_failure`
invokes the same executable in intentional-failure mode and is marked
`WILL_FAIL`, proving that `CHECK` throws and the process reports failure in
Release builds as well as Debug builds.

Machine-readable evidence belongs in `tests/results/<case>.json` and is
ignored by Git. Every result has these fields:

```json
{
  "case": "descriptive-case-name",
  "sourceCommit": "full-git-commit",
  "resourceDigest": "sha256-or-digest-set-id",
  "mode": "single|hosted|bot",
  "passed": true,
  "elapsedMs": 0,
  "peakWorkingSetBytes": 0,
  "failure": null
}
```

Use `failure: null` for a passing case. For a failed integration case,
`failure` records the initial seed, the valid responses supplied before
failure, and the first divergence point. Public, self-contained fixtures
belong in `tests/fixtures/`. Personal decks and complete private duel logs
remain local and must not be committed.
