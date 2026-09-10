# 终局录像保存界面输入修复（2026-09-10）

用户截图：对局结束后，“保存录像”的文件名、保存和取消控件无法操作，默认名称显示 1970-01-01。

## 根因与修正

RoomClient 收到 MSG_WIN 或 STOC_DUEL_END 后，保留 InputPaused=true 来禁止继续提交对局操作，同时将 PresentationFrozen=false 让终局界面继续显示。原事件入口只在开局允许原生输入；终局保留了非零 prompt，因此在 Irrlicht 生成按钮事件之前，鼠标和键盘消息已被吞掉。只放行合成的保存/取消按钮事件无法解决实际点击。

复用 RoomClient 现有的 terminal 状态，新增受状态互斥量保护的 DuelEnded() 查询，让非冻结的终局沿用原生事件处理链。终局仍禁止出牌和撤回，恢复事务的冻结与提交校验保持原样。保存后的终局确认也走同一原生路径。

撤回录像格式要求 start_time 保持为零；默认文件名改用当前保存时间，与 SingleMode 一致。未修改录像头、校验规则或录像列表的元数据日期显示。

## 修复前证据

- save：out/replay-save-mainloop-3ed7e3ef332b402caf41cde4d1f5ca6b，真实鼠标点击后保存窗仍打开，退出 2。
- cancel：out/replay-save-mainloop-45113b5a5a444e37b4dee4fd59a77418，真实鼠标点击后取消窗仍打开，退出 2。
- 同一旧版测试程序的 edit-save 与 auto-save 分支分别无法聚焦文件名和无法关闭最后确认窗，证据保留在上述目录的对应子目录。
- 终局实测 finished=1、paused=1、frozen=0、prompt=1；默认文件名为 1970-01-01 08-00-00。

## 验证方法与范围

新增 tools/Test-ReplaySaveMainLoop.ps1：使用实际 Game::MainLoop、原始帧/动作/录像/关闭信号、已安装 WindBot 的私人本地房间。猜拳、先后手、认输、保存、取消、文件名焦点及键盘 Home/Delete/数字输入、最后确认均由测试进程自己窗口的 Win32 消息驱动；不关闭等待，不直接调用保存/取消处理函数。只有初始菜单准备、卡组和开局设置为程序设置。

四个分支为 save、cancel、edit-save、auto-save；检查有效录像重新打开、房主取消不写录像、当前日期默认名及正常返回 AI 菜单。所有写入都在私有测试目录，资源目录仅复用，未操作用户正在运行的对局。

本次不关闭尚未验证的对局中 MSG_RETRY、HINT_MESSAGE、CONFIRM_CARDS、Consent 和两设备 LAN 验收。

## 当前产物

Release 增量构建：out/replay-save-20260910/build.log，既有 game.cpp 格式字符串编译警告仍存在。本次未修改该处。
客户端 SHA-256：0a06b483d98a5e847a2e205e1275cd5d49c9a4329282774e36bcb4c91cd2384d。

四条原生流程回归 save.log、cancel.log、edit-save.log、auto-save.log 均退出 0；证据位于 out/replay-save-20260910。保存分支可重新打开合法的撤回录像，取消不写文件，自动保存无需保存弹窗且最终确认可操作，四条路径均正常返回菜单。shortcut.log 和 room.log 也退出 0，分别覆盖现有 Ctrl+Z / Single / 编辑器行为和恢复事务、旧响应、终局提交保护。独立复核未发现阻塞问题。

这是单独记录的本地开发更新；未重写此前候选包或其构建清单。安装需在旧客户端关闭后执行，并保留前一版回退副本。