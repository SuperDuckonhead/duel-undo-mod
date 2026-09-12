#pragma once
namespace undo {
inline constexpr wchar_t EditorUndoText[] = L"撤回";
inline constexpr wchar_t EditorUndoEmptyText[] = L"没有可撤回的编辑";
inline constexpr wchar_t EditorUndoHint[] = L"撤回上一步编辑 (Ctrl+Z)";
inline constexpr wchar_t EditorUndoInvalidText[] = L"卡片数据已变化，无法恢复";
inline constexpr wchar_t DeckTestText[] = L"测试卡组";
inline constexpr wchar_t DeckTestHint[] = L"使用当前卡组开始测试";
inline constexpr wchar_t DeckTestPendingText[] = L"正在准备测试";
inline constexpr wchar_t DeckTestDraggingText[] = L"完成拖拽后再测试";
inline constexpr wchar_t DeckTestDialogText[] = L"关闭弹窗后再测试";
inline constexpr wchar_t DeckTestPackText[] = L"只读卡包不能测试";
inline constexpr wchar_t DeckTestReadOnlyText[] = L"只读卡组不能测试";
inline constexpr wchar_t DeckTestSidingText[] = L"换备时不能测试";
inline constexpr wchar_t DeckTestInactiveText[] = L"仅能从卡组编辑器测试";
} // namespace undo
