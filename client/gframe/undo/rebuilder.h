#pragma once
#include "core_driver.h"
namespace undo {
// Host-only comparison. Clock and AI cursor are restored by the transaction owner.
bool SamePosition(const Checkpoint&, const Checkpoint&);
// Owns only the candidate. Failure destroys it without receiving a live-session
// reference or any network, replay-file, chat, timer, audio, or UI output sink.
std::unique_ptr<CoreDriver> Rebuild(const InitialState&,
    std::shared_ptr<const ResourceView>, const std::vector<ResponseRecord>&,
    std::size_t keep, const Checkpoint& target);
}
