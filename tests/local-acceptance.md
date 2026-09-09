# 本地集成验收记录

状态：已实现的本地流程与回归证据汇总；第二台电脑局域网及人工视觉验收仍待执行。tests/acceptance.csv 的两设备及四模式完整场景保留 pending，不能由本页局部通过自动替换为 pass。

## 已通过的范围

| 范围 | 执行方式与结果 | 证据与边界 |
| --- | --- | --- |
| 卡组编辑 | 实际初始化 Game、OnEvent、第二进程，共享受控卡组文件。完整增删/拖动/排序/洗牌/清空/保存生命周期通过。 | tests/editor_manual.md；实现 ca5badb。人工视觉、真实窗口焦点切换另待验收。 |
| 单人练习 | 实际 Game 的 A→撤回→A→撤回→B，准备失败保留旧局；结束保存和实际 ReplayMode 正常/交换视角播放、倒退循环通过。 | C4 修复 94d059f、e60165d；tools/Test-SingleIntegration.ps1。最终编辑/第二进程与单人回归见 out/final-editor-integration.log、out/final-single-integration.log。 |
| 真实内核 | 固定种子、抽卡/洗牌/随机、代价/连锁/次数限制重放；篡改响应/资源失败；原局保留及后续继续通过。 | 5ca108d、6ddfb79；core/resource/rebuild CTest。真实非输入 WAIT 和冻结的虚拟卡名缺失语义新增回归分别见 8169ab6、2720629。 |
| TCP 宿主与双方可见恢复 | 实际 NetServer、两个 TCP 端点、真实 core/N3；双方视角的未知身份过滤、阶段性断线、忙碌请求、计时/超时、重复/旧消息、保留分支终局录像通过。 | b7f4974、1de53ca；out/undo-host-fix2-green.log 12/12；out/n2-host-independent-deadlines.log 5/5；out/undo-tcp-busy-green.log 3/3。两端均在本机。 |
| 两个真实客户端 | 同机两个初始化 Game 进程、真实 TCP，实际同意/拒绝按钮；拒绝后原提示继续、控件准备失败保留双方、两次批准提交/恢复、新路线普通召唤及对手可见场更新通过。 | tools/Test-RoomClientIntegration.ps1 -Pair；out/n2-client-lan-decline-pair.log，两个进程退出码 0。ConsentLan 流程通过；新增 -FreePair 经真实建房勾选及加入菜单、全程无同意弹窗，也完成失败保留/两次撤回/B分支。日志 out/n2-client-free-pair.log；原 ConsentLan 再回归也通过。两者均不能当作第二台设备测试。 |
| 完整 AI 菜单 | 实际菜单选 ChainBurn、Hand/不检查/不洗切、本机开关；真人与 AI 回合、撤回、B 普通召唤、自然终局、实际录像保存、B 分支 core 回放，回编辑页再编辑/撤回通过。 | tools/Test-RoomClientIntegration.ps1 -Ai；out/n2-client-release-ai.log，退出码 0；最终录像 SHA256 62BA5E1F5EA6F5B4B0C880FC2316D869174A333BAA8C1AF061B6F5C743DDD46D。这是一个实际 AI 菜单执行器。 |
| AI 私有参与者与故障 | 实际 ChainBurn/Dragun 进程、受控回调/随机、完整候选重建、旧/新 PID 保留、真实候选就绪、准备拒绝、丢失确认与控制管道终止统一暂停通过。 | 1de53ca；tools/Test-UndoBotHost.ps1；out/n2-host-independent-green.log；原 AI 的迟到输出受最终 token/PID 核对。 |
| 实际 AI 全列表 | 62 行、61 执行器，含隐藏项和重复映射；真实双方回合→连续两次撤回→新路线普通召唤→AI 继续→合法投降，全部通过。 | b4b4565；tests/ai-history.md 和已跟踪 CSV/来源清单。首轮 58/62 失败保留，修复后完整重跑 62/62；不代表全部卡牌/自然终局/人工视觉穷举。 |
| 资源与配置隔离 | exe 目录寻址、其他 cwd/快捷方式、Unicode/长路径、新配置跨进程原子合并、原配置不写回、缺失资源、原版并存通过。 | 4931c51 及 R1 测试；最终候选包安装及原程序哈希还须单独核验。 |
| 打包工具 | 路径/覆盖/链接/哈希/条目白名单和 fresh-build 来源检查通过。 | 0c190c2 的 74 项及 dacd667 的 26 项测试；fixture 验证不是最终程序构建或安装结果。 |

## 最后并发边界

客户端已经复现并修正 Submit→Request 排序：待发送/已发送的当前人工响应禁用本端撤回，直至新提示或实际 MSG_RETRY。独立实际 Game 两项测试通过，日志 out/n2-client-response-gate-independent.log、out/n2-client-remote-consent-independent.log。对方协商先到时，客户端保留尚未发送的原 token 响应，final Abort 后只进行首次发送。

宿主补充修复 66e62ee：首个合法当前响应仅在完整 Abort 广播后重新走正常接受路径；Commit、断线、失败丢弃旧值。独立实际 core/N3 六场景 1/1、3.12 秒通过，out/pending-response-independent-green.log；实际 AI 故障回归 1/1、44.60 秒通过。统一原生构建与 default/RESPONSE_GATE/REMOTE_CONSENT/Pair/Ai 均通过，日志 out/n2-client-response-race-*.log。最终开发 EXE SHA256 0B04ADB0AD515E43AC76D4D02866955F2C376FB2593208780B93CBC5EE27AC31。

## 仍需完成

- 统一开发构建后的编辑器/第二进程与单人 Game/ReplayMode 四种宿主选项回归已通过，见 out/final-editor-integration.log、out/final-single-integration.log；干净候选产物须独立绑定。
- 完整 AI 宿主正式三档测量已完成，实际人工目标为 34/102/1002 条，各 20 次成功、20 次失败及另外 20 次清理恢复均通过。成功中位耗时约 480/485/1773 毫秒，各档析构后子进程归零。内存增长、回落、后段平台与未采集析构后 native privateBytes 的限制见 undo-host-performance.md；不据此关闭整体资源稳定性验收，初始失败证据仍保留。内核层独立结果见 performance.md。
- 候选 1cd153e 已完成空缓存 Bootstrap、fresh Release 构建、基础 21/21、扩展首轮 41/42 及唯一退出观察断言修正后的独立通过；实际单人/编辑/双端/AI/路径流程、精确 ZIP 进程和原目录安装/卸载/重装均完成，详见 candidate-release.md。原程序、原配置与共享资源未被覆盖。
- 两台 Windows 设备的局域网操作与两边人工视觉核对。按 docs/INSTALL.md 的两设备步骤记录结果；用户已确认有第二台电脑，可稍后配合。

本地日志位于开发目录 out，可能包含本机路径；可再执行的测试源码/脚本和公开结果摘要进入 Git。个人卡组、卡图、数据库、脚本库、原配置和实际对局录像不进入源码或增量包。
