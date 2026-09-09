#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace undo {

using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;

enum class Origin : std::uint8_t {
	Manual,
	Automatic,
	Bot,
};

struct ClockState {
	std::array<std::int64_t, 2> remainingMs{};
};

struct Checkpoint {
	std::uint8_t player{};
	Bytes prompt{};
	Bytes canonicalState{};
	Digest transcriptDigest{};
	ClockState clock{};
	std::size_t aiLogCursor{};
};

struct ResponseRecord {
	std::uint8_t player{};
	Origin origin{Origin::Manual};
	Bytes response{};
	Checkpoint before{};
};

class DuelHistory {
public:
	void Accept(ResponseRecord record);
	std::optional<std::size_t> Target(std::uint8_t requester) const;
	void Truncate(std::size_t keep);
	const std::vector<ResponseRecord>& Records() const;

private:
	std::vector<ResponseRecord> records_;
};

} // namespace undo
