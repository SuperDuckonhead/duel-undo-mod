# 联机撤回与双方界面同步 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 支持本机双开自由撤回和朋友 LAN 逐次同意，并确保双方在同一版本提交后才能继续。

**Architecture:** 宿主维护唯一撤回事务，使用会话、历史版本和请求标识过滤所有迟到输入。候选内核及各玩家可见界面先准备，再统一提交与确认；无法确认提交结果时保持暂停。

**Tech Stack:** C++17、固定基线 TCP/Libevent 协议、Irrlicht、纯状态机测试、双客户端与两设备 LAN 故障测试。

**Spec:** [联机设计第 5 节](../../../openspec/changes/duel-undo-mod/design.md)、[联机行为规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)。

## Global Constraints

- N1 可在 Gate-A 后实施；N2 的真实恢复接入依赖 Gate-B 与 core C4；N3/N4 依赖 N1/N2。
- 源码路径相对于 F:/MyCardLibrary/ygopro/dev/duel-undo-mod。
- 朋友局每一次撤回均需对方明确同意；超时固定 30 秒，拒绝/超时恢复冻结前计时。
- 本机自由撤回是显式房间设置，监听回环接口并拒绝非回环连接，不能依靠昵称或客户端自报同机。
- 原协议客户端不加入撤回房间；兼容检查包含协议、内核/规则和逻辑资源，不包含卡图。
- 所有出牌响应和恢复消息均受 session/epoch 校验；重复请求幂等。普通游戏消息按当前 epoch 校验；控制消息按活动 TxKey 的基准 epoch 校验，CommitAck 的已安装 epoch 放 payload。Resume 仍用该事务 TxKey，不被普通游戏消息过滤器误丢弃。
- 原会话提交前保留；双方 Ready、CommitAck 完成后才 Resume；结果不明时暂停。
- 不发送宿主全量录像、对手隐藏身份或宿主全状态哈希；首版不承诺断线重连恢复。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| client/gframe/undo/protocol.h、.cpp | 编解码、长度校验、握手、输入版本标记。 |
| client/gframe/undo/coordinator.h、.cpp | 单事务、同意、超时、准备/提交/确认状态机。 |
| client/gframe/undo/player_restore.h、.cpp | 每个玩家的候选恢复流与完整性检查。 |
| client/gframe/network.h、single_duel.h/.cpp、duelclient.cpp | 房间能力协商、所有输入/输出路径接入。 |
| client/gframe/game.h/.cpp、client_field.h/.cpp | 候选 UI 模型、撤回弹窗/状态；B2 确认字段实际位置。 |
| tests/protocol_tests.cpp、coordinator_tests.cpp、player_restore_tests.cpp | 协议/事务/可见性测试。 |
| tests/lan-faults.md、tests/network_manual.md | 双开、两设备 LAN 与故障注入步骤。 |

### Task N1: 能力握手、消息封装与旧输入过滤

**Files:** 新建 protocol.h/.cpp、tests/protocol_tests.cpp；修改 network.h、single_duel.cpp、duelclient.cpp。

**Interfaces:** 复用 core 的 Bytes、Digest。namespace undo：

~~~cpp
using SessionId = std::array<std::uint8_t,16>;
struct TxKey {
    SessionId session;
    std::uint64_t epoch, request, targetIndex;
    Digest targetDigest;
};
enum class WireKind : std::uint8_t {
    Hello=1, Response=2, Request=3, Consent=4, Prepare=5,
    Ready=6, Commit=7, CommitAck=8, Resume=9, Abort=10, AbortAck=11
};
struct Envelope { WireKind kind; TxKey key; Bytes payload; };
Bytes Encode(const Envelope&);
Envelope Decode(const Bytes&);
bool SameKey(const TxKey&, const TxKey&);
bool IsCurrent(const TxKey&, const SessionId&, std::uint64_t epoch);
~~~

外层协议编号在固定 network.h 全量检索后选择无冲突值，记录在 docs/protocol.md；WireKind 是新封装内部枚举。线格式固定小端、逐字段编码：protocolVersion u16=1、kind u8、session[16]、epoch u64、request u64、targetIndex u64、targetDigest[32]、payloadLength u32、payload；头长 79 字节，禁止直接 memcpy C++ struct。单帧 payload <= 48 KiB，大恢复流分片并校验序号/总数；超限显式拒绝，不能截断历史。

targetDigest 只摘要公开目标描述与事务标识，不使用 core 的 canonicalState 或隐藏身份。双方恢复流各自有可见内容摘要，与目标摘要分开。

- [ ] **Step 1：写往返、截断和旧 epoch 用例。**

~~~cpp
undo::TxKey key{{},7,31,4,{}};
undo::Envelope original{undo::WireKind::Request,key,{1,2,3}};
auto bytes = undo::Encode(original);
auto decoded = undo::Decode(bytes);
CHECK(undo::SameKey(decoded.key,key));
CHECK(decoded.payload == original.payload);
CHECK(undo::IsCurrent(key,key.session,7));
CHECK(!undo::IsCurrent(key,key.session,8));
bytes.pop_back();
bool rejected = false;
try { undo::Decode(bytes); } catch (const std::exception&) { rejected = true; }
CHECK(rejected);
~~~

- [ ] **Step 2：注册运行 protocol_tests，预期编解码尚不存在。**
- [ ] **Step 3：实现显式长度/版本校验及当前版本判断。**

~~~cpp
bool IsCurrent(const TxKey& key, const SessionId& session, std::uint64_t epoch) {
    return key.session == session && key.epoch == epoch;
}
~~~

Decode 在读每个字段前检查剩余长度，payloadLength 与剩余字节完全相等，未知版本/kind、重复/遗漏分片、超长 payload 均失败。Encode 与 Decode 使用相同固定小端字段表；为每个整数边界和空 payload 增加 roundtrip 用例。SessionId 用系统安全随机数生成；request 在会话内单调递增，结束换新的 session。
- [ ] **Step 4：接入握手和整场对局的版本门控。** Hello 交换支持版本、引擎/规则指纹、资源摘要、模式；不兼容显示具体项并阻止开局。扩展房间拒收未封装 CTOS_RESPONSE。所有影响对局的客户端输入和宿主游戏消息关联 session/epoch；恢复期间旧动画回调/旧 AI 包也经过同一门控。
- [ ] **Step 5：运行旧客户端、资源不一致、只有卡图不同、旧响应/旧同意/错误目标请求测试。** 只有逻辑兼容且双方支持时能开局，卡图不同仍可开局。
- [ ] **Step 6：提交。**

~~~powershell
git add client/gframe/undo/protocol.h client/gframe/undo/protocol.cpp client/gframe/network.h client/gframe/single_duel.cpp client/gframe/duelclient.cpp tests/protocol_tests.cpp docs/protocol.md
git commit -m "feat: negotiate undo protocol and reject stale duel input"
~~~

### Task N2: 单事务、逐次同意与安全提交

**Files:** 新建 coordinator.h/.cpp、tests/coordinator_tests.cpp；修改 single_duel.h/.cpp、game.h/.cpp、duelclient.cpp。

**Interfaces:** 纯状态机不直接发包/调用内核，输出事件由宿主适配执行；participant 0/1 是两个玩家，宿主本地 Ready 代表内核与本地显示模型都已准备好，AI 作为其席位参与。

~~~cpp
enum class TxState { Running, WaitBoundary, Consent, Preparing, Committing, Aborting, PausedFailed };
class Coordinator {
public:
    Coordinator(SessionId session, std::uint64_t epoch, bool consentRequired);
    bool Request(TxKey key, std::uint8_t requester, std::int64_t nowMs, bool atBoundary);
    void Boundary(std::int64_t nowMs, bool finished);
    bool Consent(TxKey key, std::uint8_t voter, bool approve, std::int64_t nowMs);
    void Tick(std::int64_t nowMs);
    void Ready(TxKey key, std::uint8_t participant);
    void CommitAck(TxKey key, std::uint8_t participant, std::uint64_t installedEpoch);
    void Fail(TxKey key, bool commitMayHaveEscaped);
    void AbortAck(TxKey key, std::uint8_t participant);
    TxState State() const;
    std::uint64_t Epoch() const;
};
~~~

事件通过 std::vector<Envelope> Coordinator::TakeOutgoing() 取出并清空；真实状态变化只在宿主决斗线程执行。客户端 Request 只申请请求人的最近目标；宿主在安全边界按 C1 重算并固定权威 target，校验请求人席位，不能相信客户端任意指定的历史索引。等待边界期间先按 session/epoch/request 跟踪；权威目标固定后后续同意与恢复才使用完整 TxKey。等待期间对局结束则取消。冻结时保存 ClockState，失败/拒绝用冻结前值，成功用目标 Checkpoint.clock。

- [ ] **Step 1：写逐次同意、互斥和确认不齐的失败测试。**

~~~cpp
undo::TxKey key{{},0,1,0,{}};
undo::Coordinator c(key.session,0,true);
CHECK(c.Request(key,0,1000,true));
CHECK(c.State() == undo::TxState::Consent);
auto competing = key; competing.request = 2;
CHECK(!c.Request(competing,1,1001,true));
CHECK(!c.Consent(key,0,true,1002));
CHECK(c.Consent(key,1,true,1003));
c.Ready(key,0); c.Ready(key,1);
CHECK(c.State() == undo::TxState::Committing);
c.CommitAck(key,0,1);
CHECK(c.State() != undo::TxState::Running);
c.CommitAck(key,1,1);
CHECK(c.State() == undo::TxState::Running);
CHECK(c.Epoch() == 1);
~~~

- [ ] **Step 2：运行 coordinator_tests，预期缺少状态机或单个 Ack 错误放行。**
- [ ] **Step 3：实现允许转换表。**

| 当前状态 | 输入/条件 | 后续行为 |
| --- | --- | --- |
| Running | 当前会话有效 Request | WaitBoundary 或 Consent/Preparing；记录冻结前计时。 |
| WaitBoundary | 安全边界/结束 | 固定目标后进入 Consent/Preparing；结束则取消。 |
| Consent | 对方同意且未过期 | Preparing。 |
| Consent | 对方拒绝或 nowMs >= start+30000 | 原提示与计时恢复；请求失效。 |
| Preparing | 两席 Ready | 先准备无失败的切换操作，再发 Commit，进入 Committing。 |
| Preparing | 准备失败 | Aborting；确认两端恢复旧态后 Running。 |
| Committing | 两席确认 epoch+1 | 更新 epoch，发 Resume，Running。 |
| Committing | 断线/结果不明 | PausedFailed；不重开新事务。 |
| Aborting | 两席 AbortAck | Running；仍旧 epoch。 |

状态机核心提交确认必须满足完整 key、席位和版本：

~~~cpp
if (!SameKey(key, activeKey_) || state_ != TxState::Committing ||
    participant > 1 || installedEpoch != epoch_ + 1) return;
ackMask_ |= static_cast<std::uint8_t>(1u << participant);
if (ackMask_ == 3) {
    ++epoch_;
    state_ = TxState::Running;
    outgoing_.push_back(Envelope{WireKind::Resume, activeKey_, {}});
}
~~~

成员 activeKey_、state_、epoch_、ackMask_、outgoing_ 在 coordinator.h 定义；Ready 使用独立 readyMask_，不可复用 Ack 位。TakeOutgoing 按双方路由发送 Resume。已完成请求保留结果缓存，重复 Commit/Request 重发同一结果而不再次截断历史。
- [ ] **Step 4：接入每次请求弹窗与 30 秒单调时钟。** 内容只显示公开玩家、操作序号/提示类别；按钮“同意撤回/拒绝”。同意必须绑定当前 key 和真实对手连接；收到请求后先冻结输入/计时，不能继续接受新响应再批准旧目标。
- [ ] **Step 5：实现本机房间显式开关。** 自由撤回房间绑定 127.0.0.1（使用 IPv6 时只绑定 ::1），检查 peer 地址；普通 LAN 一律逐次同意。本机模式与 AI 可跳过 Consent，但仍需准备和 CommitAck。
- [ ] **Step 6：补齐 Tick(31000) 的 30 秒超时、迟到同意、重复确认、两方同时请求、连锁结束后再处理及处理前胜负已定用例。** 拒绝/超时不扣冻结时间；提交不明时任何输入均被宿主拒绝。
- [ ] **Step 7：提交。**

~~~powershell
git add client/gframe/undo/coordinator.h client/gframe/undo/coordinator.cpp client/gframe/single_duel.h client/gframe/single_duel.cpp client/gframe/game.h client/gframe/game.cpp client/gframe/duelclient.cpp tests/coordinator_tests.cpp
git commit -m "feat: coordinate consent and atomic undo epochs"
~~~

### Task N3: 各玩家可见恢复流与候选 UI

**Files:** 新建 player_restore.h/.cpp、tests/player_restore_tests.cpp；修改 duelclient.cpp、client_field.h/.cpp。

**Interfaces:**
- struct PlayerRestore { std::uint8_t player; std::vector<Bytes> frames; Bytes prompt; Digest visibleDigest; };
- PlayerRestore BuildPlayerRestore(std::uint8_t player,const std::vector<Bytes>& playerVisibleHistory,const Bytes& targetPrompt)。
- ClientRestore::Prepare(const TxKey&,const PlayerRestore&) -> bool；Commit(const TxKey&,std::uint64_t epoch) -> bool；Abort(const TxKey&) -> void。
- 恢复流来源必须是同一玩家在保留历史中的过滤后事件，不接受宿主原始完整日志作为参数；Prepare 在独立 ClientField 模型中构建。

- [ ] **Step 1：建立真实可见性 fixture。** 对手未知手牌/盖卡放入可辨识测试 ID，记录宿主完整状态和玩家实际原始网络字节。恢复流必须与目标点该玩家视角等价，不能以“UI 没显示”代替网络不泄露。
- [ ] **Step 2：运行 player_restore_tests，预期当前无完整候选 UI 或错误使用全量录像。**
- [ ] **Step 3：从经过原宿主逐玩家过滤的起始事件与保留前缀构建候选。**

~~~cpp
PlayerRestore BuildPlayerRestore(
    std::uint8_t player, const std::vector<Bytes>& playerVisibleHistory,
    const Bytes& targetPrompt) {
    PlayerRestore result;
    result.player = player;
    result.frames = playerVisibleHistory;
    result.prompt = targetPrompt;
    result.visibleDigest = HashVisibleRestore(result);
    return result;
}
~~~

Digest HashVisibleRestore(const PlayerRestore&) 在 player_restore.cpp 定义：SHA-256 按 player、每帧长度/字节、prompt 长度/字节顺序计算，复用 C2 的 SHA-256 实现。原过滤器必须正确区分双方视角；重建模式抑制网络/音效，只把过滤后的帧分别送入收集器。
- [ ] **Step 4：实现 ClientRestore 的候选显示模型。** 帧全部收齐、顺序/摘要/目标提示匹配才 Ready；缺片/重复/错误玩家视角拒绝。恢复手牌、牌库数量、墓地/除外、覆盖物、连锁、阶段与等待提示，清除旧选择窗口、动画任务、悬停和缓冲输入；等待 Commit 时不替换可操作 live 模型。
- [ ] **Step 5：Commit 只交换已构建模型并 Ack，Resume 之后解冻。** Abort 丢弃候选；提交前 live 仍保留。对实际网络记录逐条解码断言不存在目标点未公开的对手身份；场面公开过的历史信息按规格恢复，不试图消除玩家记忆。
- [ ] **Step 6：提交。**

~~~powershell
git add client/gframe/undo/player_restore.h client/gframe/undo/player_restore.cpp client/gframe/duelclient.cpp client/gframe/client_field.h client/gframe/client_field.cpp tests/player_restore_tests.cpp
git commit -m "feat: prepare player-filtered client restoration views"
~~~

### Task N4: 故障注入与真实双端验收

**Files:** 新建 tests/lan-faults.md、tests/network_manual.md；扩展 protocol_tests/coordinator_tests/player_restore_tests。

**Interfaces:** 测试传输层提供 enum FaultPoint { DropReady, DropCommit, DropCommitAck, DropAbortAck, DuplicateRequest, DelayResponse }；仅测试构建启用。故障点在发送队列入口按 TxKey 精确匹配，不修改生产网络规则。

- [ ] **Step 1：建立提交不明的直接测试。**

~~~cpp
undo::TxKey key{{},3,12,2,{}};
undo::Coordinator c(key.session,3,false);
CHECK(c.Request(key,0,0,true));
c.Ready(key,0); c.Ready(key,1);
c.CommitAck(key,0,4);
c.Fail(key,true);
CHECK(c.State() == undo::TxState::PausedFailed);
CHECK(!c.Request(key,1,5000,true));
CHECK(c.Epoch() == 3);
~~~

Epoch=3 代表协调器尚未确认全体提交，部分参与者可能已安装 4；不能由此推断可回到旧局继续。
- [ ] **Step 2：运行用例，确认没有“一个客户端成功即可继续”的路径。**
- [ ] **Step 3：用状态条件实现测试故障钩子并逐项执行。** 准备错误可安全 Abort；丢 Commit/CommitAck 后暂停并幂等重传；丢 AbortAck 不恢复到可出牌状态；旧响应与旧批准不跨 epoch 生效。
- [ ] **Step 4：本机双开完成双方各发起两次撤回及新分支，普通 LAN 模式每次都弹同意框；显式本机模式免同意且远程连接失败。**
- [ ] **Step 5：两台设备运行朋友 LAN。** 验证双方准备/确认、拒绝/超时、各自视角、连锁中请求、断线暂停和正常继续。当前没有第二设备时自动测试可先通过，但“两设备验收”保留未完成，不以本地双开替代。
- [ ] **Step 6：提交故障测试与验收记录。**

~~~powershell
git add tests/protocol_tests.cpp tests/coordinator_tests.cpp tests/player_restore_tests.cpp tests/lan-faults.md tests/network_manual.md
git commit -m "test: verify undo transaction failures across peers"
~~~

## 完成条件

双开和真实两设备 LAN 均按规格通过；所有准备/提交/确认失败均保留原局或统一暂停，无旧输入污染、额外隐藏信息泄露或一端自称成功继续。
