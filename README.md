# YGOPro 撤回版

独立启动的 Windows 修改版客户端和配套 WindBot，直接使用原 YGOPro 目录资源。

支持卡组编辑撤回，以及单人练习、AI、本机双开、双方兼容客户端的局域网两人单局撤回。朋友局逐次确认；首版不含 Match、Tag 和观战。当前为开发测试版，第二台电脑和人工视觉验收待完成。

最近修复了有计时对局的确认消息丢失、撤回同意/拒绝按钮及窗口缩放问题，见 [修复与回归记录](tests/timer-consent-ui.md)。创建房间时的网络错误仍在排查，当前已知问题见 [兼容与验收状态](docs/COMPATIBILITY.md)。

- [安装与卸载](docs/INSTALL.md)
- [构建与打包](docs/BUILD.md)
- [兼容及测试范围](docs/COMPATIBILITY.md)
- [许可与来源](docs/THIRD-PARTY.md)
- [规格与任务](openspec/changes/duel-undo-mod/tasks.md)
- [验收矩阵](tests/acceptance.csv)

## 获取源码

获得私有仓库访问权限后，可检出当前开发分支：

```powershell
git clone --branch codex/duel-undo https://github.com/SuperDuckonhead/duel-undo-mod.git
cd duel-undo-mod
```

按 [构建与打包](docs/BUILD.md) 准备固定依赖并生成客户端与 WindBot。原资源不进入 Git；`.cache` 和 `out` 为可再生成的开发输出。可直接运行的程序包通过候选打包流程生成，正式 Release 在完成验收后另行发布。
