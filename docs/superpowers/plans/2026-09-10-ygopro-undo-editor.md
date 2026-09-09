# 卡组编辑撤回 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让普通卡组编辑页面按完整操作连续撤回，并准确恢复主、额外、副卡组内容与顺序。

**Architecture:** 使用独立的有序卡片 ID 快照栈；在手势/命令边界记录，不在每个 push/pop 内记录。历史和最近成功保存基准分离，界面通过适配函数恢复现有 Deck 数据。

**Tech Stack:** C++17、现有 DeckBuilder/Irrlicht、CTest；不引入决斗内核、网络或 AI 依赖。

**Spec:** [编辑行为规格](../../../openspec/changes/duel-undo-mod/specs/duel-undo/spec.md)、[设计第 9 节](../../../openspec/changes/duel-undo-mod/design.md)。

## Global Constraints

- 依赖 baseline 的 Gate-A 和 B3；无需等待对战 Gate-B。
- 源码路径相对于 F:/MyCardLibrary/ygopro/dev/duel-undo-mod。
- 支持主/额外/副卡组、重复卡、原位置、连续撤回；不提供重做。
- 加入、删除、跨区移动、排序、洗牌、清空各按完整操作记一步；无变化不记历史。
- 保存同一卡组保留历史，加载/重新加载/新建/另存为新卡组/退出清空历史。
- 撤回只改内存；只有用户主动保存才写 .ydk；文本框 Ctrl+Z 留给文本控件。
- 普通编辑器支持；对战换备、文件删除和卡组文件管理不纳入本次撤回。

---

## 文件边界

| 文件 | 职责 |
| --- | --- |
| client/gframe/undo/editor_history.h、.cpp | 有序内容历史与保存基准。 |
| client/gframe/undo/editor_input.h | 纯逻辑输入可用性判断。 |
| client/gframe/deck_con.h、deck_con.cpp | 快照转换、手势边界、保存/加载生命周期。 |
| client/gframe/game.h、game.cpp | 编辑撤回按钮创建与状态展示。 |
| client/gframe/undo/strings_zh.h | 新增提示文本；不要求覆盖原 strings.conf。 |
| tests/editor_history_tests.cpp | 纯历史、重复卡、保存基准、分支测试。 |
| tests/editor_input_tests.cpp、tests/editor_manual.md | 快捷键判定与真实编辑器回归。 |
| CMakeLists.txt、client/gframe/premake5.lua | 纳入新增源文件；以 B2 实际构建文件映射为准。 |

### Task E1: 有序快照与多步撤回

**Files:** 新建 editor_history.h/.cpp、tests/editor_history_tests.cpp；修改 CMakeLists.txt。

**Interfaces:** 下列类型全部位于 namespace undo；CardId 使用 std::uint32_t；数组 0/1/2 对应 main/extra/side。

~~~cpp
using DeckSnapshot = std::array<std::vector<std::uint32_t>, 3>;
class EditorHistory {
public:
    void Reset(const DeckSnapshot& deck);
    bool Record(const DeckSnapshot& before, const DeckSnapshot& after);
    std::optional<DeckSnapshot> Undo();
    bool CanUndo() const;
    void Saved(const DeckSnapshot& deck);
    bool Dirty(const DeckSnapshot& current) const;
private:
    DeckSnapshot saved_;
    std::vector<DeckSnapshot> undo_;
};
~~~

头文件包含 array、vector、cstdint、optional。Reset/Saved/Dirty 的生命周期接入在 E4；E1 先实现它们的数据语义。

- [ ] **Step 1：写精确恢复重复卡与新分支的失败测试。**

~~~cpp
#include "test_support.h"
#include "undo/editor_history.h"
int main() {
    undo::DeckSnapshot a{{ {100,200,100}, {300}, {400,400} }};
    auto b = a; b[0].erase(b[0].begin());
    auto c = b; c[2].push_back(500);
    undo::EditorHistory h; h.Reset(a);
    CHECK(h.Record(a,b));
    CHECK(h.Record(b,c));
    CHECK(h.Undo().value() == b);
    CHECK(h.Undo().value() == a);
    CHECK(!h.Undo().has_value());
    CHECK(!h.Record(a,a));
    auto d = a; d[1].push_back(600);
    CHECK(h.Record(a,d));
    CHECK(h.Undo().value() == a);
    CHECK(!h.CanUndo());
}
~~~

- [ ] **Step 2：注册 editor_history_tests 并执行。** 先 cmake 配置/构建，预期缺少 EditorHistory 定义导致编译失败；补齐类型后，删除或颠倒快照顺序必须使测试失败。命令：

~~~powershell
cmake -S . -B out/tests
cmake --build out/tests --config Debug
ctest --test-dir out/tests -C Debug -R '^editor_history_tests$' --output-on-failure
~~~

- [ ] **Step 3：实现最小历史逻辑。**

~~~cpp
void EditorHistory::Reset(const DeckSnapshot& deck) { saved_ = deck; undo_.clear(); }
bool EditorHistory::Record(const DeckSnapshot& before, const DeckSnapshot& after) {
    if (before == after) return false;
    undo_.push_back(before);
    return true;
}
std::optional<DeckSnapshot> EditorHistory::Undo() {
    if (undo_.empty()) return std::nullopt;
    auto value = std::move(undo_.back());
    undo_.pop_back();
    return value;
}
bool EditorHistory::CanUndo() const { return !undo_.empty(); }
void EditorHistory::Saved(const DeckSnapshot& deck) { saved_ = deck; }
bool EditorHistory::Dirty(const DeckSnapshot& current) const { return current != saved_; }
~~~

.cpp 包含自身头文件和 utility，并处于 namespace undo。CMake 为测试 target_sources 加 editor_history.cpp。

- [ ] **Step 4：重跑同一测试，预期全部通过。**
- [ ] **Step 5：提交。**

~~~powershell
git add client/gframe/undo/editor_history.h client/gframe/undo/editor_history.cpp tests/editor_history_tests.cpp CMakeLists.txt
git commit -m "feat: add ordered deck editor undo history"
~~~

### Task E2: 在完整手势和命令边界接入历史

**Files:** 修改 deck_con.h/.cpp；扩展 tests/editor_history_tests.cpp；新建 tests/editor_manual.md。

**Interfaces:**
- DeckBuilder::CaptureEditorDeck() const -> undo::DeckSnapshot。
- DeckBuilder::RestoreEditorDeck(const undo::DeckSnapshot&) -> bool，先解析全部 ID 到该编辑会话稳定的 CardDataC 指针，全部成功后一次性替换三个区域；失败不改变当前内容/历史。
- DeckBuilder::BeginEditorEdit() -> void；FinishEditorEdit(bool accepted) -> void。
- DeckBuilder 成员 editorHistory、std::optional<undo::DeckSnapshot> editorEditStart。
- 恢复不经过加卡数量限制，不调用会产生历史的上层操作；数据库重载前结束编辑会话。

- [ ] **Step 1：用三分区快照写取消拖拽用例，再在真实编辑器复现原始拖拽中途 pop 行为。**

~~~cpp
undo::DeckSnapshot before{{ {100,200,100}, {300}, {400} }};
auto during = before;
during[0].erase(during[0].begin() + 1);
auto cancelled = before;
undo::EditorHistory h; h.Reset(before);
CHECK(!h.Record(before,cancelled));
CHECK(!h.CanUndo());
auto moved = during; moved[2].push_back(200);
CHECK(h.Record(before,moved));
CHECK(h.Undo().value() == before);
~~~

单测证明历史合同；tests/editor_manual.md 单独记录拖拽真实接入结果，不能用上述赋值模拟冒充 UI 验证。

- [ ] **Step 2：运行原始编辑器取消跨区拖拽，记录原位置是否丢失以及一次拖拽被低层拆分的风险。** 当前参考源码在实际拖动开始先 pop，必须在这之前调用 BeginEditorEdit；取消和非法落点恢复 before。
- [ ] **Step 3：实现手势包装。**

~~~cpp
void DeckBuilder::BeginEditorEdit() {
    if (!editorEditStart) editorEditStart = CaptureEditorDeck();
}
void DeckBuilder::FinishEditorEdit(bool accepted) {
    if (!editorEditStart) return;
    const auto before = *editorEditStart;
    if (!accepted) {
        if (!RestoreEditorDeck(before)) return;
    } else {
        editorHistory.Record(before, CaptureEditorDeck());
    }
    editorEditStart.reset();
    is_modified = editorHistory.Dirty(CaptureEditorDeck());
}
~~~

Capture 逐区按现有 vector 顺序保存 card->code。Restore 先构造新的 Deck，再用固定基线 CardData 查找接口解析每个 ID，保留重复项；查找失败显示“卡片数据已变化，无法恢复”，禁用后续编辑撤回并保留当前内容。用 B2 确认的数据库查询签名，不猜测上游不存在的方法。

- [ ] **Step 4：逐一接入右键加入/删除、中键副本、搜索结果拖入、区内拖动、跨区拖动、拖拽丢弃。** 每种事件各用 Begin/Finish 包裹一次。鼠标移动的多帧不重复 Begin；窗口失焦、Escape、非法区域用 Finish(false)。失败的 check_limit/push 不入栈。删除低层 push/pop 中任何重复历史记录。
- [ ] **Step 5：包裹排序、洗牌、清空。** 清空仅在确认后提交一步；排序前后相同不记步；筛选/搜索不调用 Begin。真实执行“加入→排序→跨区→撤回三次”，要求逐步回到原快照。
- [ ] **Step 6：运行 editor_history_tests 和 tests/editor_manual.md 的三分区/重复卡/取消案例；预期原顺序精确恢复，一次手势一步。**
- [ ] **Step 7：提交。**

~~~powershell
git add client/gframe/deck_con.h client/gframe/deck_con.cpp tests/editor_history_tests.cpp tests/editor_manual.md
git commit -m "feat: record deck edits at complete gesture boundaries"
~~~

### Task E3: 编辑撤回按钮与 Ctrl+Z 焦点规则

**Files:** 新建 editor_input.h、strings_zh.h、tests/editor_input_tests.cpp；修改 game.h/.cpp、deck_con.h/.cpp、CMakeLists.txt。

**Interfaces:**
- struct EditorInputState { bool history; bool textFocus; bool readOnly; bool dragging; bool modal; bool siding; };
- bool CanEditorUndo(const EditorInputState&)。
- DeckBuilder::UndoEditorEdit() -> bool：统一供按钮和快捷键调用，只有成功恢复才弹出历史。

- [ ] **Step 1：添加焦点/状态的失败测试。**

~~~cpp
undo::EditorInputState s{true,false,false,false,false,false};
CHECK(undo::CanEditorUndo(s));
s.textFocus = true; CHECK(!undo::CanEditorUndo(s));
s.textFocus = false; s.dragging = true; CHECK(!undo::CanEditorUndo(s));
s.dragging = false; s.siding = true; CHECK(!undo::CanEditorUndo(s));
s.siding = false; s.history = false; CHECK(!undo::CanEditorUndo(s));
~~~

- [ ] **Step 2：注册并运行 editor_input_tests，预期 CanEditorUndo 尚未定义。**
- [ ] **Step 3：实现判断和提示。**

~~~cpp
inline bool CanEditorUndo(const EditorInputState& s) {
    return s.history && !s.textFocus && !s.readOnly &&
           !s.dragging && !s.modal && !s.siding;
}
inline constexpr wchar_t EditorUndoText[] = L"撤回";
inline constexpr wchar_t EditorUndoEmptyText[] = L"没有可撤回的编辑";
~~~

UndoEditorEdit 先复制 editorHistory 到候选副本，在副本调用 Undo，RestoreEditorDeck 成功才换入候选历史；随后刷新数量、悬停、选中和 dirty。复制失败同样保留现状。按钮 ID 从固定基线 GUI ID 枚举分配无冲突值。

- [ ] **Step 4：在普通编辑器添加按钮，将键盘按下 Ctrl+Z 路由到同一方法。** 文本框焦点按 Irrlicht 控件类型及其父级判定，不能仅检查键码；文本焦点时让现有控件继续处理。拖拽、弹窗、只读、换备时禁用；刷新按钮状态不修改卡组。
- [ ] **Step 5：运行 editor_input_tests 并手测搜索框/卡组名文本撤回、无历史、连续撤回、弹窗和换备页面；预期不会误触卡组回退。**
- [ ] **Step 6：提交。**

~~~powershell
git add client/gframe/undo/editor_input.h client/gframe/undo/strings_zh.h client/gframe/game.h client/gframe/game.cpp client/gframe/deck_con.h client/gframe/deck_con.cpp tests/editor_input_tests.cpp CMakeLists.txt
git commit -m "feat: expose deck undo with focus-aware shortcut"
~~~

### Task E4: 保存基准、文件边界与编辑会话生命周期

**Files:** 修改 deck_con.cpp、tests/editor_history_tests.cpp、tests/editor_manual.md。

**Interfaces:** 沿用 Reset、Saved、Dirty；普通保存成功调用 Saved，失败不调用；成功 load/new/save-as-new 调用 Reset，取消或失败保持原状态。

- [ ] **Step 1：写保存后撤回与切换卡组测试。**

~~~cpp
undo::DeckSnapshot a{{ {100}, {}, {} }};
auto b = a; b[0].push_back(200);
undo::EditorHistory h; h.Reset(a); h.Record(a,b); h.Saved(b);
CHECK(!h.Dirty(b));
const auto restored = h.Undo().value();
CHECK(restored == a);
CHECK(h.Dirty(restored));
h.Record(a,b);
h.Reset(b);
CHECK(!h.CanUndo());
CHECK(!h.Dirty(b));
~~~

- [ ] **Step 2：在真实编辑器记录“加入→保存→撤回”原行为和 .ydk 指纹，预期当前缺少目标行为。**
- [ ] **Step 3：接入现有保存成功分支。**

~~~cpp
if (saveSucceeded) {
    editorHistory.Saved(CaptureEditorDeck());
    is_modified = editorHistory.Dirty(CaptureEditorDeck());
}
~~~

saveSucceeded 是原 SaveDeck 的实际返回值局部变量，不引入新的隐式磁盘保存。成功切换/重新加载/新建/另存为新名称后调用 Reset；同名普通保存不 Reset；取消退出不清历史，实际退出才清理。
- [ ] **Step 4：运行单测及文件边界回归。** 专用测试 .ydk 在“未保存编辑→撤回→排序→撤回”前后 SHA-256 必须不变；只有按保存改变。保存后撤回显示未保存；模拟只读文件导致保存失败，dirty 与历史保持。双进程打开同一卡组，A 的撤回不改变 B 的内存，保存冲突沿用原程序策略。
- [ ] **Step 5：回到主菜单、进入单人对局再退出编辑，验证两个撤回入口历史完全分开；提交。**

~~~powershell
git add client/gframe/deck_con.cpp tests/editor_history_tests.cpp tests/editor_manual.md
git commit -m "fix: preserve deck undo history across successful saves"
~~~

## 完成条件

E1–E4 单测和真实编辑器回归均通过，可单独交付编辑撤回开发结果；完整 mod 发行仍需决斗、WindBot、LAN 和 release 计划全部通过。
