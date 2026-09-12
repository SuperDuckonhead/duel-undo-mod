# Native-network candidate delivery — 2026-09-12

Source and runtime regression evidence: `native-network-undo.md` and `native-room-ui.md`.

## Fresh build and package

- Built source: `2187cb36ab7dcac7511a578470612b58cda277a6`.
- Clean checkout: `F:/MyCardLibrary/ygopro/dev/undo-n3`; 19 locked archives copied, then verified offline bootstrap. Windows PowerShell 5.1 ran `Prepare-Release.ps1 -Build`, invoking the complete Release build. No prior objects were reused.
- Fresh default CTest: 23/23 passed. The adapted bot and bot tests were rebuilt by the same All target. The read-only provenance check passed afterward.
- Fresh executable initialization and normal closure passed with an unrelated working directory, non-ASCII executable path and shell shortcut; original program/config/dependency hashes were unchanged. The 258-character executable-path probe passed.
- ZIP: `ygopro-undo-2187cb3-win-x64.zip`, 7,469,913 bytes, 39 files.
- ZIP SHA-256: `958de032bff63aa39aa922dcc1f2c30f3da88e92fbd3676bf11446d6436ca339`.
- Client SHA-256: `833839f6f7b71a8017035dddbb074219ec28fa16a5a1b3355bc5be5260bc70be`.
- Adapted WindBot SHA-256: `02fe61badb23c6b5448b0b6ee42bfe660afc45b6776f9c025da75f1c524d5287`.
- Delivery copy and `.sha256` file: `F:/MyCardLibrary/ygopro/dev/packages/`.

## Package and installation verification

The exact ZIP passed 491/491 PowerShell 5.1 fixture checks: 39 files extracted and hash-verified, preview changed nothing, all 39 uninstalled and backed up, all 39 restored from the backup and hash-verified. Eighteen original program/config/deck/resource sentinels remained unchanged. An initially over-deep fixture encountered the Windows path limit and stopped safely; the shorter fixture passed without changing the production tool.

The existing 79a7e96 package was independently verified against its previously recorded ZIP SHA-256. Its 39 installed mod files and personal mod configuration were backed up to `F:/MyCardLibrary/ygopro/dev/backups/before-native-network-20260912-115630`.

The actual runtime `F:/MyCardLibrary/ygopro` passed managed-ownership layout checks. Installation then verified and locked the existing destinations, checked the backups, and updated six changed files within the 39-file manifest. No new destination was added. Original executable, original bot/dependencies, shared database/configuration and personal mod configuration hashes remained unchanged. Installation failure would restore the held prior bytes.

Post-install verification matched all 39 file hashes and source commit. The installed uninstaller's read-only preview recognized exactly 39 managed files, preserved zero unknown collisions and removed zero files. Actual uninstall was performed only in the temporary fixture.

Local evidence is retained under ignored `out/native-network-validation/`: `fresh-release.log`, `fresh-provenance.log`, `fresh-client-runtime.log`, `preupdate-backup.json`, `actual-layout-preview.log`, `actual-install.json`, `installed-verification.json`, and `pf-234c05ed53fa/validation-report.json`. The fixture replay script is `PackageFixtureValidation-2187cb3.ps1` in that directory.

Both physical computers must use this new v2-capability package. Physical LAN/Tailscale testing and second-device cold AI startup remain pending. This work did not publish a new GitHub Release; the existing public alpha package does not contain this change. This documentation commit records delivery of the built source above and does not relabel its artifacts as built from a later commit.
