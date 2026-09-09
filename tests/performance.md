# 撤回性能与资源释放记录

2026-09-10，内核独立测试通过；完整客户端、AI、网络同步的性能验收仍待接入后执行。

本轮运行 Windows 10.0.26200 x64、AMD Ryzen 7 9800X3D（8 核 16 线程）、33,487,212,544 字节物理内存、LLVM-MinGW Clang 23.1.1 Release，直接固定读取当前安装的 13,616 份脚本与 15,049 条卡片数据。场景为双方各 40 张原有通常怪兽、初始手牌 0、每回合抽卡 0 的练习局。引擎实际接受了 1001 条结束回合/自动跳过连锁响应；取前 10、100、1000 条建立目标，没有复制无效响应凑数。这是低复杂度长历史样本，不能代表复杂卡组或 AI 的最坏情况。

| 保留响应数 | 成功重建中位数 | 成功 P95 | 成功最大值 | 故意失败中位数 |
| --- | ---: | ---: | ---: | ---: |
| 10 | 24.29 ms | 25.48 ms | 25.73 ms | 22.79 ms |
| 100 | 174.19 ms | 183.05 ms | 183.64 ms | 180.65 ms |
| 1000 | 1701.99 ms | 1749.11 ms | 1751.01 ms | 1699.39 ms |

每个规模各 20 次成功和 20 次目标摘要错误失败。时间包含建立和销毁独立候选，排除资源首次固定和历史录制；首次固定资源 1567.21 ms，录制 1001 条响应 1701.35 ms。中位数为排序后第 10/11 项均值，P95 为第 19 项。

每次候选销毁后检查原句柄的当前状态/输出摘要不变，并采集真实进程资源。所有采样句柄数 201，与各规模预热后的基线一致；子进程数 0（本测试不创建 AI）。PrivateUsage 从 73,572,352 字节保持稳定，后段降至 73,474,048。工作集从约 74.9 MB 预热至 75.1 MB 后稳定，末段约 75.06 MB。未观察到本样本重复恢复导致的持续私有内存增长；这不代替完整进程、复杂长局和 AI 的泄漏检查。完成全部 120 次恢复/失败后，原局仍能接受下一条合法响应。

可复现命令（源码工作区）：

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Measure-Undo.ps1 -RuntimeRoot F:/MyCardLibrary/ygopro -Cases 10,100,1000 -OutFile out/performance/core.json
~~~

工具生成独立 Release 测量目标、原始 JSONL、编译日志和 JSON 记录。每个样本包含 resourceDigest、responses、undoCount、elapsedMs、workingSetBytes、privateBytes、handleCount、childProcessCount。整体记录 sourceCommit、sourceDirty、实际编译输入摘要、测量程序 SHA-256；编译前后源文件摘要不同即失败。本轮存在并行中的 C4 工作，因此明确记录 dirty=true，不宣称测得纯提交版本。

完整采样证据：tests/fixtures/performance/core-2026-09-10.json。

- 提交基准：4931c5124db83c62b1cbb34d898f086b06280dc3
- 编译输入 SHA-256：1210cfcb9f68360856ac4b883f0d4d44e212df665483296d7ed6ad5553a179f8
- 实际测量程序 SHA-256：1d26aa90dec22b29639a25a7072d084c7a8dcbf3b5f6045945b674b4f1c3bb8a
- 资源摘要：bff81471fcd422aa17c700f4d770a65f49886369900bfb2bf7da73517f9a1858
- 实际响应序列摘要：5c695d0093dc68729df174ab40d44e81b35c1716a3ea6a87764583c0bb0ba21e

待验收：完整 UI/网络/AI 下的同规模恢复耗时与内存、候选 AI 进程生命周期、内存不足失败及原局保留、真实两设备 LAN。未完成项仍保留在 acceptance.csv，不标可发行。
