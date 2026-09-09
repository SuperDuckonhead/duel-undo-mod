# 从源码构建撤回版

在 Windows x64 的独立源码目录运行；原游戏目录只在运行测试时提供资源。源码、第三方来源与 SHA 校验值见 sources.lock.json，编译参数见 build-profile.json。当前工具链为固定的 LLVM-MinGW UCRT x64、Premake、CMake、.NET SDK 和 .NET Framework 4.8 参考程序集。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Bootstrap.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Target All -Configuration Release
```

Bootstrap 只下载并校验锁定的工具及库，不导入覆盖已修改的客户端/内核/WindBot 源码。-Offline 使用已校验缓存。编译输出位于源码目录中已忽略的 client/bin、client/obj、client/build、bot/bin、bot/obj、bot-tests/bin、bot-tests/obj 和 out 等构建目录。初次原生构建耗时较长；正常使用无需安装编译器。

交付文件取自 out/client/Release/ygopro-undo.exe，以及 out/bot/Release 下的 WindBot-undo.exe、WindBot-undo.exe.config、undo-deps/x86/sqlite3.dll、undo-deps/x64/sqlite3.dll。Build-Bot.ps1 保留普通基线构建；Build-BotTests.ps1 会单独构建适配版及其测试，Build.ps1 的 All/Bot 目标包含两者。不要把普通 Bot.exe/WindBot.exe 或测试程序混入增量包。

原生客户端静态链接 C++ 运行库，PE 导入检查只允许 Windows 系统库，渲染使用 OpenGL。程序不是原安装包 MSVC 产物的逐字节复现。

## 使用原资源测试

从源码根运行，RuntimeRoot 换成自己的完整原游戏目录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Prepare-SmokeRuntime.ps1 -RuntimeRoot F:/MyCardLibrary/ygopro -OutputName baseline-runtime
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-SingleIntegration.ps1 -Configuration Release -RuntimeRoot F:/MyCardLibrary/ygopro
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-RoomClientIntegration.ps1 -Configuration Release -Pair
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-RoomClientIntegration.ps1 -Configuration Release -FreePair
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-RoomClientIntegration.ps1 -Configuration Release -Ai
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-UndoBotHost.ps1 -Configuration Release -RuntimeRoot F:/MyCardLibrary/ygopro
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-AiHostHistory.ps1 -Configuration Release -RuntimeRoot F:/MyCardLibrary/ygopro -All -Workers 2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-EditorIntegration.ps1 -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-SingleIntegration.ps1 -Configuration Release
```

各脚本的默认资源位置针对开发机器；移植时先查看对应 param 参数。测试临时文件位于源码 out 下，具体位置由各脚本输出；自动创建的资源链接不可当作副本递归清理。测试不代替两台设备的人工局域网验收，参见 tests/acceptance.csv 和 COMPATIBILITY.md。

## 生成本地候选包

另建一个干净检出用于候选构建，先运行 Bootstrap.ps1，再运行下方 Prepare-Release.ps1 -Build。此目录须尚无 build/bin/obj/out 构建产物；不要在开发目录运行过 Build 后事后给旧产物生成来源证明。脚本会调用 Build.ps1 -Target All -Configuration Release，核对构建前后源文件不变，再准备精确新增文件及哈希清单。无 -Build 时仅校验已生成的匹配结果，不重编译或覆盖产物。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Prepare-Release.ps1 -Build
```

然后在该候选构建目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-InstallLayout.ps1 -Manifest release-files.json -RuntimeRoot F:/MyCardLibrary/ygopro
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Package.ps1 -Manifest release-files.json -OutFile out/packages/ygopro-undo-candidate.zip
```

Package 拒绝已存在的 ZIP，重跑需另取新文件名。release-files.json 是精确文件/哈希清单，不可用旧版字符串白名单替代。构建清单标注源码提交、依赖及产物哈希；ZIP 条目必须与清单严格一致。当前工作只准备本地源码与候选 ZIP，不创建远程仓库或发布 Release。
