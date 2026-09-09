#include "player_visible_filter.h"
#include "../config.h"
#include "../network.h"
#include "protocol.h"
#include <cstring>
#include <stdexcept>
namespace undo {
VisibleQueryPair FilterVisibleQuery(const Bytes &input) {
    if (input.size() < 3 || input.size() > MaxRestoreBytes || input[0] != MSG_UPDATE_DATA || input[1] > 1 ||
        (input[2] != LOCATION_HAND && input[2] != LOCATION_MZONE && input[2] != LOCATION_SZONE))
        throw std::invalid_argument("invalid visible UPDATE_DATA");
    VisibleQueryPair result{input, input};
    for (std::size_t at = 3; at < input.size();) {
        if (input.size() - at < 4)
            throw std::invalid_argument("truncated visible query length");
        std::uint32_t len{};
        std::memcpy(&len, input.data() + at, 4);
        if (len < 4 || len % 4 != 0 || len > input.size() - at)
            throw std::invalid_argument("invalid visible query length");
        if (len > LEN_HEADER) {
            if (len < 16)
                throw std::invalid_argument("query missing visibility position");
            const auto position = input[at + 15];
            const bool hand = input[2] == LOCATION_HAND;
            const bool hidden =
                hand ? !(position & POS_FACEUP) : ((position & POS_FACEDOWN) && !(position & POS_REVEAL));
            if (!hand) {
                result.owner[at + 15] &= ~POS_REVEAL;
                result.opponent[at + 15] &= ~POS_REVEAL;
            }
            if (hidden)
                std::memset(result.opponent.data() + at + 4, 0, len - 4);
        }
        at += len;
    }
    return result;
}
} // namespace undo
