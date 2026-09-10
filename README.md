# YGOPro 撤回版

独立启动的 Windows 修改版客户端和配套 WindBot，直接使用原 YGOPro 目录资源。

**测试版下载：[v0.1.0-alpha.1 · Windows x64 ZIP](https://github.com/SuperDuckonhead/duel-undo-mod/releases/download/v0.1.0-alpha.1/ygopro-undo-v0.1.0-alpha.1-win-x64.zip)** · [版本说明与已知问题](https://github.com/SuperDuckonhead/duel-undo-mod/releases/tag/v0.1.0-alpha.1)

需要已有完整 YGOPro 和 WindBot 资源，适配 AI 需要 .NET Framework 4.8。关闭撤回版及其 AI，将 ZIP 解压到原 `ygopro.exe` 所在目录，然后启动 `ygopro-undo.exe`；详细步骤见 [安装说明](docs/INSTALL.md)。

支持卡组编辑撤回，以及单人练习、AI、本机双开、双方兼容客户端的局域网两人单局撤回。朋友局逐次确认；首版不含 Match、Tag 和观战。当前为开发测试版，第二台电脑和人工视觉验收待完成。

最近修复了有计时对局的确认消息丢失、撤回同意/拒绝按钮及窗口缩放问题，见 [修复与回归记录](tests/timer-consent-ui.md)。创建房间时的网络错误仍在排查，当前已知问题见 [兼容与验收状态](docs/COMPATIBILITY.md)。

当前源码还包含普通联机排除无关练习脚本的修正，以及随包的卸载工具；以上 alpha.1 下载包尚不包含这两项。卸载工具确认后先备份再移除可验证的 mod 文件，默认保留原游戏资源、配置和日志。

- [安装与卸载](docs/INSTALL.md)
- [构建与打包](docs/BUILD.md)
- [兼容及测试范围](docs/COMPATIBILITY.md)
- [许可与来源](docs/THIRD-PARTY.md)
- [规格与任务](openspec/changes/duel-undo-mod/tasks.md)
- [验收矩阵](tests/acceptance.csv)

## 获取源码

可直接检出当前开发分支：

```powershell
git clone --branch codex/duel-undo https://github.com/SuperDuckonhead/duel-undo-mod.git
cd duel-undo-mod
```

按 [构建与打包](docs/BUILD.md) 准备固定依赖并生成客户端与 WindBot。原资源不进入 Git；`.cache` 和 `out` 为可再生成的开发输出。alpha 测试包已在 [Releases](https://github.com/SuperDuckonhead/duel-undo-mod/releases) 提供，稳定版待完成验收后发布。
