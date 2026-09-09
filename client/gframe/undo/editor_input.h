#pragma once
namespace undo {
struct EditorInputState {
    bool history;
    bool textFocus;
    bool readOnly;
    bool dragging;
    bool modal;
    bool siding;
};
inline bool CanEditorUndo(const EditorInputState& s) {
    return s.history && !s.textFocus && !s.readOnly && !s.dragging && !s.modal && !s.siding;
}
} // namespace undo
