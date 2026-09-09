# Source baseline and compatibility evidence

The selected source commits are strong candidates for the installed September 1, 2026 MyCard package. They are **not a claim of byte-identical reproduction**. The original installation is F:/MyCardLibrary/ygopro; this independent development Git root is its dev/duel-undo-mod subdirectory.

See [sources.lock.json](../sources.lock.json) for immutable commits/archive checksums and [installed binary measurements](baseline/installed-binaries.json) for the executable hashes. No original executable, personal deck or runtime resource has been changed by source import.

| Component | Fixed source |
| --- | --- |
| Client | mycard/ygopro@1e8472b8bd51e1242be133189d547ac2f55eddaa |
| Core | mycard/ygopro-core@e04144d62499c17d0cfa8313f9742434ef99c3a7 |
| WindBot and original wrapper source | mycard/windbot@3a6a462828046e05793e8f74bf446c2d5c713e8c |
| Irrlicht | code.moenext.com/mycard/irrlicht-1.9@20f86d251624334d53585a1809ea4c7178f72a34 |
| miniaudio | mackron/miniaudio@9634bedb5b5a2ca38c1ee7108a9358a4e233f14d (0.11.25) |

Client evidence: the installed PE is x64, version1.036.2, timestamp2026-09-01 06:25UTC, with a MyCard CI PDB path. Installed README, strings.conf and lflist.conf match the selected client Git blobs. The source pins core and script commits; installed constant.lua, utility.lua and procedure.lua exactly match script revision5864b6f6e58d49738e0996b94e96655b51f420bf. These are source-linkage facts, not an exhaustive proof for every installed/custom script. [Client commit](https://github.com/mycard/ygopro/commit/1e8472b8bd51e1242be133189d547ac2f55eddaa).

WindBot evidence: the embedded build path names mycard/windbot, and the commit precedes the installed executable timestamp by2m24s. All70 shipped decks and20 dialogs match the merged upstream ancestry after newline normalization; App.config is byte-identical to WindBot.exe.config. All62 installed bots.json entries resolve to source executor attributes, installed decks and dialogs. Preserve the observed duplicate/hidden menu entries. [WindBot commit](https://github.com/mycard/windbot/commit/3a6a462828046e05793e8f74bf446c2d5c713e8c).

The original Bot.exe is a wrapper that hardcodes WindBot/WindBot.exe and takes command/hand/port arguments. The undo client must direct-launch the separately named WindBot-undo.exe (or an explicitly separate wrapper); do not overwrite or redirect the original Bot.exe.

Irrlicht caveat: production CI used a mutable master. The observed production ref is20f86d25 and its public mirror dates it2026-06-25. This is the selected fixed development dependency, but present-day ls-remote cannot prove the historical September1 ref. [Mirror commit](https://github.com/mercury233/irrlicht/commit/20f86d251624334d53585a1809ea4c7178f72a34).

Build facts and gates:

- The original client CI used Premake beta8, VS2026/MSVC, Release/x64, SSE2 and the dependency archives recorded in its [.gitlab-ci.yml](https://github.com/mycard/ygopro/blob/1e8472b8bd51e1242be133189d547ac2f55eddaa/.gitlab-ci.yml).
- This machine has no Visual Studio/Windows SDK. Portable CMake4.4.3, LLVM-MinGW20260908 and .NETSDK8.0.425 are confined to ignored .cache/tools. LLVM builds are compatibility builds, not MSVC binary reproductions.
- A clean WindBot/Wrapper rebuild succeeded with portable SDK MSBuild plus official net48 reference assemblies1.0.3; executable sizes match, hashes differ. Legacy Framework MSBuild cannot compile the source's modern C#.
- Client compilation and all GUI/single/AI/LAN runtime smoke are still pending; Gate-A is **not passed** merely from source fingerprints or the successful bot compilation.
- Gate-B (engine and AI deterministic candidate reconstruction) has not started. No undo functionality is claimed by this baseline.

Resource/source boundary: do not track bot/Decks or bot/Dialogs runtime data. Do track bot/Game/AI/Decks because it contains required C# executor source. Upstream dependencies remain reproducible from pinned sources/checksums; installed resource trees are neither copied nor treated as source dependencies.
