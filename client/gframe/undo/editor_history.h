#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace undo {

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

} // namespace undo