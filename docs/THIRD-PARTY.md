# 来源与许可

此分支修改了 YGOPro 客户端、决斗适配及 WindBot，并保留上游版权与许可。源码中 sources.lock.json 固定来源提交、第三方归档地址和校验值；tools/Bootstrap.ps1 按该清单取回源码/构建工具。许可原文收录于 licenses 目录，不能用本页摘要替代。

| 组件 | 源码/来源 | 随附许可 |
| --- | --- | --- |
| YGOPro 客户端 | mycard/ygopro，1e8472b8bd51e1242be133189d547ac2f55eddaa | GPL v2，ygopro.txt |
| 决斗内核 | mycard/ygopro-core，e04144d62499c17d0cfa8313f9742434ef99c3a7 | MIT，ocgcore.txt |
| WindBot | mycard/windbot，3a6a462828046e05793e8f74bf446c2d5c713e8c | MIT，windbot.txt |
| Lua | 锁定的 Lua 源码及本仓库确定性补丁 | lua.txt |
| Irrlicht | 20f86d251624334d53585a1809ea4c7178f72a34 及控件恢复补丁 | irrlicht.txt，内含压缩组件另附许可 |
| FreeType | sources.lock.json 固定归档 | freetype.txt、freetype-gpl2.txt；本 GPL 客户端使用 GPL 选项 |
| libevent / zlib / libpng | 固定源码静态构建 | libevent.txt、zlib.txt、libpng.txt |
| libjpeg-turbo | 固定源码静态构建 | jpeg.txt、jpeg-ijg.txt |
| liblzma | 固定源码静态构建 | liblzma.txt、liblzma-0bsd.txt |
| miniaudio / Ogg / Vorbis / Opus / opusfile | 固定源码静态构建 | 同名许可文本 |
| SQLite | 客户端固定 amalgamation；WindBot 上游仓库的 x86/x64 SQLite DLL | sqlite.txt；新增 DLL 置于私有 undo-deps，保留原 DLL |
| Mono.Data.Sqlite / SQLite provider | WindBot 内嵌第三方源码，保留各文件的原声明 | sqlite-provider.txt |
| LLVM / MinGW 运行库 | 锁定的 LLVM-MinGW 工具链，静态链接部分 | llvm.txt、mingw-runtime.txt、winpthreads.txt |

本软件包含 FreeType 项目的字体渲染组件。This software is based in part on the work of the Independent JPEG Group. 完整构建工具、测试运行库不进入游戏增量 ZIP。

卡图、个人卡组、卡片数据库、卡片脚本、字体和音效来自用户既有安装，本候选包不分发这些资源。源码包与二进制包应按构建清单使用同一源提交；对外分发时同时提供该提交的修改源码、构建脚本及对应依赖来源。当前产物仅作本地候选，不代表已完成对外发布。
