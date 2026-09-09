#pragma once
namespace undo {
inline constexpr wchar_t EditorUndoText[] = L"撤回";
inline constexpr wchar_t EditorUndoEmptyText[] = L"没有可撤回的编辑";
inline constexpr wchar_t EditorUndoHint[] = L"撤回上一步编辑 (Ctrl+Z)";
inline constexpr wchar_t EditorUndoInvalidText[] = L"卡片数据已变化，无法恢复";
} // namespace undo
