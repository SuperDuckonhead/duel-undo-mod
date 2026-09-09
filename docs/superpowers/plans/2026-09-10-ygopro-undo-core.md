# 决斗历史与候选重建 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在固定初始条件下重建到发起者最后一次有效人工选择之前，验证后切换并继续单人对局。

**Architecture:** 宿主/单人模式共用有效响应历史；独立候选内核从初始条件重放全部保留响应。资源视图和输出通道按会话隔离，成功前保留原会话；单人入口复用正式重建服务。

**Tech Stack:** C++17、固定 YGOPro/ocgcore/Lua、SHA-256、CTest 与真实引擎测试程序。

**Spec:** [设计第 3–5 节](../../../openspec/changes/duel-undo-mod/design.md)、[决斗行为规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)。

## Global Constraints

- 依赖 Gate-A/B3。C1–C3 是技术验证及可复用基础，C4 须等待 C3 和 WindBot W1 共同通过 Gate-B。
- 源码根为 F:/MyCardLibrary/ygopro/dev/duel-undo-mod；不更改原脚本以逐卡反向撤销。
- 所有有效响应均记录；只有人工提交且引擎接受的选择建立该玩家的撤回点。无效尝试/MSG_RETRY 不进入用于确定性校验的有效消息摘要。
- 保留不检查/不洗切设置、实际初始牌序、种子、规则和单人场景参数；重建不再次调用开局洗牌。
- 原会话在候选校验与提交前保留；候选不能真实发包、写录像、聊天、播放动画/音效或改变原局计时。
- 仅当前未结束的双人对局；无历史、已结束、TAG、跨局和赛后恢复均不启用。
- 内核/资源状态的完整摘要仅供宿主内部校验，不发送给普通玩家。
- 测试片段是待实施的合同与算法锚点，本次未编译；C++ 测试均包含 B3 的 test_support.h。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| client/gframe/undo/duel_history.h、.cpp | 有效历史、目标与分支截断。 |
| client/gframe/undo/resource_view.h、.cpp | 固定逻辑资源及资源摘要；无持久化复制。 |
| client/gframe/undo/core_driver.h、.cpp | 唯一 ocgcore 调用适配、回调路由、规范化状态。 |
| client/gframe/undo/rebuilder.h、.cpp | 重放、比较、候选所有权。 |
| client/gframe/undo/single_undo.h、.cpp | 安全边界、时钟和单人切换。 |
| client/gframe/single_duel.cpp、single_mode.cpp | 响应记录、初始条件和内核驱动接入。 |
| client/gframe/replay.cpp、replay_mode.cpp | 当前分支录像写入/回放验证；B2 核实实际录像文件映射。 |
| client/gframe/game.h、game.cpp、duelclient.cpp | 单人撤回控件、来源标记与输入门控。 |
| tests/duel_history_tests.cpp、resource_view_tests.cpp、duel_rebuild_tests.cpp、single_undo_tests.cpp | 分层行为测试。 |
| tests/fixtures/duel/、tests/rebuild-cases.md、docs/rebuild-gate.md | 可公开 fixture、真实重建证据。 |

### Task C1: 有效响应历史与请求者目标

**Files:** 新建 duel_history.h/.cpp、tests/duel_history_tests.cpp；修改 single_duel.cpp、single_mode.cpp、duelclient.cpp、CMakeLists.txt。

**Interfaces:** namespace undo；头文件包含 array、vector、cstdint、optional、string。ClockState 使用宿主剩余毫秒，不用回放墙钟。

~~~cpp
using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t,32>;
enum class Origin : std::uint8_t { Manual, Automatic, Bot };
struct ClockState { std::array<std::int64_t,2> remainingMs; };
struct Checkpoint {
    std::uint8_t player;
    Bytes prompt;
    Bytes canonicalState;
    Digest transcriptDigest;
    ClockState clock;
    std::size_t aiLogCursor;
};
struct ResponseRecord {
    std::uint8_t player;
    Origin origin;
    Bytes response;
    Checkpoint before;
};
class DuelHistory {
public:
    void Accept(ResponseRecord record);
    std::optional<std::size_t> Target(std::uint8_t requester) const;
    void Truncate(std::size_t keep);
    const std::vector<ResponseRecord>& Records() const;
private:
    std::vector<ResponseRecord> records_;
};
~~~

输入 pending 记录由 CoreDriver 保存，只有确定非 MSG_RETRY、未被解析拒绝且引擎真正前进才调用 Accept。纯 UI 未提交选中项不创建 pending。来源不是客户端可随意绕过校验的授权标志。

- [ ] **Step 1：写交错操作与连续撤回用例。**

~~~cpp
undo::DuelHistory h;
undo::Checkpoint p{0,{10},{20},{},{{120000,120000}},0};
h.Accept({0,undo::Origin::Manual,{1},p});
h.Accept({1,undo::Origin::Manual,{2},p});
h.Accept({0,undo::Origin::Automatic,{3},p});
CHECK(h.Target(0).value() == 0);
CHECK(h.Target(1).value() == 1);
h.Truncate(h.Target(1).value());
CHECK(h.Records().size() == 1);
CHECK(!h.Target(1).has_value());
h.Truncate(h.Target(0).value());
CHECK(h.Records().empty());
~~~

- [ ] **Step 2：注册 duel_history_tests，构建并运行，预期未定义 DuelHistory。**

~~~powershell
cmake --build out/tests --config Debug
ctest --test-dir out/tests -C Debug -R '^duel_history_tests$' --output-on-failure
~~~

- [ ] **Step 3：实现目标和分支算法。**

~~~cpp
void DuelHistory::Accept(ResponseRecord record) { records_.push_back(std::move(record)); }
std::optional<std::size_t> DuelHistory::Target(std::uint8_t requester) const {
    for (std::size_t i = records_.size(); i > 0; --i)
        if (records_[i-1].player == requester && records_[i-1].origin == Origin::Manual)
            return i-1;
    return std::nullopt;
}
void DuelHistory::Truncate(std::size_t keep) {
    if (keep > records_.size()) throw std::out_of_range("history target");
    records_.resize(keep);
}
const std::vector<ResponseRecord>& DuelHistory::Records() const { return records_; }
~~~

.cpp 包含 utility、stdexcept。截断 k 表示保留 [0,k)，撤回的正是提交 k 之前。

- [ ] **Step 4：接入实际输入生命周期。** 人工确认按钮/点击标 Manual，自动过时点标 Automatic，AI 标 Bot；从 UI 到单人/宿主保留来源。在宿主当前等待玩家和提示匹配、引擎确认接受后才落记录。单独注入非法响应触发 MSG_RETRY，断言历史数不变；自动响应在 Records 中但不成为 Target。
- [ ] **Step 5：重跑历史测试，实际执行“自己选择→对手行动→自己撤回”，目标必须早于两者且新选择形成新分支；提交。**

~~~powershell
git add client/gframe/undo/duel_history.h client/gframe/undo/duel_history.cpp client/gframe/single_duel.cpp client/gframe/single_mode.cpp client/gframe/duelclient.cpp tests/duel_history_tests.cpp CMakeLists.txt
git commit -m "feat: record accepted choices and requester undo targets"
~~~

### Task C2: 固定资源、回调隔离和规范化状态

**Files:** 新建 resource_view.h/.cpp、core_driver.h/.cpp、tests/resource_view_tests.cpp；修改 data_manager.cpp 和固定内核回调接入点。

**Interfaces:**
- ResourceView::Capture(const std::string& runtimeRoot) -> std::shared_ptr<const ResourceView>。
- ResourceView::Read(const std::string& resolvedLogicalPath) const -> const Bytes&；缺失抛出带路径的异常。
- ResourceView::Fingerprint() const -> Digest。
- Digest Sha256(const Bytes&) 在 resource_view.h/.cpp 定义，供逻辑资源和 N3 可见流摘要共用；编码方负责稳定字段顺序。
- Capture 固定数据库内容和全部可执行脚本视图，记录扩展/包加载优先级与解析结果；不得只缓存已访问脚本后继续从变化磁盘读取新脚本。
- 保存原开局实际交给内核的卡片记录，不保存需重新随机化的中间牌组：

~~~cpp
struct InitialCard {
    std::uint32_t code;
    std::uint8_t owner, controller, location, sequence, position;
};
struct PlayerInit { std::int32_t lp, startCount, drawCount; };
struct InitialState {
    std::vector<std::uint32_t> seed;
    std::uint32_t duelOptions;
    bool noCheckDeck, noShuffleDeck;
    std::array<PlayerInit,2> players;
    std::vector<InitialCard> cards;
    std::string scenarioName;
    Bytes scenarioParameters;
    Digest resourceDigest;
};
enum class BoundaryKind { AwaitResponse, Finished, Failed };
struct Boundary {
    BoundaryKind kind;
    Checkpoint checkpoint;
    bool rejectedResponse;
    std::string failure;
};
class CoreDriver {
public:
    static std::unique_ptr<CoreDriver> Create(
        const InitialState&, std::shared_ptr<const ResourceView>);
    ~CoreDriver();
    Boundary Advance();
    Boundary Current() const;
    void Submit(const Bytes&);
    const InitialState& Initial() const;
    std::shared_ptr<const ResourceView> Resources() const;
};
~~~

CoreDriver 构造后未推进；Advance 推进至下一个待输入/结束/错误边界。Submit 只向当前等待提示提交。对象独占引擎句柄，析构只 end_duel 自己的句柄；禁止复制。

- [ ] **Step 1：写资源固定的失败测试。** 使用 tests/results/resource-root 中的测试脚本/数据库小 fixture，而非改写原安装脚本：

~~~cpp
const auto view = undo::ResourceView::Capture(fixtureRoot);
const auto original = view->Read("script/c900000001.lua");
WriteFixtureFile(fixtureRoot + "/script/c900000001.lua", {9,9,9});
CHECK(view->Read("script/c900000001.lua") == original);
CHECK(view->Fingerprint() != undo::ResourceView::Capture(fixtureRoot)->Fingerprint());
~~~

本任务定义测试辅助函数 void WriteFixtureFile(const std::string&, const undo::Bytes&)：用 std::ofstream(binary|trunc) 写入 bytes，写失败抛异常；fixtureRoot 来自测试命令参数，创建到 tests/results，禁止指向运行资源根。

- [ ] **Step 2：运行 resource_view_tests，预期当前读取仍来自变化的磁盘或缺少 ResourceView；明确记录失败原因。**
- [ ] **Step 3：实现固定读取及 SHA-256 清单。** Capture 通过原扩展解析器枚举最终逻辑名与原始字节，按逻辑名排序，把长度、名称、内容纳入哈希；相同卡图不同不改变 Fingerprint。Read 只查固定 map：

~~~cpp
const Bytes& ResourceView::Read(const std::string& name) const {
    const auto found = contents_.find(name);
    if (found == contents_.end()) throw std::runtime_error("Pinned resource missing: " + name);
    return found->second;
}
~~~

ResourceView 私有成员 std::map<std::string,Bytes> contents_ 和 Digest digest_；使用固定基线已有 SHA-256 实现，若无则用 Windows BCrypt SHA-256 并链接 bcrypt，不引入未经锁定的下载依赖。规范化数据库记录而非依赖 SQLite 页布局做兼容摘要；真实 SQL 内容视图固定在该会话。

- [ ] **Step 4：将 CoreDriver 映射到实际 API。** Create 使用匹配种子 API、set_player_info、new_card 和必要 preload_script，start_duel 使用保存的 duelOptions；Advance 通过 process/get_message 解析完整消息边界；Submit 对应 set_responseb 或该基线实际接口。初始牌序来自首次真实创建时记录，不再走 TPResult 的洗牌段。
- [ ] **Step 5：隔离读取和日志回调。** 所有内核调用在单一受控执行线程串行；带句柄回调用句柄表，无句柄回调在每次调用的 RAII 作用域绑定当前 CoreDriver。递归调用恢复上一绑定；全局可变缓存按 ResourceView 隔离。用 live/candidate 两个句柄交错 Advance，断言 live 状态和输出未变；候选失败后 live 接受下一输入。若仍有无法隔离全局状态，C2 失败，先修复/修订隔离设计，禁止提前销毁 live。
- [ ] **Step 6：实现规范化 Checkpoint。** 解析 query_field_info/query_field_card 与完整消息流，保存区域顺序、卡身份/位置/控制权、LP、阶段、连锁、覆盖物、计数物和选择提示；剔除内存地址，稳定映射实例 ID。无法查询的 Lua/次数状态靠相同资源+全部输入重放及后续行为用例验证，不能声称查询覆盖所有私有内核字段。
- [ ] **Step 7：资源变化、双句柄隔离和缺失资源用例通过后提交。**

~~~powershell
git add client/gframe/undo/resource_view.h client/gframe/undo/resource_view.cpp client/gframe/undo/core_driver.h client/gframe/undo/core_driver.cpp client/gframe/data_manager.cpp tests/resource_view_tests.cpp
git commit -m "feat: isolate pinned resources and candidate core sessions"
~~~

### Task C3: 确定性重建与 Gate-B 证据

**Files:** 新建 rebuilder.h/.cpp、tests/duel_rebuild_tests.cpp、tests/rebuild-cases.md、docs/rebuild-gate.md；新增 tests/fixtures/duel 下可公开输入。

**Interfaces:**
- bool SamePosition(const Checkpoint&,const Checkpoint&)：比较 player、prompt、canonicalState、transcriptDigest；clock 和 aiLogCursor 属于外部恢复状态，不通过内核运行时间计算。
- std::unique_ptr<CoreDriver> Rebuild(const InitialState&,std::shared_ptr<const ResourceView>,const std::vector<ResponseRecord>&,std::size_t keep,const Checkpoint& target)；失败抛异常、销毁候选、不修改 live。
- duel_rebuild_tests --runtime-root <路径> --suite deterministic|isolation|faults；返回非零表示至少一个用例失败。
- fixture 保存 InitialState、每个有效 ResponseRecord、目标 Checkpoint；格式为版本化长度前缀二进制，主机内部使用，禁止直接发给玩家。

- [ ] **Step 1：写固定历史恢复及故意篡改用例。**

~~~cpp
auto candidate = undo::Rebuild(initial, resources, history, keep, target);
CHECK(undo::SamePosition(candidate->Current().checkpoint, target));
auto changed = target; changed.prompt.push_back(255);
bool rejected = false;
try { undo::Rebuild(initial, resources, history, keep, changed); }
catch (const std::exception&) { rejected = true; }
CHECK(rejected);
CHECK(undo::SamePosition(liveCheckpointAfter, liveCheckpointBefore));
~~~

initial/resources/history/keep/target 从本任务 fixture 解析；liveCheckpointBefore/After 为同一 live 在候选测试前后的无推进查询。CoreDriver 的 Boundary Current() const 返回缓存边界，不得用再次 Advance 改变候选位置。

- [ ] **Step 2：运行 deterministic 和 faults，预期缺少重建或修改目标仍被接受。**
- [ ] **Step 3：实现位置比较与候选重放循环。**

~~~cpp
bool SamePosition(const Checkpoint& a, const Checkpoint& b) {
    return a.player == b.player && a.prompt == b.prompt &&
           a.canonicalState == b.canonicalState && a.transcriptDigest == b.transcriptDigest;
}
~~~

~~~cpp
std::unique_ptr<CoreDriver> Rebuild(
    const InitialState& initial, std::shared_ptr<const ResourceView> resources,
    const std::vector<ResponseRecord>& records, std::size_t keep,
    const Checkpoint& target) {
    if (keep > records.size()) throw std::out_of_range("rebuild prefix");
    auto candidate = CoreDriver::Create(initial, resources);
    auto boundary = candidate->Advance();
    for (std::size_t i = 0; i < keep; ++i) {
        if (boundary.kind != BoundaryKind::AwaitResponse ||
            !SamePosition(boundary.checkpoint, records[i].before))
            throw std::runtime_error("rebuild prompt diverged");
        candidate->Submit(records[i].response);
        boundary = candidate->Advance();
        if (boundary.rejectedResponse) throw std::runtime_error("rebuild response rejected");
    }
    if (boundary.kind != BoundaryKind::AwaitResponse ||
        !SamePosition(boundary.checkpoint, target))
        throw std::runtime_error("rebuild target diverged");
    return candidate;
}
~~~

- [ ] **Step 4：录制并运行真实引擎用例。** tests/rebuild-cases.md 分别建立：不洗切固定牌序、开局洗牌、抽卡与随机效果、支付 LP/丢弃代价、连锁中多次选择、卡片一次一回合使用后撤回。每例记录实际 card ID、固定资源摘要、种子、响应和目标；重建后执行相同后续选择验证效果/次数限制。测试选用当前资源确实存在的卡，不在计划中猜测卡号。
- [ ] **Step 5：测试副作用与失败保留。** 候选输出收集器真实网络/文件/聊天计数为 0；错误响应、脚本缺失和资源视图无法固定时，原句柄及历史不变且能继续；相同 fixture 重建三次摘要一致。
- [ ] **Step 6：与 W1 汇总 Gate-B。** docs/rebuild-gate.md 记录每个用例、首个分歧、耗时、资源策略及 AI 执行器证据。C2/C3/W1 任一失败，不进入 C4/N2 的正式切换，也不取消 AI 范围绕过。
- [ ] **Step 7：提交。**

~~~powershell
git add client/gframe/undo/rebuilder.h client/gframe/undo/rebuilder.cpp client/gframe/undo/core_driver.h tests/duel_rebuild_tests.cpp tests/fixtures/duel tests/rebuild-cases.md docs/rebuild-gate.md
git commit -m "feat: verify deterministic candidate duel reconstruction"
~~~

### Task C4: 单人入口、安全边界、计时与当前分支录像

**Files:** 新建 single_undo.h/.cpp、tests/single_undo_tests.cpp；修改 single_mode.cpp、single_duel.cpp、game.h/.cpp、duelclient.cpp 和录像记录接入文件。

**Interfaces:**
- enum class LocalUndoState { Running, WaitBoundary, Preparing, Failed }。
- SingleUndo::Request(std::uint8_t player) -> bool；记录请求，效果处理中不重入 process。
- SingleUndo::AtBoundary(bool finished) -> void；在安全边界固定 target，再准备候选。
- bool SingleUndo::CanUndo(std::uint8_t player) const；无历史/结束/不支持模式返回 false。
- SingleUndo 拥有 unique_ptr<CoreDriver> live_、DuelHistory history_、ClockState clock_；准备时复制保留旧值，提交前失败原样恢复。

- [ ] **Step 1：写安全边界测试：处理中请求后 live 不变，边界成功才替换；已结束则取消。** 注入失败的 Rebuild 调用，断言 history 长度与 clock 相等。SingleUndo 构造注入 RebuildFn：

~~~cpp
using RebuildFn = std::function<std::unique_ptr<CoreDriver>(
    const InitialState&, std::shared_ptr<const ResourceView>,
    const std::vector<ResponseRecord>&, std::size_t, const Checkpoint&)>;
~~~

失败测试传入同签名 lambda 抛 std::runtime_error("injected rebuild failure")，不得销毁旧句柄。
- [ ] **Step 2：运行 single_undo_tests，预期缺少门控/失败恢复。**
- [ ] **Step 3：在安全边界实现候选先行的切换核心。**

~~~cpp
const auto keep = history_.Target(requester).value();
const auto target = history_.Records().at(keep).before;
auto candidate = rebuild_(live_->Initial(), live_->Resources(), history_.Records(), keep, target);
auto nextHistory = history_;
nextHistory.Truncate(keep);
live_.swap(candidate);
history_ = std::move(nextHistory);
clock_ = target.clock;
~~~

rebuild_ 为上述注入函数，requester 为固定请求人；切换前完成所有可能失败的 UI 候选准备。单人场景虽无同意弹窗仍需冻结旧输入、暂停计时和抑制重放副作用。失败恢复冻结前 clock，成功采用目标 clock。
- [ ] **Step 4：接入单人撤回按钮和状态文案。** 使用待输入边界排队；无历史显示“没有可撤回的选择”，结束显示“本局已结束”，准备显示“正在恢复”。清除旧选择/动画，渲染目标提示后恢复输入；未提交的 UI 勾选不单独成为撤回点。
- [ ] **Step 5：录像改为从初始状态与当前有效 Records 生成。** 成功截断后再保存新分支，原录像响应缓冲区不能继续附带旧分支。实际执行“选 A→撤回→选 B→结束→保存录像→播放”，必须只重放 B 路线，不重复聊天。
- [ ] **Step 6：运行单人场景全流程、连续撤回、两个开局选项四组合与失败注入，预期牌序/规则不变，失败仍可继续。**
- [ ] **Step 7：提交。**

~~~powershell
git add client/gframe/undo/single_undo.h client/gframe/undo/single_undo.cpp client/gframe/single_mode.cpp client/gframe/single_duel.cpp client/gframe/game.h client/gframe/game.cpp client/gframe/duelclient.cpp client/gframe/replay.cpp client/gframe/replay_mode.cpp tests/single_undo_tests.cpp
git commit -m "feat: restore single-player choices at safe input boundaries"
~~~

## 完成条件

Gate-B 有真实内核与 AI 证据，单人模式能连续撤回并继续新路线，错误时原局保留，录像只包含当前分支。联机和 AI 正式提交由后续计划接入。
