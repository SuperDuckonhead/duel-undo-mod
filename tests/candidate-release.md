# 候选包本机验收：1cd153e

状态：已完成干净构建、精确增量包、原目录安装/启动/卸载/重装及本次已运行的本机自动回归。扩展测试唯一的退出观察断言已修正并独立通过，生产程序不变。两台设备局域网、人工视觉与整体资源稳定性验收仍待完成，不标记正式可发行。

## 构建与文件绑定

构建源码提交：1cd153ea0cc67c6d9de02a4e3b687edbacbf6fe0。独立检出目录为 out/clean-release-20260910；空缓存在线 Bootstrap 验证了 5 个工具及 14 个库。真实干净构建暴露的补丁换行与 Windows 资源文件准备遗漏已修复，失败日志保留。失败的 bin/obj/build 仅移入独立 out/failed-native-811dada；最终 fresh product 检查、Build All Release 和前后源码快照全部通过。

构建日志：out/clean-release-build-1cd153e.log。Prepare-Release 的只读来源复核通过，日志 out/candidate-1cd153e-provenance-verify.log。构建清单绑定 1cd153e 的整个 Git 跟踪源码树（包含当时的测试与文档，仅排除生成的 release-files.json）；后续新增验收驱动属于测试提交 ec546ff，不在该构建源码 ZIP 内。

| 文件 | SHA-256 |
| --- | --- |
| ygopro-undo.exe | e8caab0799bb9d0cba20d7f75c1c735bdd6e75fb6a03306124e06f2bbbec009f |
| WindBot-undo.exe | 6582a35a5a270c93854449ea135892f055ba731cbb370a2e26d80334e720ad61 |
| ygopro-undo-candidate-1cd153e.zip | f160dfcf41ea972e4cb8fea72a5b0d19ed6620b87403f38deb3efe68086ba1ff |
| ygopro-undo-source-1cd153e.zip | e48020822aad45ecab20833b35dec8d654f4a520ccc73d3dfd2432df42e26f16 |
| release-files-1cd153e.json | 9a1642ba3c116246b038d16cda9d0d675d7b597a81d3d340855e306bd6319b8e |

三个文件及 SHA256SUMS-1cd153e.txt 位于 out/packages。候选 ZIP 为 7,640,720 字节，37 个精确条目：5 个独立程序/依赖、5 个说明、26 个许可文本和 1 个构建清单。所有 ZIP 条目和解压文件的哈希均与外部清单相符。源码 ZIP 为 git archive 固定构建提交，619 个跟踪文件的目录检查没有卡图、个人卡组、数据库、原配置、脚本资源库或构建缓存。

## 实际客户端与 AI

Build All Release 的基础 CTest 为 21/21 通过，17.43 秒（out/clean-release-build-1cd153e.log）。配置实际资源与适配 Bot 后，扩展 CTest 首轮为 41/42，89.16 秒（out/candidate-1cd153e-runtime-ctest.log）；唯一失败是 bot_transaction_tests 析构后立即检查工作进程退出的断言。保留精确进程句柄的诊断复现了同一工作进程在析构返回后 0.9639 毫秒才退出；原断言检查过早。测试提交 082755a251414593e98a927bf4f4ca649dd35ab3 改为保留精确句柄并最多等待 5 秒退出，超时仍失败，生产代码没有修改。修正后实际完整 W2 测试 10/10 通过；根独立复核将修正测试与 clean 1cd153e 的生产对象/库链接，对实际已安装 WindBot 运行通过（out/candidate-1cd153e-w2-independent-{build,green,binding}.*）。该定向复核关闭首轮唯一失败，不将首轮记录改写为 42/42。

本轮所有 Game 集成驱动链接最终 clean Release 的客户端对象；该候选客户端 EXE 前后 SHA 一致，且精确等于 ZIP 与实际安装文件。Game/OnEvent 自动化不是人工视觉验收。

- 单人 Game/ReplayMode：四种宿主选项、A→撤回→A→撤回→B、无效响应、状态恢复、保存与回放重启通过。
- 编辑器：真实 Game 与第二进程、增删/拖拽/排序/洗牌/清空/保存及历史隔离通过。
- 两个 Game 进程与实际 TCP：双方确认和仅本机自由撤回均通过；涵盖拒绝后原提示继续、候选控件失败保留双方、两次撤回、B 分支召唤及对手视角继续。自由模式从真实菜单建立且全程没有同意弹窗。
- 实际 AI 菜单：ChainBurn、Hand/两个开局选项、人类和 AI 回合、撤回后新路线、自然结束、网络录像保存/B 分支回放、返回编辑器的历史隔离通过。
- 实际客户端从无关 cwd、中文路径和 Windows 快捷方式启动/正常退出，258 字符可执行路径探测通过。适配 WindBot 的中文路径/独立 SQLite/缺数据库诊断及 W1 全部套件通过。

证据为 out/candidate-1cd153e-{single,editor,consent-pair,free-pair,ai-menu,client-runtime,bot-runtime}.log，程序绑定记录为 out/candidate-1cd153e-local-binding.json。此前实际 AI 列表 62 行、61 执行器的完整宿主历史回归见 ai-history.md；本次不把该先前批次说成逐项重新运行。

## 精确 ZIP 与原安装

精确解压 ZIP 的进程测试通过，out/candidate-1cd153e-process-green.log；结果目录 out/candidate-process-a0c6750916d84d5e8c6c7bdf00ab5f80。测试原版 EXE 的逐字节隔离副本、候选、两个并存候选及重新启动，五个实际进程均正常退出 0；缺 cards.cdb 的第六个进程退出 1，自己的日志包含准确隔离路径。并发退出后的配置与完整顺序保存基准逐字节相同；115 个原安装文件的前后哈希一致。

随后通过 Test-InstallLayout 对实际原目录的检查，只创建清单里的 37 个新增文件。在 F:/MyCardLibrary/ygopro/ygopro-undo.exe 实际启动，cwd 为无关临时目录，生成独立 system-undo.conf，窗口正常关闭，进程 150580 退出 0。原程序、原配置和数据库的早期快照仍一致。

按已安装清单和独立保存的 receipt SHA 逐项卸载，事前验证所有文件，完全没有递归删除或目录删除；新配置保留。卸载后再次从原版 EXE 逐字节隔离副本实际启动并完成全部进程测试，116 个原文件/新配置前后不变，再将同一个 ZIP 安装回原目录。最终保留可启动的候选客户端供用户验收。

安装/卸载记录：out/candidate-1cd153e-{first-install,installed-startup,uninstall,final-install}.json；卸载后进程测试：out/candidate-1cd153e-after-uninstall-process.log。两次外部收据内容相同，SHA-256 为 61e5a0ebb191bddefb4553b666e3871e7205e5241a7be6045b7c74b562b1d27d。

这里的原版运行在隔离目录中，使用原 EXE 字节与共享资源，以保护用户原配置。并未声称在原目录人工操作原版窗口。

## 待完成

- 两台 Windows 设备 LAN 的逐次确认/拒绝/超时、双方发起、连续回退、新路线和隐藏信息核对。
- 人工视觉与完整四模式验收、编辑后保存的卡组用于开局这一完整操作。
- 整体资源稳定性验收：正式 120 次 AI 宿主成功/失败测量已完成，耗时及内存增长/回落的边界见 undo-host-performance.md；不能由有界平台样本推断普遍零泄漏。

本地源码与文件已准备，没有创建远程仓库、推送、打标签或发布 GitHub Release。
