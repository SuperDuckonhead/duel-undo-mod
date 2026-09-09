#pragma once
#include "duel_history.h"
namespace undo {
struct VisibleQueryPair {
    Bytes owner, opponent;
};
// Exact legacy UPDATE_DATA projection used by SingleDuel RefreshHand/Mzone/Szone.
// Both outputs are network game-message bytes, beginning with MSG_UPDATE_DATA.
VisibleQueryPair FilterVisibleQuery(const Bytes &legacyUpdateData);
} // namespace undo
