#include "duel_history.h"

#include <stdexcept>
#include <utility>

namespace undo {

void DuelHistory::Accept(ResponseRecord record) {
	records_.push_back(std::move(record));
}

std::optional<std::size_t> DuelHistory::Target(std::uint8_t requester) const {
	for(std::size_t i = records_.size(); i > 0; --i) {
		const auto& record = records_[i - 1];
		if(record.player == requester && record.origin == Origin::Manual)
			return i - 1;
	}
	return std::nullopt;
}

void DuelHistory::Truncate(std::size_t keep) {
	if(keep > records_.size())
		throw std::out_of_range("history target");
	records_.resize(keep);
}

const std::vector<ResponseRecord>& DuelHistory::Records() const {
	return records_;
}

} // namespace undo
