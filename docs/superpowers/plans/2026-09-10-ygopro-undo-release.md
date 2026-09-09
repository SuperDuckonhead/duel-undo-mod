# 同目录运行与增量发行准备 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成可直接放入原安装目录的撤回版客户端和适配 WindBot，并提供可追溯的本地增量 ZIP。

**Architecture:** 程序以自身安装目录解析共享资源，新配置/依赖按名称隔离。打包由新增文件白名单驱动，构建和源码清单关联；先完成本地验收，实际 GitHub 发布作为另一个明确任务。

**Tech Stack:** Windows C++/Win32、.NET Framework 4.8、PowerShell、固定构建清单、ZIP、OpenSpec 验收映射。

**Spec:** [同目录与发行设计第 7–8 节](../../../openspec/changes/duel-undo-mod/design.md)、[发行行为规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)。

## Global Constraints

- 源码根：F:/MyCardLibrary/ygopro/dev/duel-undo-mod；运行根：F:/MyCardLibrary/ygopro。
- 独立程序名称固定 ygopro-undo.exe、WindBot/WindBot-undo.exe、WindBot/WindBot-undo.exe.config。
- 原资源 pics、deck、script、expansions、pack、cards.cdb、字体/纹理/声音和 AI 数据直接复用，不复制。
- 新配置 system-undo.conf 首次生成；兼容原 system.conf 默认值，但不改写原配置。
- 不覆盖原 exe/DLL/资源；新增依赖使用独立名称或专用目录。
- 共享 .ydk 只由用户主动保存改写；配置双开写入需进程间协调与原子替换。
- Git 只管理开发源码、锁定依赖、测试和构建文档；ZIP 只包含新增文件清单。
- 本计划生成本地候选包；不创建 GitHub 仓库、不 push、不发布 Release、不预设账号。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| client/gframe/undo/runtime_paths.h、.cpp | EXE 根目录、资源诊断和 AI 启动路径。 |
| client/gframe/undo/config_store.h、.cpp | 首次导入、改版保存、并发合并与原子替换。 |
| client/gframe/game.cpp、data_manager.cpp、image_manager.cpp | 入口/配置与原资源加载器接入。 |
| bot/Program.cs、bot/WindBot.csproj、bot/App.config | AI 独立名称、资源根、依赖探测；实际 config 文件按 B2 映射。 |
| tools/Test-InstallLayout.ps1、tools/Package.ps1、release-files.json | 路径白名单、增量打包与指纹。 |
| tests/runtime_paths_tests.cpp、tests/config_store_tests.cpp、tests/acceptance.csv | 路径、并发配置、全部场景结果。 |
| docs/BUILD.md、INSTALL.md、COMPATIBILITY.md、RELEASE-NOTES.md、THIRD-PARTY.md | 构建、安装/卸载、兼容性、许可和候选发行文案。 |
| tests/performance.md、tools/Measure-Undo.ps1 | 历史长度/耗时/内存和资源释放报告。 |

### Task R1: 程序命名、资源根与配置隔离

**Files:** 新建 runtime_paths.h/.cpp、config_store.h/.cpp、两个测试文件；修改 game.cpp、data_manager.cpp、image_manager.cpp、bot/Program.cs、WindBot.csproj 和构建目标名。

**Interfaces:**
- std::filesystem::path ExecutableRoot()：从 GetModuleFileNameW 动态增长缓冲区读取 EXE 路径，返回父目录。
- std::filesystem::path ResourcePath(const std::filesystem::path& root,const std::filesystem::path& relative)：拒绝绝对 relative 与 .. 越界，不改变原扩展加载优先级。
- ConfigStore(root).Load() -> std::map<std::string,std::string>：优先新配置，否则兼容读取原设置。
- ConfigStore::Save(const std::map<std::string,std::string>& changedKeys) -> bool：锁内合并最新新配置，仅写本实例修改键；失败保留旧文件。
- 客户端 AI 启动固定到 root/WindBot/WindBot-undo.exe，工作目录 root/WindBot，并传明确 runtime root 和会话信息。

- [ ] **Step 1：添加路径与首次配置行为的失败测试。**

~~~cpp
const auto root = std::filesystem::path(L"F:/测试目录/ygopro");
CHECK(undo::ResourcePath(root,L"pics/100.jpg") == root / L"pics/100.jpg");
bool rejected = false;
try { undo::ResourcePath(root,L"../outside"); }
catch (const std::exception&) { rejected = true; }
CHECK(rejected);
~~~

配置测试在 tests/results/config-root 写入专用 system.conf，Load 读出兼容字段；Save 后原文件字节不变，新文件完整可解析。测试不得向真实安装 system.conf 写入。

- [ ] **Step 2：运行 runtime_paths_tests/config_store_tests；预期当前程序仍依赖启动工作目录并写旧配置。**
- [ ] **Step 3：实现路径和命名。**

~~~cpp
std::filesystem::path ResourcePath(
    const std::filesystem::path& root, const std::filesystem::path& relative) {
    if (relative.has_root_name() || relative.has_root_directory())
        throw std::invalid_argument("Rooted resource suffix");
    for (const auto& part : relative)
        if (part == L"..") throw std::invalid_argument("Resource escapes root");
    return (root / relative).lexically_normal();
}
~~~

客户端初始化最早阶段确定 root，并将原相对资源入口锚定该 root；扩展/pack/script 优先级沿用原加载器，不能把 Path 拼接替换成新的优先级规则。WindBot AssemblyName 改为 WindBot-undo；客户端构建目标名改 ygopro-undo，输出不落到原 exe 路径。
- [ ] **Step 4：实现配置锁与原子替换。** 命名 mutex 名由规范化安装绝对路径的 SHA-256 派生；持锁后重新读取最新 system-undo.conf，合并 changedKeys，再写同目录随机临时文件、FlushFileBuffers，已有目标用 ReplaceFileW，无目标用 MoveFileExW；任一步失败保留旧配置并清理本次临时文件。原 system.conf 从不作为写入目标。可写日志/历史按 session 隔离。
- [ ] **Step 5：隔离新依赖。** 客户端优先沿用兼容静态依赖或独立导入名；不能依赖 main 之后的搜索路径修改修复启动前的隐式 DLL 冲突。AI 托管私有依赖放 WindBot/undo-deps（.NET probing 限同 AppBase 子目录），必要 native 依赖按架构用已验证绝对路径加载。每个新增文件进 release-files.json；不替换旧 WindBot 的 x86/x64 共享库。
- [ ] **Step 6：从其他工作目录、中文路径、快捷方式启动，验证原卡图/扩展卡/脚本/AI 数据可用。** 缺失 cards.cdb、脚本或 AI 数据时提示实际查找路径。双实例分别修改不同设置后同时退出，新配置包含两者修改且可解析；原配置/原程序 SHA-256 不变。
- [ ] **Step 7：保留并回归编辑入口和 AI/普通建房两项开局设置的四种组合；运行一次撤回确认不重新洗牌。提交。**

~~~powershell
git add client/gframe/undo/runtime_paths.h client/gframe/undo/runtime_paths.cpp client/gframe/undo/config_store.h client/gframe/undo/config_store.cpp client/gframe/game.cpp client/gframe/data_manager.cpp client/gframe/image_manager.cpp bot/Program.cs bot/WindBot.csproj bot/App.config tests/runtime_paths_tests.cpp tests/config_store_tests.cpp release-files.json
git commit -m "feat: run undo executables beside shared game resources"
~~~

### Task R2: 完整场景与性能验收

**Files:** 新建 tests/acceptance.csv、tests/performance.md、tools/Measure-Undo.ps1；汇总各模块测试结果。

**Interfaces:** acceptance.csv 每个 OpenSpec Scenario 一行：requirement、scenario、mode、testCommandOrSteps、evidenceFile、result；result 只能 pending/pass/fail。Measure-Undo.ps1 -RuntimeRoot -Cases -OutFile 记录 sourceCommit、resourceDigest、responses、undoCount、elapsedMs、workingSetBytes、handleCount、childProcessCount。

- [ ] **Step 1：先建立“有未验收场景则失败”的检查。**

~~~powershell
$rows = Import-Csv -LiteralPath 'tests/acceptance.csv'
if (-not $rows -or ($rows | Where-Object { $_.result -ne 'pass' })) {
    throw 'Acceptance is incomplete'
}
foreach ($row in $rows) {
    if (-not (Test-Path -LiteralPath $row.evidenceFile)) { throw 'Evidence missing' }
}
~~~

另将 Scenario 标题集合与规格逐项比较，缺失/重复均失败；不能用相同 result 文件伪装未运行模式。
- [ ] **Step 2：运行各模块已定义测试与真实流程。** 编辑增删/拖拽/保存后撤回；单人、AI、双开、两设备 LAN；双方发起、连续撤回、新路线、两个开局选项。每个规格场景链接到结果，不自动把待办改为 pass。
- [ ] **Step 3：运行网络、脚本变化、AI 分歧及并发输入故障。** 要求准备失败保留原局，提交不明统一暂停；网络记录没有隐藏卡信息额外泄露。
- [ ] **Step 4：测量短、中、长历史。** 使用固定 fixture 中实际有效响应数量划分，至少 10、100、1000 响应样本；不足时补建合法长历史，不复制无效响应凑数量。每个目标重复重建 20 次，记录耗时分布、进程内存、句柄及候选 AI 数。
- [ ] **Step 5：检查释放与上限行为。** 20 次完成/失败后候选句柄与 AI 进程数回到基线；查看内存是否持续单向增长并定位泄漏。记录实际性能而不编造毫秒指标，不静默裁剪有效历史。若过长恢复导致资源不足，明确失败并保留原局。
- [ ] **Step 6：全部验收有真实证据且无失败再提交。** 无第二设备时保留 LAN pending，不标可发行。

~~~powershell
git add tests/acceptance.csv tests/performance.md tools/Measure-Undo.ps1
git commit -m "test: complete undo acceptance and resource lifetime checks"
~~~

### Task R3: 可追溯源码与本地增量 ZIP

**Files:** 新建 tools/Test-InstallLayout.ps1、tools/Package.ps1、docs/BUILD.md、INSTALL.md、COMPATIBILITY.md、RELEASE-NOTES.md、THIRD-PARTY.md；完善 release-files.json。

**Interfaces:**
- release-files.json.files 每项 source、destination、sha256、license；source 相对 out/release，destination 相对玩家安装根。
- Package.ps1 -Manifest release-files.json -OutFile out/packages/ygopro-undo-candidate.zip；只从白名单读取/复制到空 staging，再压缩。
- Test-InstallLayout.ps1 -Manifest -RuntimeRoot：拒绝原文件冲突、路径越界、资源/配置进入 ZIP；报告新增项，不修改原目录。
- build-manifest.json 记录源 commit、组件锁、构建配置、产物 SHA-256；候选包不要求创建远程标签。

- [ ] **Step 1：写拒绝把原图目录/配置打包的用例。**

~~~powershell
$forbidden = @(
    'ygopro.exe','Bot.exe','WindBot/WindBot.exe',
    'system.conf','system-undo.conf','pics/100.jpg','deck/private.ydk','cards.cdb'
)
foreach ($path in $forbidden) {
    $allowed = $path -match '^(ygopro-undo\.exe|WindBot/WindBot-undo\.exe(\.config)?|WindBot/undo-deps/.+|undo-mod/.+)$'
    if ($allowed) { throw "Forbidden package destination allowed: $path" }
}
~~~

用测试 manifest 再调用 Test-InstallLayout.ps1，要求实际校验器退出非零；单独测试上述正则不能代替正式打包检查。
- [ ] **Step 2：实现严格白名单检查。** canonical destination 必须在 root 内，拒绝绝对路径、..、重复大小写冲突、符号链接/重解析点逃逸，原始存在文件只允许已验证为同一 mod 的受管新增项；首次安装不得覆盖其他文件。包不含开发者配置、私人卡组、完整资源库、.git 或源码构建缓存。
- [ ] **Step 3：实现清单驱动打包。**

~~~powershell
$manifest = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $sourcePath = Join-Path $releaseRoot $entry.source
    if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Artifact hash mismatch: $($entry.source)"
    }
    $targetPath = Join-Path $stagingRoot $entry.destination
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($targetPath)) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath
}
Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $OutFile
~~~

Package.ps1 定义参数 Manifest/OutFile；releaseRoot 固定开发根/out/release，stagingRoot 为开发根/out/staging 下本次 GUID 目录；在进入复制循环前执行 Step 2 的源/目标路径与清单校验。禁止在未知计算路径上递归删除；临时目录若需清理，先验证绝对路径位于 out/staging。
- [ ] **Step 4：编写实际构建、安装/卸载、兼容和许可证说明。** 安装只解压新增文件；卸载按清单删除新增项，可选删除 system-undo.conf/独立日志，不递归清理共享目录。写明朋友双方所需协议/构建兼容版本、AI 验证结果和当前性能。THIRD-PARTY 保留源码/依赖许可证与来源。
- [ ] **Step 5：从开发 Git 根的干净检出重新构建，核对锁文件和构建清单。** 源码必须足以重建，资源通过运行目录参数提供。git ls-files 检查无个人资源；ZIP 条目与白名单集合严格相等。安装候选后原版/撤回版各启动一次，按清单卸载后原版仍可用。
- [ ] **Step 6：汇总本地候选包、源码 commit 和验收证据，提交打包文档。**

~~~powershell
git add tools/Test-InstallLayout.ps1 tools/Package.ps1 release-files.json docs/BUILD.md docs/INSTALL.md docs/COMPATIBILITY.md docs/RELEASE-NOTES.md docs/THIRD-PARTY.md
git commit -m "build: prepare traceable incremental undo release package"
~~~

实际创建 GitHub 仓库、远程 push、发布标签/Release 均不在此步骤执行；后续明确发布任务直接使用这里经过检查的源码和候选包。

## 完成条件

所有规格场景通过，本地增量 ZIP 可同目录运行且可按清单卸载，原版与资源未被覆盖，源码/构建/产物一一对应。只写完文档或只生成 ZIP 不代表满足发行条件。
