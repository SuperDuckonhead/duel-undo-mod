# Match 候选包交付（2026-09-12）

功能及回归范围见 [Match 测试记录](match-undo.md)。本次没有发布新的 GitHub Release；以下是本机构建并安装、可发给第二台电脑的同版本增量包。

## 构建与文件

| 项目 | 结果 |
|---|---|
| 构建源码 | `4c2cccc230fb126cc9ec5ce00f55cb7de7cf2e90` |
| 全新构建目录 | `F:/MyCardLibrary/ygopro/dev/undo-m1` |
| ZIP | `F:/MyCardLibrary/ygopro/dev/packages/ygopro-undo-4c2cccc-win-x64.zip` |
| 大小/文件数 | 7,474,038 字节；39 个文件 |
| ZIP SHA-256 | `7f667317b98c97839cbce618fe25c7c7274b139783f7e512495996a482d4ce07` |
| 客户端 SHA-256 | `04c588e61854070ae76e9746da0319efc20168ec9cdff7b4fecc8df1314ac761` |
| WindBot SHA-256 | `88d0ea9efc7af7ab402c3f075c5b4eefae138cade1acd6c6b030b131bf969131` |

19 个固定依赖归档经过离线校验，Windows PowerShell 5.1 从新目录运行完整 Release 构建，没有复用旧对象。新目录默认 CTest 23/23 通过；适配 WindBot 和其测试程序重新构建，`UndoTests.exe --suite all` 通过。包生成后只读构建溯源校验通过，39 个文件绑定上述源码与哈希；交付目录同时保存 `.sha256` 文件。

实际新 EXE 从无关工作目录、非 ASCII 路径和 Windows 快捷方式初始化并正常退出；258 字符路径探针通过，原程序/配置/依赖未改变。

新编译客户端对象与新 WindBot 的实际 AI MainLoop 补验通过：正常召唤、按钮撤回、再次召唤、Ctrl+Z、列表悬停及正常关闭。首次补验在测试驱动看到 `CanUndo()` 后、界面按钮刷新前就点击，触发测试自身的 `button unavailable: undo`；仅修正测试驱动等待实际按钮可见且启用，保留全局截止时间和原生帧等待，客户端与包字节未变。首次失败与修正后日志分别保存在 `out/match-validation/fresh-ai-mainloop.log` 和 `fresh-ai-mainloop-corrected.log`，最终驱动绑定了新构建的实际对象、EXE 与 bot 哈希。

## 带倒计时 Match

额外独立测试将 Match 房间计时设为 180 秒，完整双进程 2:0 通过。两端第二局均重置为 180 秒，观察到正常减少至 178/179 秒，并完成拒绝后继续、准备失败取消、成功撤回及新召唤、保存每局录像。双方两局录像 SHA-256 全部一致。

该补验使用 `out/match-validation/timed-match-driver/` 中的临时驱动/头文件，只调整计时与验证日志，保留真实主循环等待。原 tests 和生产源码哈希未变；准确副本/EXE 哈希、退出码和录像比对保存在 `final-timed-match-source-hashes.json`、`final-timed-match-result.json` 与 `final-timed-match-replay-hashes.json`。本次 4 组完整 Match 共 9 局，18 份录像按局双方全部一致。

## 安装、卸载与恢复

精确 ZIP 在独立夹具中通过 491/491 检查：校验全部 39 个文件，只读预览不写入，卸载恰好 39 个并备份，再恢复全部 39 个且哈希一致。18 个原程序/配置/卡组/资源哨兵保持不变。夹具记录为 `out/match-validation/pf-a707a3a836a3/validation-report.json`；没有在真实游戏目录执行卸载。

安装前独立核对旧 `2187cb3` ZIP 哈希及其 39 个已安装文件，连同个人 mod 配置备份到 `F:/MyCardLibrary/ygopro/dev/backups/before-match-20260912-143806`。安装程序持有旧文件排他句柄，核对所有权与备份后，仅更新 39 文件清单中变化的 6 个文件，没有新增目的路径；失败时可恢复持有的原字节。原 EXE、原 WindBot/依赖、共享数据库/配置和个人 mod 配置的哈希均未改变。

安装后真实目录 `F:/MyCardLibrary/ygopro` 的全部 39 个文件及构建源码复核一致。已安装卸载器的只读预览识别 39 个文件、移除 0 个。证据位于 `out/match-validation/actual-install.json`、`installed-verification.json`、`preupdate-backup.json` 及 `installed-uninstall-preview.log`。

双方需要一起使用这个恢复格式 2 的新包。人工 Match 可选；AI 仍为 Single，撤回仅作用于当前未结束小局。两台物理电脑的 LAN/Tailscale 和人工操作验收仍为 pending；首次 AI 启动等待和此前记录的建房问题没有在本次宣称修复。

后续测试/文档提交仅记录该已构建源码的交付，不将现有包重新标成来自后续提交。
