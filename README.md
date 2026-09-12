# YGOPro 撤回版

独立启动的 Windows 修改版客户端和配套 WindBot，直接使用原 YGOPro 目录资源。

**测试版下载：[v0.1.0-alpha.2 · Windows x64 ZIP](https://github.com/SuperDuckonhead/duel-undo-mod/releases/download/v0.1.0-alpha.2/ygopro-undo-v0.1.0-alpha.2-win-x64.zip)** · [版本说明与已知问题](https://github.com/SuperDuckonhead/duel-undo-mod/releases/tag/v0.1.0-alpha.2)

需要已有完整 YGOPro 和 WindBot 资源，适配 AI 需要 .NET Framework 4.8。关闭撤回版及其 AI，将 ZIP 解压到原 `ygopro.exe` 所在目录，然后启动 `ygopro-undo.exe`；详细步骤见 [安装说明](docs/INSTALL.md)。

支持卡组编辑撤回，以及单人练习、AI、本机双开、双方兼容客户端的局域网两人对战撤回。人工对战支持 Single 和 Match；Match 只可撤回当前未结束小局，换备及下一局沿用原版流程。朋友局逐次确认；AI 仍为 Single，Tag 和观战暂未开放。2026-09-12 用户确认两台电脑统一卡表后能够联机；完整跨设备撤回、Match 局间流程和人工视觉验收仍需逐项验证。

最近修复了有计时对局的确认消息丢失、撤回同意/拒绝按钮及窗口缩放问题，见 [修复与回归记录](tests/timer-consent-ui.md)。创建房间时的网络错误仍在排查，当前已知问题见 [兼容与验收状态](docs/COMPATIBILITY.md)。

alpha.2 包含原版联机流程调整、Match 支持及随包卸载工具。普通响应和计时复用原处理方法；客机不扫描效果脚本。双方须一起更新，alpha.1 及此前只有 Single 的旧包不能混用。卸载工具确认后先备份再移除可验证的 mod 文件，默认保留原游戏资源、配置和日志。

**当前联机限制：双方实际加载的完整逻辑卡表仍须一致。** 即使只是其中一台多出未使用的新卡，也会被拒绝入房；这是当前 mod 的额外限制，原版没有这项完整卡表比较。更新 mod 不会同步原游戏的 `cards.cdb` 和扩展卡库。遇到该提示时，先确认同一 Match 包及同一套卡库，并完全重启两端游戏。此项限制的简化已列入下方待办。

- [安装与卸载](docs/INSTALL.md)
- [构建与打包](docs/BUILD.md)
- [兼容及测试范围](docs/COMPATIBILITY.md)
- [更新日志](docs/RELEASE-NOTES.md)
- [待更新功能](docs/ROADMAP.md)
- [许可与来源](docs/THIRD-PARTY.md)
- [规格与任务](openspec/changes/duel-undo-mod/tasks.md)
- [验收矩阵](tests/acceptance.csv)

## 待更新功能

- **简化联机检查**：沿用原版入房及卡组检查，保留必要的撤回协议兼容检查，取消完整卡表等值要求，并区分版本、卡片数据与握手错误。
- **卡组测试功能**：增加面向玩家的卡组测试能力；具体测试入口、操作方式及结果展示待后续设计。

这两项均尚未实装于 alpha.2，计划与验收方向见 [待更新功能](docs/ROADMAP.md)。

## 获取源码

可直接检出当前开发分支：

```powershell
git clone --branch codex/duel-undo https://github.com/SuperDuckonhead/duel-undo-mod.git
cd duel-undo-mod
```

按 [构建与打包](docs/BUILD.md) 准备固定依赖并生成客户端与 WindBot。原资源不进入 Git；`.cache` 和 `out` 为可再生成的开发输出。alpha 测试包已在 [Releases](https://github.com/SuperDuckonhead/duel-undo-mod/releases) 提供，稳定版待完成验收后发布。
