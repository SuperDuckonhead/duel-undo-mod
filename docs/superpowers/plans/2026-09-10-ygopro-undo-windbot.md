# WindBot 撤回适配 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让适配 WindBot 在撤回后恢复决策上下文、随机状态和选择队列，并能继续新的行动路线。

**Architecture:** 每个活动/候选 AI 使用独立 WindBot-undo 进程，避免现有静态字段在两个会话间串用。候选从同样初始化重放真实可见消息和决策回调，逐项校验随机消耗及响应，准备成功后才随宿主事务切换。

**Tech Stack:** C#、.NET Framework 4.8、现有 GameAI/GameBehavior/Executor、命名管道、MSBuild、自包含控制台测试。

**Spec:** [AI 设计](../../../openspec/changes/duel-undo-mod/design.md)、[AI 行为规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)。

## Global Constraints

- W1 依赖 Gate-A/B3，可与 C1–C3 分别推进；W1 与 C2/C3 共同形成 Gate-B。
- W2/W3 依赖 Gate-B 及 network 的 N1/N2 协议/事务合同。
- 源码路径相对于 F:/MyCardLibrary/ygopro/dev/duel-undo-mod。
- 产物固定 WindBot/WindBot-undo.exe，保留原 WindBot.exe 和共享依赖。
- 直接读取原 WindBot/Decks、Dialogs、bots.json，并使用与客户端一致的固定数据库/扩展资源视图。
- 真人对 AI 撤回不需 AI 同意，但需 AI 完整准备与提交确认。
- 不用“仅刷新场面”或“把历史响应喂给内核”代替 AI 决策回调重建。
- 未验证或失败的实际 AI 列表项必须明确记录，不能宣称完整支持；全功能发行受其阻断。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| bot/Undo/ReplayRandom.cs、DecisionTape.cs | 随机源与外部输入顺序记录/校验。 |
| bot/Undo/ReplaySession.cs、UndoControl.cs | 回放真实 AI 回调、输出隔离、候选控制。 |
| bot/Program.cs、bot/Game/GameAI.cs、GameBehavior.cs | 初始化、调度、响应/聊天出口接入。 |
| bot/Game/AI/Executor.cs 及审计找到的随机/时间调用点 | 将影响决策的非确定输入统一路由。 |
| bot/WindBot.csproj、bot-tests/UndoTests.csproj、bot-tests/Program.cs | 产物命名、固定 .NET 4.8 控制台测试。 |
| client/gframe/undo/bot_controller.h、.cpp | 活动/候选进程管理、主事务参与者。 |
| tests/ai-cases.csv、tests/ai-manual.md、docs/rebuild-gate.md | 实际 AI 清单与 Gate-B 证据。 |

### Task W1: 确定性 AI 重建验证

**Files:** 新建 ReplayRandom.cs、DecisionTape.cs、ReplaySession.cs、bot-tests 项目；修改 Program.cs、GameAI.cs、GameBehavior.cs、WindBot.csproj；更新 docs/rebuild-gate.md。

**Interfaces:** namespace WindBot.Undo；测试不依赖 NUnit/xUnit，断言失败抛 InvalidOperationException，Main 返回非零。

~~~csharp
public enum TapeKind { Message, Decision, Random, Clock, External }
public sealed class TapeEntry {
    public TapeKind Kind;
    public string Call;
    public byte[] Input;
    public byte[] Output;
}
public sealed class BotInit {
    public string Executor;
    public int Seed;
    public byte[] Deck;
    public byte[] ResourceDigest;
    public byte[] Options;
}
public sealed class DecisionTape {
    public void Observe(TapeEntry entry);
    public void BeginReplay(TapeEntry[] entries);
    public void RequireEnd();
    public TapeEntry[] Snapshot();
}
public sealed class ReplaySession {
    public ReplaySession(BotInit init);
    public byte[][] Dispatch(byte[] visibleMessage);
    public void Replay(TapeEntry[] entries, int stopBeforeEntry);
    public byte[] DecisionStateDigest();
}
~~~

Dispatch 必须调用实际 GameBehavior 消息处理和 GameAI 决策链；返回本次生成的响应数组。DecisionStateDigest 仅供宿主/测试，校验队列、执行器回调进度和随机/外部输入游标，不能用它代替后续行为验证。候选无真实对局 Socket；对局响应与聊天经可注入 sink 收集。

- [ ] **Step 1：建立会失败的 tape 分歧用例。**

~~~csharp
var tape = new DecisionTape();
tape.BeginReplay(new[] {
    new TapeEntry { Kind = TapeKind.Decision, Call = "response",
                    Input = new byte[] { 1 }, Output = new byte[] { 2 } }
});
bool rejected = false;
try {
    tape.Observe(new TapeEntry { Kind = TapeKind.Decision, Call = "response",
                                Input = new byte[] { 1 }, Output = new byte[] { 3 } });
} catch (InvalidOperationException) { rejected = true; }
if (!rejected) throw new InvalidOperationException("AI divergence accepted");
~~~

- [ ] **Step 2：构建并运行 bot-tests，预期类型缺失；有实现后此例必须因响应分歧被正确拒绝。**

~~~powershell
powershell -NoProfile -File tools/Build.ps1 -Target Bot -Configuration Debug
& .\out\bot-tests\UndoTests.exe --suite tape
if ($LASTEXITCODE -ne 0) { throw 'Bot tape tests failed' }
~~~

B2 build-profile.json 为 Bot 目标增加 UndoTests.csproj，固定输出 out/bot-tests。Program.Main 支持 --suite tape|rebuild|faults|all，未知 suite 返回非零。

- [ ] **Step 3：实现严格顺序 tape。**

~~~csharp
private readonly List<TapeEntry> recorded = new List<TapeEntry>();
private TapeEntry[] expected;
private int cursor;
public void Observe(TapeEntry entry) {
    if (expected == null) { recorded.Add(Clone(entry)); return; }
    if (cursor >= expected.Length) throw new InvalidOperationException("Extra AI event");
    var want = expected[cursor];
    if (want.Kind != entry.Kind || want.Call != entry.Call ||
        !want.Input.SequenceEqual(entry.Input) || !want.Output.SequenceEqual(entry.Output))
        throw new InvalidOperationException("AI event diverged at " + cursor);
    cursor++;
}
public void RequireEnd() {
    if (expected != null && cursor != expected.Length)
        throw new InvalidOperationException("Missing AI events");
}
~~~

同文件实现 Clone 深拷贝 Input/Output；BeginReplay 深拷贝数组并将 cursor=0；Snapshot 返回记录的深拷贝。使用 System.Collections.Generic、System.Linq。禁止把待校验旧输出直接当新决策返回。

- [ ] **Step 4：审计并控制非确定输入。**

~~~powershell
rg 'new Random|Program\.Rand|DateTime\.|Environment\.TickCount|Stopwatch|Task\.Run|Thread|Guid\.NewGuid' bot
~~~

ReplayRandom : Random 持有独立 Random inner 和 DecisionTape tape，初始化使用 BotInit.Seed。重写 Next()、Next(int)、Next(int,int)、NextDouble()、NextBytes(byte[]) 和 Sample()，调用 inner 计算真实值再 Observe；不能调用自己的另一个重写方法造成双重记数。示例：

~~~csharp
public override int Next(int maxValue) {
    int value = inner.Next(maxValue);
    tape.Observe(new TapeEntry {
        Kind = TapeKind.Random, Call = "Next(max)",
        Input = BitConverter.GetBytes(maxValue), Output = BitConverter.GetBytes(value)
    });
    return value;
}
~~~

其余重写方法使用对应参数/结果的固定小端序编码；回放与首次运行做同样计算并验证，不能只回传 tape 值而不推进 RNG。影响决策的时钟/外部值在原运行记录、回放按序提供；调度固定为消息队列单线程。进程隔离保留既有静态字段语义，避免同时运行两个 GameAI 对象共享 Program.Rand。

- [ ] **Step 5：录制真实消息和决策回调。** Message entry 在处理前入 tape，Random/Clock 在处理内部入 tape，Decision 由真实 response sink 记录。Replay 只把 Message/External 驱动项送给真实处理器，随机/决策项由执行过程中 Observe 消耗校验，不能再把它们逐项手动注入一遍。
- [ ] **Step 6：测试至少一个带持续执行器字段、一个待选队列、一个随机决策的实际 AI。** 记录初始化、可见消息、响应、目标 aiLogCursor；候选重放到相同边界，再给活动/候选相同下一条消息，响应字节必须相同。真实发送/聊天计数必须为 0，原进程能继续。结果写入 Gate-B。
- [ ] **Step 7：提交。**

~~~powershell
git add bot/Undo bot/Program.cs bot/Game/GameAI.cs bot/Game/GameBehavior.cs bot/WindBot.csproj bot-tests docs/rebuild-gate.md build-profile.json
git commit -m "feat: verify deterministic WindBot decision replay"
~~~

### Task W2: AI 候选进程与宿主事务切换

**Files:** 新建 UndoControl.cs、bot_controller.h/.cpp；修改 ReplaySession.cs、Program.cs 和 network N2 的 coordinator 接入。

**Interfaces:**
- BotController::Prepare(const TxKey&,std::size_t aiCursor) -> bool。
- BotController::Commit(const TxKey&) -> bool；Abort(const TxKey&) -> void。
- TxKey 使用 network N1 的 session/epoch/request/targetIndex/targetDigest；AI 控制通道不向对手广播日志。
- Checkpoint.aiLogCursor 必须位于一次真实消息/决策回调完整结束的位置，不能截断到回调内部；记录对应已排空输入队列的边界。
- 候选参数 --undo-candidate --control-pipe <随机管道名>，通过管道发送 BotInit/tape；命令行不携带完整历史。
- 管道仅本机当前用户和创建者可访问；活动/候选响应都带 session+epoch，宿主拒绝旧 epoch。

- [ ] **Step 1：建立故障测试序列。** Prepare 创建候选后注入 tape 分歧，要求 Abort 只终止候选；活动 AI PID 保留、目标 epoch 不变。Commit 重复两次只生效一次。把旧活动 AI 迟到响应送入 N1 输入门控，要求不进入内核。
- [ ] **Step 2：运行 UndoTests --suite faults 和 network 的 bot_transaction_tests，预期尚未提供事务参与者。**
- [ ] **Step 3：实现明确进程状态。**

~~~csharp
public enum BotUndoState { Running, Frozen, Ready, Committed, Failed }
public static bool CanEmitResponse(BotUndoState state, ulong responseEpoch, ulong activeEpoch) {
    return state == BotUndoState.Running && responseEpoch == activeEpoch;
}
~~~

客户端对应 BotController 用 RAII 进程/管道句柄，Prepare 先冻结原 AI 再启动候选。通过全部 tape 校验返回 Ready；Commit 只切换活动引用，不立即解除冻结；收到 N2 的最终 Resume 才允许新响应，旧进程在全体提交确认后释放。提交结果不明时原/新都保持冻结，不恢复任一方单独出牌。
- [ ] **Step 4：接入资源视图。** 候选直接使用原 AI Decks/Dialogs/bots.json 的会话固定字节；数据库和扩展决策数据通过与内核相同摘要校验的只读视图/管道传入。不能让候选从已变化的磁盘重新读出不同规则。
- [ ] **Step 5：运行 AI 对局撤回，验证无同意弹窗但仍等待 AI Ready/Ack；改走新路线后 AI 正常继续。** 注入准备失败、管道断开、丢 CommitAck，分别保留原局或全员暂停。
- [ ] **Step 6：提交。**

~~~powershell
git add bot/Undo/UndoControl.cs bot/Undo/ReplaySession.cs bot/Program.cs client/gframe/undo/bot_controller.h client/gframe/undo/bot_controller.cpp
git commit -m "feat: coordinate candidate WindBot processes with undo commits"
~~~

### Task W3: 实际 AI 列表与连续撤回回归

**Files:** 新建 tests/ai-cases.csv、tests/ai-manual.md；扩展 bot-tests/Program.cs 和 tests/results 输出。

**Interfaces:** UndoTests.exe --suite rebuild --cases tests/ai-cases.csv --runtime-root <安装目录>；CSV 列为 botId、executor、deckRelativePath、fixtureRelativePath、scenario、expected。读取实际 bots.json 生成覆盖项，不把私人原始卡组提交。

- [ ] **Step 1：先生成当前列表覆盖检查，存在无结果的 AI 时返回非零。**

~~~powershell
$cases = Import-Csv -LiteralPath 'tests/ai-cases.csv'
if (-not $cases -or ($cases | Where-Object { $_.expected -ne 'pass' })) {
    throw 'AI coverage incomplete'
}
~~~

完整性还需把解析 bots.json 得到的 botId 集合与 CSV 比较，重复/缺失均失败；不能只改 expected 字段来关闭失败。
- [ ] **Step 2：为每种执行器建立实际可运行历史。** 同一执行器多个牌组记录映射，至少覆盖真人回合、AI 回合、连续两次撤回和改选。不能仅凭一次 AI 能启动就标记恢复通过。
- [ ] **Step 3：执行自动重放和继续决策，写出实际结果。** 加入随机输入故意变更与执行器字段错误用例，要求能检测分歧。
- [ ] **Step 4：手工完成选 AI→开局→撤回→新路线→正常结束，检查对局计时、原 AI 进程释放与无重复聊天；失败写最小复现。**
- [ ] **Step 5：覆盖项全部有真实结果后提交；未通过的保留未完成并阻断发行。**

~~~powershell
git add bot-tests/Program.cs tests/ai-cases.csv tests/ai-manual.md docs/rebuild-gate.md
git commit -m "test: cover installed AI executors across undo branches"
~~~

## 完成条件

W1 重放确定性、W2 提交协议和 W3 实际 AI 列表均通过。不同执行器的失败不得靠隐藏 AI 列表或删除原有 AI 支持解决。
