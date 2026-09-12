# Match 撤回验证（2026-09-12）

基线源码 `f4bf0efc9ef5cd34261a2e362ead2558ed0e1d0a`，安装前版本 `2187cb36ab7dcac7511a578470612b58cda277a6`。本次仅开放人工两人 Match；AI 仍从原 Single 入口启动，Tag/观战未开放。最终包的准确源码及文件哈希以 `undo-mod/build-manifest.json` 和对应外部文件清单为准。

## 行为与边界

- 复用原版 Match 比分、换备合法性、败方下一局先后手与比赛结束规则。
- 撤回只作用于当前未结束小局，胜负结算后清空历史，不支持跨局或换备撤回。
- 原房间会话保持不变，撤回和换局都递增 epoch。每局有新的核心、随机种子、初始卡组、提示、界面快照及计时状态。
- 新局状态在有效原生换备/先后手流程完成后启用。旧选择、自动计时确认、同意/拒绝及恢复模型不可应用到新局。
- 已提交但未收到 Resume 的不确定状态不能凭 WIN 和下一局控制重新开放。换局失败按两端可能处于的旧/新 epoch 独立发送明确失败状态。
- 握手仍为 99 字节 v2，恢复格式升级为 2；此前 Single 专用包会在房间握手时被拒绝。

## 测试组成

| 层级 | 场景 | 工具/记录 |
|---|---|---|
| 协议 | RoundStart 编解码、身份/长度/小局号/epoch 溢出拒绝；旧恢复格式拒绝 | `protocol_tests`、`room_wire_tests`、`resource_scope_tests` |
| 实际宿主 | 2:0/2:1、合法/非法换备、席位变化、新初始卡组/计时、第二局撤回拒绝/失败取消/成功/继续、旧输入隔离 | `match_duel_series`、`match_duel_epochs` |
| 结算 | 实际核心卡组耗尽、原生计时超时、投降及逐局录像 | `match_duel_terminal` 与 series |
| 平局 | 向原生 Analyze 注入 MSG_WIN 平局消息，验证三平局及原生先后手规则；不声称实际卡片效果产生平局 | `match_duel_draws` |
| 失败 | 明确拒绝人工构造的 AI Match；仅一端收到换局通知后的失败通知，无法继续旧局/新局 | `match_duel_bot-match`、`match_duel_round-failure` |
| 客户端 | 实际 RoomClient 和原生处理器的换备纠正、2/3 局、模型/输入隔离、乱序状态/游戏帧/计时帧、不确定提交及两种 epoch 的明确失败 | `Test-RoomClientIntegration.ps1 -MatchClient` |
| 双客户端 | 两个独立 Game::MainLoop 与真实 TCP：同意房 2:0/2:1、本机自由房 2:0；第二局撤回并新召唤；双方逐局录像哈希相同、宿主重放最终响应分支 | `Test-RoomClientIntegration.ps1 -MatchPair`，可加 `-MatchThree` 或 `-FreePair` |

双客户端 Match 测试保留生产 `frameSignal/actionSignal/replaySignal/closeSignal` 等待，通过各自窗口的 Win32 消息操作猜拳、先后手、录像保存、换备确认和撤回弹窗。房间准备和部分核心响应由测试驱动发送，并非所有操作均为人工鼠标验收。独立 RoomClient 模型测试允许跳过信号等待；不能拿它替代上述主循环测试。

## 回归与交付

修改前默认测试 23/23 通过。最终源码的 CMake 回归 46/46 通过（包含 6 组 Match），固定资源核心重建 3/3 通过。固定的三个核心重建场景使用原安装的只读资源；其摘要与普通 c4 界面测试资源不同，不通过修改 golden 或弱化校验绕过。

最终共享客户端编译成功，仅依赖 Windows 系统动态库。原生 Room UI 的 confirm/fade/resize/clock/early-status 五场景、两次召唤/撤回及悬停的实际 MainLoop、Ctrl+Z 与编辑器隔离、单人实际 Game/Replay、录像编辑文件名保存与取消的实际 MainLoop 均通过。

独立审查指出的提前 Status/Game/TimeLimit、部分换局送达无失败提示，以及协调者补充的未 Resume 提交可被终局重新开放问题，均有先失败后修复通过的回归；最终只读复查无剩余阻断项。日志保留在忽略的 `out/match-validation/`、`out/room-match-client-tests/` 和 `out/match-host-build/`；这些不是安装所需文件。

最终同意 Match 2:0、2:1 和本机自由 Match 2:0 双端均正常退出；7 局共 14 份录像按局逐一比对，双方 SHA-256 全部相同。宿主实际重放各局最终响应分支，确认第二局换备生效且撤回后新的召唤保留。三组记录分别为 `room-pair-7bdef0b8a26345388c606e4a46b6b989`、`room-pair-f4a305b58f60489cbd17a14d1c62b70a`、`room-pair-82f3a738bb0943b2a8afff504b4cfa6b`。

Single 计时双开、自由双开、真实 AI 菜单/私有 bot、默认 RoomClient 分配失败/恢复与 256 字节响应，以及计时先于选择提示的回归均通过；日志为 `out/match-validation/final-*.log`。Match 专用双进程主用例设置无倒计时以精确验证撤回事务保存的时钟值；带 180 秒倒计时的额外完整 Match 也通过，和安装包结果一起记录在 [交付记录](match-delivery.md)。

2026-09-12，两台物理电脑的客户端文件经只读检查确认相同；入房失败来自完整逻辑卡表差异（15,049 对 15,077 张，第二台多 28 张，共有卡片数值相同）。统一卡表后，用户确认双方可以联机，详情见 [交付记录](match-delivery.md)。

这次确认不关闭跨电脑逐项验收：撤回同意/拒绝/超时、连续撤回与新路线、Match 局间换备/比分/计时及两边视觉显示仍待实测，本机自动双进程测试也不能替代这些项目。首次 AI 启动等待、此前独立记录的建房问题不在本次改动范围内。当前完整逻辑卡表等值限制仍在；后续简化方案及卡组测试功能见 [待更新功能](../docs/ROADMAP.md)。
