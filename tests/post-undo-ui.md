# 撤回后界面闪退与对局 Ctrl+Z（2026-09-10）

用户反馈：已撤回后，再召唤/发动卡牌时闪退。系统记录为 18:27:15 的 APPCRASH、异常 0xc0000374。保留的该次本地转储显示原生鼠标悬停进入 ShowCardInfoInList，随后 SetStaticText 更新 stCardListTip 时触发堆损坏。相关函数的完整机器码区间与已安装程序一致，且转储里实际传入的控件指针与 Game::stCardListTip 一致。转储未包含该控件完整堆内存；首次释放由源码生命周期与运行回归共同确认，不声称仅靠转储证明。

## 修正

undo_prompt.cpp 的恢复绑定遗漏了 stCardListTip 和 btnOperation。撤回提交重建窗口后，旧窗口释放，但 Game 仍保留这两个旧控件地址。补入已有 slots 绑定即可让现有准备/提交过程更新它们，保持原菜单和鼠标分发。静态核对 41 个直接属于这些恢复窗口的 Game 控件字段，只发现这两项遗漏。

Ctrl+Z 在 event_handler.cpp 调用与“撤回选择”按钮相同的入口。只有进行中且可撤回的 Single/Room 对局触发；文本焦点沿用原处理，恢复、赛后、回放和系统确认状态不触发。普通卡牌 YES/NO 提示仍可撤回。使用原生 KeyInput.AutoRepeat 防止长按连续触发，避免自建按键状态遗漏失焦时的松键。原编辑器快捷键保持不变。

## 回归证据

- 两次修前真实 MainLoop 测试，实际鼠标召唤→撤回→再次打开手牌菜单。恢复后两个 Game 控件地址都不属于当前窗口树，断言退出 2：out/room-action-mainloop-{red,two-widgets-red}.log。测试以地址比较确认，不将未出现同一 Windows 异常码的断言失败说成同次系统崩溃。
- 相同输入修后通过，两个控件都归属于现窗口：out/room-action-mainloop-green.log。
- 扩展真实主循环：两次召唤、第一次按钮撤回、第二次 Ctrl+Z 撤回；恢复后的显示列表与选择列表均通过原生 Win32 鼠标悬停显示提示文字、点击关闭，并正常退出：out/crash-20260910/actions-final.log。列表准备调用原 GUI 函数；Ctrl+Z 使用带 Control 标志的规范化按键经 device 入口派发，不声称由物理键盘驱动。
- 快捷键先缺失 RED，再捕捉私有按键锁存导致漏松键后的新按键被忽略的 RED；最终使用原生重复标记通过 Room 请求/文本与暂停保护、普通 YES/NO、长按、漏松键后新按键、原按钮、真实 SingleMode 两次撤回和编辑器 Ctrl+Z：out/crash-20260910/shortcut-final.log。
- 实际 Room 准备失败保留、Abort 屏障、Commit/Resume、旧 token、256 字节响应回归：out/crash-20260910/room-final.log。
- 单机恢复与回放的既有集成回归：out/crash-20260910/single-final.log。

本轮最终 Release 增量构建日志为 out/crash-20260910/build-final.log；客户端 SHA-256 为 034d063f08a3142021524a91be49d7efa8290bc024d69460a382c1e0a7ce9b40。没有更改 WindBot 或资源。转储、旧对象/程序及地址校验记录仅在忽略的 out/crash-20260910 中，不进入源码提交或发行包。

## 范围

修正本次已定位的悬停闪退和同类菜单地址问题，并加入快捷键。不宣称所有卡组或全部交互零故障。此前标记的原生 Consent、部分阻塞确认、完整两设备 LAN 与最终发行验收仍未关闭；本轮是可追溯的本地开发更新，不改写历史候选包的干净构建证明。
