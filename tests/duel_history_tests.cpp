#include "test_support.h"
#include "undo/duel_history.h"

#include <cstdint>
#include <stdexcept>

namespace {

undo::Checkpoint CheckpointFor(std::uint8_t player, std::uint8_t marker) {
	undo::Digest digest{};
	digest[0] = marker;
	return {
		player,
		{static_cast<std::uint8_t>(marker + 1), static_cast<std::uint8_t>(marker + 2)},
		{static_cast<std::uint8_t>(marker + 3), static_cast<std::uint8_t>(marker + 4)},
		digest,
		{{120000 - marker, 90000 + marker}},
		marker,
	};
}

} // namespace

int main() {
	undo::DuelHistory history;
	CHECK(history.Records().empty());
	CHECK(!history.Target(0).has_value());
	CHECK(!history.Target(1).has_value());

	const auto player0 = CheckpointFor(0, 10);
	const auto player1 = CheckpointFor(1, 20);
	history.Accept({0, undo::Origin::Manual, {1}, player0});
	history.Accept({1, undo::Origin::Manual, {2}, player1});
	history.Accept({0, undo::Origin::Automatic, {3}, CheckpointFor(0, 30)});
	history.Accept({0, undo::Origin::Bot, {4}, CheckpointFor(0, 40)});

	CHECK(history.Target(0).value() == 0);
	CHECK(history.Target(1).value() == 1);
	CHECK(history.Records().size() == 4);
	CHECK(history.Records()[0].before.prompt == undo::Bytes({11, 12}));
	CHECK(history.Records()[0].before.canonicalState == undo::Bytes({13, 14}));
	CHECK(history.Records()[0].before.transcriptDigest[0] == 10);
	CHECK(history.Records()[0].before.clock.remainingMs[0] == 119990);
	CHECK(history.Records()[0].before.clock.remainingMs[1] == 90010);
	CHECK(history.Records()[0].before.aiLogCursor == 10);

	history.Truncate(history.Target(1).value());
	CHECK(history.Records().size() == 1);
	CHECK(!history.Target(1).has_value());
	history.Truncate(history.Target(0).value());
	CHECK(history.Records().empty());

	bool rejectedPastEnd = false;
	try {
		history.Truncate(1);
	} catch(const std::out_of_range&) {
		rejectedPastEnd = true;
	}
	CHECK(rejectedPastEnd);

	auto saved = CheckpointFor(0, 50);
	history.Accept({0, undo::Origin::Manual, {5, 6}, saved});
	saved.prompt[0] = 255;
	saved.canonicalState[0] = 255;
	saved.transcriptDigest[0] = 255;
	CHECK(history.Records()[0].before.prompt == undo::Bytes({51, 52}));
	CHECK(history.Records()[0].before.canonicalState == undo::Bytes({53, 54}));
	CHECK(history.Records()[0].before.transcriptDigest[0] == 50);

	history.Truncate(0);
	history.Accept({1, undo::Origin::Manual, {7}, CheckpointFor(1, 60)});
	CHECK(history.Records().size() == 1);
	CHECK(history.Records()[0].response == undo::Bytes({7}));
	CHECK(!history.Target(0).has_value());
	CHECK(history.Target(1).value() == 0);
}
