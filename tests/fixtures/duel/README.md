# Host-only public reconstruction fixtures

These six version 1 `.duel` files contain only controlled, public initial card placements and responses authored in `tests/duel_rebuild_tests.cpp`. They do not contain a user's deck or captured game. The human-readable card IDs, seed words, initial options, exact responses and target hashes are in `tests/rebuild-cases.md`.

Do not send this format to ordinary players. Canonical snapshots include both players' hidden cards and are host-internal verification evidence. This is a test fixture codec, not the network snapshot protocol.

All integers are explicitly little-endian. The file is:

1. u32 byte length + UTF-8 magic `YGOUndoCase`.
2. u32 version (`1`).
3. u32 payload length + payload bytes.
4. Exactly 32 SHA-256 bytes over the payload; no trailing data.

The payload contains a length-prefixed name; seed word count and u32 words; duel options; two one-byte booleans; two player records (LP, starting count, draw count); card count and insertion records (u32 code, u8 owner/controller/location/sequence/position); length-prefixed scenario name and parameter bytes; resource digest; response count and records (player, origin, response bytes, before-checkpoint); retained-prefix count; target and final checkpoints. A checkpoint contains player, length-prefixed prompt and canonical bytes, transcript digest, two i64 external clock values, and u64 external AI cursor.

The decoder rejects unsupported versions, checksum changes, truncation, trailing bytes, oversized fields/counts, wrong seed count, invalid player/origin/boolean, empty or over-256-byte responses, mismatched response owner, and out-of-range retained prefixes. The file limit is 64 MiB; canonical/parameter blobs are limited to 16 MiB, prompts to 64 KiB, names to 4096 bytes, initial cards to 512 and responses to 10000. These are test-fixture parser limits, not production history limits.

Run against the matching installed immutable-resource digest:

```powershell
out/tests/C2Release/duel_rebuild_tests.exe --runtime-root F:/MyCardLibrary/ygopro --suite deterministic
out/tests/C2Release/duel_rebuild_tests.exe --runtime-root F:/MyCardLibrary/ygopro --suite isolation
out/tests/C2Release/duel_rebuild_tests.exe --runtime-root F:/MyCardLibrary/ygopro --suite faults
```

For intentional fixture re-recording, `--record-fixtures` executes the actual card effects and independent behavioral checks, then writes candidate files only under `out/tests/results/duel`. Review those files and resource changes before replacing the tracked fixtures. Ordinary tests only read the tracked files. The fault suite writes its small, controlled failing-Lua scenario under the same ignored output root and never writes the installed runtime.
