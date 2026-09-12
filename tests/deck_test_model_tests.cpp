#include "deck_test_fixture.h"
#include "test_support.h"

#include <variant>

using namespace undo;

int main() {
	const auto fixture = deck_test_fixture::MakeFixedPosition();

	// Removing either stable ID or collapsing same-code copies must invalidate
	// this otherwise complete material plan.
	CHECK(Validate(fixture.context) == DeckTestError::None);
	CHECK(Validate(fixture.availableCopies, fixture.context) == DeckTestError::None);
	CHECK(Validate(fixture.resolutionMaterials) == DeckTestError::None);
	CHECK(fixture.firstCopy.code == fixture.secondCopy.code);
	CHECK(fixture.firstCopy.instance != fixture.secondCopy.instance);
	CHECK(fixture.context.historyCursor == 4);
	CHECK(fixture.context.resourceVersion[0] == 0xa5);
	auto unidentifiedContext = fixture.context;
	unidentifiedContext.session = {};
	CHECK(Validate(unidentifiedContext) == DeckTestError::MissingContextIdentity);

	auto unidentified = fixture.firstCopy;
	unidentified.instance = {};
	CHECK(Validate(unidentified) == DeckTestError::MissingInstance);

	auto duplicate = fixture.resolutionMaterials;
	duplicate.cards[2].instance = duplicate.cards[1].instance;
	CHECK(Validate(duplicate) == DeckTestError::DuplicateInstance);

	// An existing entity has a live location, while a prospective copy has one
	// of the two eligible sources and no pretend engine location.
	auto existingFromDeck = fixture.existingCard;
	existingFromDeck.source = CardSource::OwnMainDeck;
	CHECK(Validate(existingFromDeck) == DeckTestError::ContradictoryReference);
	auto newInGraveyard = fixture.firstCopy;
	newInGraveyard.location = CardLocation{0, 0x10, 0, 0x4};
	CHECK(Validate(newInGraveyard) == DeckTestError::ContradictoryReference);
	auto opponentCopy = fixture.firstCopy;
	opponentCopy.owner = 1;
	CHECK(Validate(opponentCopy, fixture.context) == DeckTestError::IneligibleSource);
	auto facedownExtra = fixture.availableCopies;
	facedownExtra.source = CardSource::OwnFacedownExtraDeck;
	CHECK(Validate(facedownExtra, fixture.context) == DeckTestError::None);
	auto unavailable = fixture.availableCopies;
	unavailable.quantity = 0;
	CHECK(Validate(unavailable, fixture.context) == DeckTestError::InvalidQuantity);

	auto tooEarly = fixture.resolutionMaterials;
	tooEarly.bindingStage = BindingStage::Activation;
	CHECK(Validate(tooEarly) == DeckTestError::WrongBindingStage);
	auto empty = fixture.resolutionMaterials;
	empty.cards.clear();
	CHECK(Validate(empty) == DeckTestError::IncompletePlan);

	ModeChangedEvent mode{fixture.context, DeckTestMode::Editing};
	CardIntroducedEvent introduced{fixture.context, fixture.firstCopy, BindingStage::Resolution};
	ResponseRecord legacy{0, Origin::Manual, {0x04, 0x00, 0x00, 0x00}, fixture.context.checkpoint};
	ResponseAcceptedEvent accepted{fixture.context, legacy};
	DeckTestEvent events[] = {mode, introduced, accepted};
	CHECK(std::holds_alternative<ModeChangedEvent>(events[0]));
	CHECK(Validate(std::get<CardIntroducedEvent>(events[1])) == DeckTestError::None);
	auto existingIntroduction = introduced;
	existingIntroduction.card = fixture.existingCard;
	CHECK(Validate(existingIntroduction) == DeckTestError::ContradictoryReference);
	const auto& readable = std::get<ResponseAcceptedEvent>(events[2]).response;
	CHECK(readable.response == Bytes({0x04, 0x00, 0x00, 0x00}));
	CHECK(readable.before.prompt == Bytes({0x16, 0x01}));
	CHECK(readable.before.canonicalState == Bytes({0x27, 0x02}));
	CHECK(readable.before.aiLogCursor == 4);
}
