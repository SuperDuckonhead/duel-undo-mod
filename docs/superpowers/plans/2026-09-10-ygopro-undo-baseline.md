# YGOPro 源码基线与测试入口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 得到可追溯、可在现有资源包上运行的源码基线，并建立后续测试入口。

**Architecture:** 在安装目录 dev/duel-undo-mod 下建立独立 Git 根，导入固定提交的源码。先原样构建并验证原资源兼容性，再增加撤回模块。

**Tech Stack:** Windows PowerShell、Git、固定基线的 Premake/MSBuild、C++17、CMake/CTest、WindBot .NET Framework 4.8。

**Spec:** [设计](../../../openspec/changes/duel-undo-mod/design.md)、[规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)。

## Global Constraints

- 开发 Git 根：F:/MyCardLibrary/ygopro/dev/duel-undo-mod；运行资源根：F:/MyCardLibrary/ygopro。
- 当前只有安装包，未证明任何上游提交与本地 1.036.2 二进制对应；不能用 master 或版本号代替来源证据。
- 不把原 pics、deck、script、expansions、pack、数据库、AI 数据和个人配置导入开发仓库。
- 新产物使用 ygopro-undo.exe、WindBot/WindBot-undo.exe；原程序与共享依赖不覆盖。
- 本计划所有源码路径均相对于开发 Git 根。获取源码、构建和 Git 提交属于执行阶段，本次不执行。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| tools/Measure-InstalledBaseline.ps1、docs/baseline.md | 采集二进制指纹、来源及兼容结果。 |
| sources.lock.json、tools/Test-SourceLock.ps1 | 锁定客户端、内核、AI 和依赖的真实提交。 |
| client/、client/ocgcore/、bot/ | 固定提交的源码及原许可证。 |
| .gitignore、build-profile.json、tools/Build.ps1 | 排除资源/输出；记录并执行已验证构建命令。 |
| CMakeLists.txt、tests/test_support.h、tests/harness_smoke.cpp | 无窗口 C++ 测试入口。 |
| tests/README.md、tests/fixtures/、tests/results/ | 输入与结果约定；results 不提交。 |

B1 锁定后核实以下接入路径：client/gframe/deck_con.cpp、deck_con.h、deck_manager.h、single_duel.cpp、single_duel.h、single_mode.cpp、duelclient.cpp、network.h、game.cpp、game.h、data_manager.cpp、image_manager.cpp，以及 bot/Program.cs、bot/Game/GameAI.cs、bot/Game/GameBehavior.cs、bot/Game/AI/Executor.cs、bot/WindBot.csproj。它们是已查上游的参考落点，不能假称本地已有这些源码。DuelMode 的参考定义在 network.h，不创建虚构的 duel_mode.h。

### Task B1: 锁定二进制来源与源码证据

**Files:** 新建 tools/Measure-InstalledBaseline.ps1、tools/Test-SourceLock.ps1、sources.lock.json、docs/baseline.md。

**Interfaces:** 采集脚本参数 -RuntimeRoot、-OutFile；输出 relativePath、fileVersion、sha256、peMachine。锁文件 components 每项必须有 name、url、commit、evidenceUrl、evidenceNote、destination。commit 是真实 40 位 Git SHA。校验脚本参数 -Path，失败退出非零。

- [ ] **Step 1：先创建独立开发根并初始化 Git，仅在这个空源码根执行后续命令；写无效锁定检查。**

~~~powershell
$bad = Join-Path $env:TEMP 'ygopro-undo-invalid-lock.json'
'{"components":[{"name":"client","commit":"master"}]}' |
    Set-Content -LiteralPath $bad -Encoding UTF8
powershell -NoProfile -File tools/Test-SourceLock.ps1 -Path $bad
if ($LASTEXITCODE -eq 0) { throw 'Branch name accepted as fixed source' }
~~~

首次因脚本缺失失败；实现后应因提交/证据无效失败，不能把工具缺失当作通过。

- [ ] **Step 2：实现只读采集。**

~~~powershell
param([Parameter(Mandatory)][string]$RuntimeRoot,
      [Parameter(Mandatory)][string]$OutFile)
$ErrorActionPreference = 'Stop'
$rows = foreach ($relative in 'ygopro.exe','Bot.exe','WindBot/WindBot.exe') {
    $path = Join-Path $RuntimeRoot $relative
    $bytes = [IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    [ordered]@{
        relativePath = $relative
        fileVersion = (Get-Item -LiteralPath $path).VersionInfo.FileVersion
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        peMachine = ('0x{0:X4}' -f [BitConverter]::ToUInt16($bytes, $pe + 4))
    }
}
$rows | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutFile -Encoding UTF8
~~~

- [ ] **Step 3：追溯安装 README 的发布地址、发布包清单、子模块和 WindBot 版本。** 每个组件记录实际提交与证据，不填写虚构 SHA。无法证明精确对应时明确记录差异；B2 兼容验证通过后可称“兼容重建基线”，不能宣称是原二进制确切源码。Bot.exe 记录启动参数职责，适配客户端直接启动新 AI，不无依据修改旧启动器。
- [ ] **Step 4：实现锁定校验并重跑无效输入。**

~~~powershell
param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
$lock = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
if (-not $lock.components) { throw 'No components' }
foreach ($item in $lock.components) {
    foreach ($key in 'name','url','commit','evidenceUrl','evidenceNote','destination') {
        if ([string]::IsNullOrWhiteSpace([string]$item.$key)) { throw "Missing $key" }
    }
    if ($item.commit -notmatch '^[0-9a-f]{40}$') { throw 'Full Git SHA required' }
    if ([IO.Path]::IsPathRooted($item.destination) -or
        $item.destination -match '(^|[\\/])\.\.([\\/]|$)') { throw 'Unsafe destination' }
}
~~~

- [ ] **Step 5：真实锁文件校验成功；把来源/差异写入 docs/baseline.md。** 没有证据则继续追溯，不将 B1 标为完成。
- [ ] **Step 6：在 Step 1 创建的开发 Git 根内提交。**

~~~powershell
git add tools/Measure-InstalledBaseline.ps1 tools/Test-SourceLock.ps1 sources.lock.json docs/baseline.md
git commit -m "docs: pin source provenance for undo development"
~~~

### Task B2: 导入源码并完成原样兼容构建

**Files:** 新建 client/、bot/、.gitignore、build-profile.json、tools/Build.ps1；更新 docs/baseline.md。

**Interfaces:** Build.ps1 -Target Client|Bot|Tests|All -Configuration Debug|Release。build-profile.json 固定 compiler、architecture、framework、dependencies 和 commands；每条 command 有 program、workingDirectory、arguments、targets、configuration。输出 Gate-A：来源、构建、资源兼容均通过。

- [ ] **Step 1：在 B1 已创建的开发 Git 根写 .gitignore。** 忽略 .cache/、out/、tests/results/、*.user、本地运行日志；排除运行资源路径和个人配置。复制本次 openspec 与 docs/superpowers/plans 规划文档进入开发根，不复制其他安装内容。
- [ ] **Step 2：先运行源码存在检查，预期失败；按锁文件获取并导入后重跑通过。**

~~~powershell
if (-not (Test-Path -LiteralPath 'client/gframe/deck_con.cpp')) { throw 'Client source missing' }
$bad = git ls-files | Where-Object {
    $_ -match '(^|/)(pics|deck|script|expansions|pack|Decks|Dialogs)/' -or
    $_ -match '(^|/)(cards\.cdb|system(-undo)?\.conf|bots\.json)$'
}
if ($bad) { throw ('Runtime assets tracked: ' + ($bad -join ', ')) }
~~~

获取方法：git clone --no-checkout 到被忽略的 .cache/sources；git archive --format=tar --output 导出锁定 SHA；tar -xf 解到 destination。子模块按各自固定 SHA 单独导入，保留许可证。不要用 PowerShell 文本管道传递二进制 archive。

- [ ] **Step 3：检查编译工具。** 先 Get-Command，再用 Visual Studio Installer 的 vswhere 定位未加入 PATH 的工具；根据固定提交说明准备编译器、SDK、Premake、Lua、改版 Irrlicht 及其他锁定依赖。不得直接照抄当前 master 的依赖版本。每项下载/构建独立记录。
- [ ] **Step 4：原样构建客户端和 AI，把成功的命令参数数组录入 build-profile.json，实现命令运行器。**

~~~powershell
param([ValidateSet('Client','Bot','Tests','All')][string]$Target = 'All',
      [ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$profile = Get-Content -LiteralPath (Join-Path $root 'build-profile.json') -Raw |
    ConvertFrom-Json
$selectedCount = 0
foreach ($command in $profile.commands) {
    if ($Target -ne 'All' -and $command.targets -notcontains $Target) { continue }
    if ($command.configuration -and $command.configuration -ne $Configuration) { continue }
    ++$selectedCount
    Push-Location (Join-Path $root $command.workingDirectory)
    try {
        $buildProgram = [string]$command.program
        $buildArguments = @($command.arguments)
        & $buildProgram @buildArguments
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $($command.program)" }
    } finally { Pop-Location }
}
if ($selectedCount -eq 0) { throw 'No matching build commands' }
~~~

program 从固定工具目录或 PATH 解析，不提交开发机的绝对工具路径。程序参数保存为数组，不能拼接 shell 代码。

- [ ] **Step 5：逐项运行 smoke 并记录结果。** 编辑并另存专用测试卡组、单人场景、本地双开、AI 对局；核对 Bot 启动、AI 列表/执行器和扩展卡。AI/普通建房各运行两个开局选项的四种组合，检查实际开局顺序和校验范围。必要兼容修复必须单独记录，不宣称仍为完全未改动源码。
- [ ] **Step 6：记录固定源码的接入符号映射。** 文件或签名与其他计划不同，先更新该计划映射再开始对应任务；不得改变行为规格来适配源码。原程序和共享依赖 SHA-256 不变，原版也能启动才通过 Gate-A。
- [ ] **Step 7：提交导入与构建记录。**

~~~powershell
git add .gitignore sources.lock.json build-profile.json tools/Build.ps1 client bot docs/baseline.md openspec docs/superpowers/plans
git commit -m "build: import verified client and bot source baseline"
~~~

### Task B3: 建立无窗口测试与证据格式

**Files:** 新建 CMakeLists.txt、tests/test_support.h、tests/harness_smoke.cpp、tests/README.md；更新 build-profile.json。

**Interfaces:** add_undo_test(name source) 注册一个 C++17 程序及同名 CTest；CHECK 在 Release 下也有效。真实引擎测试链接 B2 的固定内核/Lua，不能用纯逻辑测试代替。

- [ ] **Step 1：建立失败用例及断言。**

~~~cpp
// tests/test_support.h
#pragma once
#include <stdexcept>
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
~~~

~~~cpp
// tests/harness_smoke.cpp
#include "test_support.h"
int main() { CHECK(false); }
~~~

- [ ] **Step 2：建立 CMake，运行并确认用例失败。**

~~~cmake
cmake_minimum_required(VERSION 3.20)
project(ygopro_undo_tests LANGUAGES CXX)
enable_testing()
function(add_undo_test name source)
    add_executable(${name} ${source})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE tests client/gframe)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
add_undo_test(harness_smoke tests/harness_smoke.cpp)
~~~

~~~powershell
cmake -S . -B out/tests
cmake --build out/tests --config Debug
ctest --test-dir out/tests -C Debug -R '^harness_smoke$' --output-on-failure
~~~

预期非零且指向 CHECK(false)，不是配置/链接工具缺失。

- [ ] **Step 3：将 CHECK(false) 改为 CHECK(2 + 2 == 4)，重跑同一命令，预期通过。** 将测试命令加入 build-profile.json 的 Tests 目标。
- [ ] **Step 4：建立结果格式。** tests/results/<case>.json 固定字段 case、sourceCommit、resourceDigest、mode、passed、elapsedMs、peakWorkingSetBytes、failure；结果不提交。自建可公开 fixture 存 tests/fixtures；实际个人卡组和完整私人对局日志留在本地。集成用例失败需要同时记录初始种子、有效响应和第一个分歧点。
- [ ] **Step 5：提交测试入口。**

~~~powershell
git add CMakeLists.txt tests/test_support.h tests/harness_smoke.cpp tests/README.md build-profile.json
git commit -m "test: add native undo test harness"
~~~

## 完成条件

Gate-A 通过，测试入口能先失败再通过。这里不证明对战撤回可行；引擎与 AI 的 Gate-B 由 core 的 C2/C3 和 windbot 的 W1 共同给出。
