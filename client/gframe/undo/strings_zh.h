#pragma once
namespace undo {
inline constexpr wchar_t EditorUndoText[] = L"撤回";
inline constexpr wchar_t EditorUndoEmptyText[] = L"没有可撤回的编辑";
inline constexpr wchar_t EditorUndoHint[] = L"撤回上一步编辑 (Ctrl+Z)";
inline constexpr wchar_t EditorUndoInvalidText[] = L"卡片数据已变化，无法恢复";
inline constexpr wchar_t DeckTestText[] = L"测试卡组";
inline constexpr wchar_t DeckTestHint[] = L"当前卡组\n开始测试";
inline constexpr wchar_t DeckTestPendingText[] = L"正在准备\n测试";
inline constexpr wchar_t DeckTestDraggingText[] = L"完成拖拽\n再测试";
inline constexpr wchar_t DeckTestDialogText[] = L"关闭弹窗\n再测试";
inline constexpr wchar_t DeckTestPackText[] = L"只读卡包\n不能测试";
inline constexpr wchar_t DeckTestReadOnlyText[] = L"只读卡组\n不能测试";
inline constexpr wchar_t DeckTestSidingText[] = L"换备时\n不能测试";
inline constexpr wchar_t DeckTestInactiveText[] = L"仅限卡组\n编辑器测试";
} // namespace undo
