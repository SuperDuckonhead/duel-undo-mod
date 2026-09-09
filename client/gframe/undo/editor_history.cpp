#include "editor_history.h"

#include <utility>

namespace undo {

void EditorHistory::Reset(const DeckSnapshot& deck) {
    saved_ = deck;
    undo_.clear();
}

bool EditorHistory::Record(const DeckSnapshot& before, const DeckSnapshot& after) {
    if(before == after)
        return false;
    undo_.push_back(before);
    return true;
}

std::optional<DeckSnapshot> EditorHistory::Undo() {
    if(undo_.empty())
        return std::nullopt;
    auto value = std::move(undo_.back());
    undo_.pop_back();
    return value;
}

bool EditorHistory::CanUndo() const {
    return !undo_.empty();
}

void EditorHistory::Saved(const DeckSnapshot& deck) {
    saved_ = deck;
}

bool EditorHistory::Dirty(const DeckSnapshot& current) const {
    return current != saved_;
}

} // namespace undo