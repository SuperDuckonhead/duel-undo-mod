# Portable WindBot baseline build

The pinned MyCard WindBot source builds on this machine without a system Visual Studio or .NET Framework Developer Pack. The build uses a repository-local .NET SDK and Microsoft's official .NET Framework 4.8 reference assemblies, and writes final artifacts only below `out/`.

## Inputs

- Source: `bot/`, imported from `mycard/windbot` commit `3a6a462828046e05793e8f74bf446c2d5c713e8c`.
- .NET SDK: 8.0.425, downloaded as `dotnet-sdk-8.0.425-win-x64.zip` from Microsoft's .NET distribution service. Archive size 285,157,731 bytes; SHA-256 `CD6EB1DF826DD168108E2C427673F9D627B7E0631963EEEDF0FA7AEF0913C53F`.
- .NET host used by the script: `.cache/tools/dotnet/dotnet.exe`, SHA-256 `111DA7B604CD196B49167CFEECDAEF4034B58CC7DE71A91CBF9E162C9E8B0931`.
- Reference assemblies: NuGet `Microsoft.NETFramework.ReferenceAssemblies.net48` 1.0.3 from `https://api.nuget.org/v3-flatcontainer/microsoft.netframework.referenceassemblies.net48/1.0.3/microsoft.netframework.referenceassemblies.net48.1.0.3.nupkg`. Package size 20,997,929 bytes; SHA-256 `8A7E348538E7EB91351696911689F49E3D4F63F8BAB517432BBE159B8B1104A2`.

The cache is intentionally untracked. These version, URL, size, and hash values should be copied into the root-owned dependency lock/profile.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-Bot.ps1 `
    -Configuration Release `
    -OutputDirectory out/baseline-bot
```

`Build-Bot.ps1` resolves its default SDK and reference-assembly paths relative to the repository root. Overrides may be absolute or repository-relative. The output path must resolve below this repository's `out` directory. Paths are passed as an argument array to MSBuild, including paths containing spaces.

The script invokes the imported solution as Release or Debug, Any CPU, targeting .NET Framework 4.8. `tools/Bot.Build.targets` removes copy items for runtime-owned files intentionally excluded from source import (`bots.json`, `Decks`, `Dialogs`, and `BotWrapper/bot.conf`). It does not alter WindBot source or behavior.

## Verified Release result

MSBuild 17.11.48 completed with exit code 0 and emitted:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Bot.exe` | 25,600 | `6D2221225AC24BDD458419C59C3637819EC48E2A5C0B9753EB01084C374E1326` |
| `Bot.exe.config` | 161 | `D61BDE901E7189CC97D45A1D4C4AA39D4C4DE2B68419773EC774338506D659AD` |
| `Bot.pdb` | 17,920 | `D170A62EDBC17322671A6B9EF49D71F6C77DD46122B2EE7CBAF76A1D023EEFD9` |
| `load-once.conf` | 83 | `F238B0BC7F0D7582540418CBEE7A0704C51655024B682734D5FD85D27A044ED0` |
| `WindBot.exe` | 2,254,848 | `FE8A6E1F481A2915AE2D7073AEF0DD83144A8E8A474B9A25541255FE55240046` |
| `WindBot.exe.config` | 279 | `F97D190912E6ED91BAE640DD26F2F3CE6C7229E03F97324C768957B270A41A71` |
| `x64/sqlite3.dll` | 3,285,504 | `AB57D0437795ECC757CB693F32EA224173FA9856594D95CFA6B5033E645CD1EC` |
| `x86/sqlite3.dll` | 2,572,288 | `1C2FCFA7632B6025829E3539142F1B7EBDBC5BB44D4FD6CC0F42F83715D2EB9F` |

A separate Debug build to `out/baseline-bot-debug` also completed with exit code 0.

The clean Release rebuild reports 43 warning lines and zero error lines: `MSB3884` once for the absent legacy `MinimumRecommendedRules.ruleset`, `CS0168` once, `CS0169` twice, `CS0414` 38 times, and `CS0649` once. The C# warnings are existing unused-variable or unused-field diagnostics in pinned source. They are recorded rather than broadly suppressed.

The generated executables have the same byte sizes as the installed baseline but different hashes. This build establishes source/toolchain reproducibility, not byte-for-byte reproduction of the original CI binary.

## Runtime boundary and remaining gate

No build output is copied to `F:/MyCardLibrary/ygopro/Bot.exe` or `F:/MyCardLibrary/ygopro/WindBot`. The build output deliberately lacks runtime Decks, Dialogs, `bots.json`, `bot.conf`, and `cards.cdb`; those remain runtime-owned inputs for later smoke testing.

Compilation does not complete Gate A. GUI smoke checks still need to verify original application startup, bot list loading, direct AI launch, actual AI duel connection, runtime resources/extensions, and the four starting-order combinations described in the B2 plan.