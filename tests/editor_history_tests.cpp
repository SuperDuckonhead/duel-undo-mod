#include "test_support.h"
#include "undo/editor_history.h"

int main() {
    const undo::DeckSnapshot initial{{
        {100, 200, 100},
        {300, 300},
        {400, 500, 400},
    }};

    auto removed = initial;
    removed[0].erase(removed[0].begin());
    auto moved = removed;
    moved[1].erase(moved[1].begin());
    moved[2].insert(moved[2].begin() + 1, 300);

    undo::EditorHistory history;
    history.Reset(initial);
    CHECK(!history.CanUndo());
    CHECK(history.Record(initial, removed));
    CHECK(history.Record(removed, moved));
    CHECK(history.CanUndo());
    CHECK(history.Undo().value() == removed);
    CHECK(history.Undo().value() == initial);
    CHECK(!history.Undo().has_value());
    CHECK(!history.CanUndo());

    CHECK(!history.Record(initial, initial));
    CHECK(!history.CanUndo());

    auto branched = initial;
    branched[1].push_back(600);
    CHECK(history.Record(initial, branched));
    CHECK(history.Undo().value() == initial);
    CHECK(!history.CanUndo());

    history.Reset(initial);
    CHECK(!history.Dirty(initial));
    CHECK(history.Record(initial, removed));
    CHECK(history.Dirty(removed));
    history.Saved(removed);
    CHECK(!history.Dirty(removed));
    CHECK(history.CanUndo());
    CHECK(history.Undo().value() == initial);

    CHECK(history.Dirty(initial));
    // A cancelled gesture is a no-op even though its live vector had a provisional pop.
    const auto cancelled = initial;
    CHECK(!history.Record(initial, cancelled));
    CHECK(!history.CanUndo());
    CHECK(history.Record(initial, moved));
    history.Reset(removed);
    CHECK(!history.CanUndo());
    CHECK(!history.Undo().has_value());
    CHECK(!history.Dirty(removed));
    CHECK(history.Dirty(initial));
}