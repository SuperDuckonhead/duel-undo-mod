# 猜拳开局原生输入回归（2026-09-10）

用户复现：单人对战猜拳按钮出现后，整个窗口无法操作。正常主循环的红测显示帧率和计时器仍在运行，但新增的 Room 输入门在 prompt=0 时吞掉原始鼠标事件；GUI 按钮无法生成点击通知。此前直接调用按钮 GUI 事件且关闭帧等待的测试没有覆盖这一入口。

## 最小修复

只在 client/gframe/event_handler.cpp 中区分正常开局：prompt=0 且 PresentationFrozen=false 时，沿用原 OnCommonEvent 和 Irrlicht 鼠标→GUI 分发。没有增加独立鼠标处理器，未更改菜单、RoomClient::InputPaused、核心响应校验、记录/恢复/同步或 SingleMode。

这不是完整交互重构。Consent 和实战 MSG_RETRY/HINT_MESSAGE/CONFIRM_CARDS 的确认、终局交互仍需单独闭合；后续按 docs/INTEGRATION-STAGES.md 逐阶段推进。

## 验证

构建：tools/Build-Client.ps1 -Configuration Release，退出 0；日志 out/rps-opening-build.log。这是既有开发工作区的增量构建，不是新的干净发行构建。

客户端 SHA-256：f99462cd6e7445262e8183cf95d0d061e481bfff8672ced92396fecbdb66f52e。
生产源文件 SHA-256：270624d105790c06fcff3784d19c360b94a5f7673e8cac775f0f25fca017406d。

tools/Test-RoomMainLoop.ps1 用独立隐藏窗口、真实 Game::MainLoop、正常帧/动作/退出信号、实际 ChainBurn 和本机宿主/客户端。猜拳和先后手由发给本测试窗口的 Win32 鼠标消息驱动；菜单准备、卡组、Ready/Start 仍用程序调用，因此不宣称整段菜单均经实际鼠标操作。每次链接保留对象哈希并检查对象在链接期间没有变化。

| 场景 | 结果与证据 |
| --- | --- |
| 旧门禁 RED | 点击后 6 秒仍停留猜拳，帧率正常；进程 166608 退出 2。out/room-mainloop-red.log |
| first | 真实鼠标约 109 ms 响应；正常猜拳动画与先手选择，进入人类回合 1、prompt 1，退出 0。out/room-mainloop-first-green.log |
| tie | 真实平局动画→再次猜拳→先手选择→人类回合 1，退出 0。out/room-mainloop-tie-green.log |
| second | 点击后手，经过 AI 首回合进入人类回合 2、prompt 5，退出 0。out/room-mainloop-second-green.log |
| unchecked | 不锁定 AI 猜拳，正常进入人类回合 1，退出 0。本次随机结果为人类获胜，不据此推断全部随机分支。out/room-mainloop-unchecked-green.log |
| 恢复保护回归 | 实际 Game 的准备失败保留、Abort 屏障、Commit/Resume、旧 token 和完整 256 字节传输通过，退出 0。out/rps-opening-room-regression.log；这一项沿用原集成驱动，不代替真实鼠标测试。 |

四次 MainLoop 运行均观察到真实猜拳动画的 signalFrame>0，等待正常倒计时结束及首个人工 idle 提示，然后主循环正常返回。没有通过 SetNoWait 跳过等待。

## 本地修复与发行边界

原候选 1cd153e 的 ZIP、源码包和安装收据作为历史证据保留。本轮只更新本机开发修复版，不将旧候选的构建清单改写成新程序的发行证明。安装前保留旧 EXE、配置哈希及来源记录；本地替换记录存放在 out/local-fixes。正常发行和两设备验收仍未完成。

补充精确已安装 WindBot 的复核：使用 -BotDirectory F:/MyCardLibrary/ygopro/WindBot，first 与 unchecked 均退出 0（out/room-mainloop-installed-first-green.log、out/room-mainloop-installed-unchecked-green.log）。Bot SHA-256 为 6582a35a5a270c93854449ea135892f055ba731cbb370a2e26d80334e720ad61，与原安装相同。unchecked 这次实际经历两次平局后第三次分出胜负，正常进入首个人工回合。各自 GUID 证据目录中的 binding.json 记录测试与驱动源、相关生产源、测试 EXE、客户端 EXE 参考值、实际 Bot 及对象哈希；客户端 EXE 参考哈希不是声称测试进程就是发布 EXE。
