#pragma once

#include "undo/deck_test_model.h"

namespace deck_test_fixture {

struct FixedPosition {
	undo::DeckTestContext context;
	undo::CardReference existingCard;
	undo::CardAvailability availableCopies;
	undo::CardReference firstCopy;
	undo::CardReference secondCopy;
	undo::SelectionPlan resolutionMaterials;
};

inline FixedPosition MakeFixedPosition() {
	undo::Digest resources{};
	resources[0] = 0xa5;
	undo::Digest transcript{};
	transcript[0] = 0x5a;
	undo::Checkpoint checkpoint{
		0,
		{0x16, 0x01},
		{0x27, 0x02},
		transcript,
		{{87000, 91000}},
		4,
	};
	undo::DeckTestContext context{
		{41},
		{7},
		4,
		checkpoint,
		resources,
		3,
	};
	undo::CardReference existing{
		undo::CardReferenceKind::Existing,
		{1001},
		900000001,
		1,
		undo::CardSource::ExistingState,
		undo::CardLocation{1, 0x10, 2, 0x4},
	};
	undo::CardReference first{
		undo::CardReferenceKind::NewCopy,
		{2001},
		900000002,
		0,
		undo::CardSource::OwnMainDeck,
		std::nullopt,
	};
	undo::CardReference second = first;
	second.instance = {2002};
	undo::CardAvailability availability{
		900000002,
		0,
		undo::CardSource::OwnMainDeck,
		2,
	};
	undo::SelectionPlan plan{
		context,
		undo::SelectionRole::ResolutionMaterial,
		undo::BindingStage::Resolution,
		{existing, first, second},
	};
	return {context, existing, availability, first, second, plan};
}

} // namespace deck_test_fixture
