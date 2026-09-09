# 实际 AI 列表恢复回归

2026-09-10，最终 62/62 条配置通过，覆盖 61 个执行器、9 条隐藏项以及两条 ChaosRitual 配置。逐条结果与产物哈希见 fixtures/ai-history/2026-09-10-results.csv 和对应 provenance.txt。

每条使用实际 bots.json 的名称、执行器、对话和实际 AI 卡组；真人测试卡组为 40 张通常怪兽斧王。真实宿主/内核与私有 AI 进程完成两轮人工结束回合及 AI 行动，连续两次撤回，改走普通召唤分支，再验证 AI 继续决策，最后通过真实投降命令合法结束。每次撤回实际构造 N3 候选模型与私有候选 AI，等待 Commit/Ack/Resume。所有成功条目结束后后代进程数回到基线。

首轮 58/62；保留了 Qliphort/Zefra/SuperheavySamurai 的无链选卡提示失败，以及 DarkMagician 的虚拟附加卡名查询失败。修复共用原因后重新跑了全部 62 条，未删掉失败项。源提交中的 focused 测试保留两个失败的最小复现。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-AiHostHistory.ps1 -Configuration Release -RuntimeRoot F:/MyCardLibrary/ygopro -All -Workers 2
```

这是每项一条真实宿主/内核/AI/N3 恢复轨迹，不是每种牌组全部分支、另一先后手方向或完整 GUI/TCP 的穷举，也不声称自然胜负。结果 visual=pending、termination=human-surrender 明确保留；完整客户端 AI 界面测试和两台设备联机验收分别记录。测试不修改原 AI 列表、卡组和资源；真实种子、逐项日志和恢复包保留在 ignored out 下。
