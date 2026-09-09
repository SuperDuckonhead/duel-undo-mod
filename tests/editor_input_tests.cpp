#include "test_support.h"
#include "undo/editor_input.h"
int main() {
    undo::EditorInputState s{true, false, false, false, false, false};
    CHECK(undo::CanEditorUndo(s));
    s.textFocus = true; CHECK(!undo::CanEditorUndo(s)); s.textFocus = false;
    s.dragging = true; CHECK(!undo::CanEditorUndo(s)); s.dragging = false;
    s.modal = true; CHECK(!undo::CanEditorUndo(s)); s.modal = false;
    s.readOnly = true; CHECK(!undo::CanEditorUndo(s)); s.readOnly = false;
    s.siding = true; CHECK(!undo::CanEditorUndo(s)); s.siding = false;
    s.history = false; CHECK(!undo::CanEditorUndo(s));
}
