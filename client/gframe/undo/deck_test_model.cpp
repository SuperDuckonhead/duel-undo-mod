#include "deck_test_model.h"

#include <unordered_set>

namespace undo {
namespace {

bool IsNewCopySource(CardSource source) {
	return source == CardSource::OwnMainDeck || source == CardSource::OwnFacedownExtraDeck;
}

std::optional<BindingStage> RequiredStage(SelectionRole role) {
	switch(role) {
	case SelectionRole::ActivationCost:
	case SelectionRole::ActivationTarget:
		return BindingStage::Activation;
	case SelectionRole::ResolutionTarget:
	case SelectionRole::ResolutionMaterial:
		return BindingStage::Resolution;
	}
	return std::nullopt;
}

} // namespace

DeckTestError Validate(const DeckTestContext& context) {
	if(context.session.value == 0 || context.branch.value == 0)
		return DeckTestError::MissingContextIdentity;
	return DeckTestError::None;
}

DeckTestError Validate(const CardReference& card) {
	if(card.instance.value == 0)
		return DeckTestError::MissingInstance;
	if(card.code == 0)
		return DeckTestError::MissingCardCode;
	if(card.owner > 1 || (card.location && card.location->controller > 1))
		return DeckTestError::InvalidOwner;
	if(card.kind == CardReferenceKind::Existing) {
		if(card.source != CardSource::ExistingState || !card.location)
			return DeckTestError::ContradictoryReference;
		return DeckTestError::None;
	}
	if(card.kind == CardReferenceKind::NewCopy) {
		if(!IsNewCopySource(card.source) || card.location)
			return DeckTestError::ContradictoryReference;
		return DeckTestError::None;
	}
	return DeckTestError::ContradictoryReference;
}

DeckTestError Validate(const CardReference& card, const DeckTestContext& context) {
	if(const auto contextError = Validate(context); contextError != DeckTestError::None)
		return contextError;
	if(const auto cardError = Validate(card); cardError != DeckTestError::None)
		return cardError;
	if(card.kind == CardReferenceKind::NewCopy && card.owner != context.checkpoint.player)
		return DeckTestError::IneligibleSource;
	return DeckTestError::None;
}

DeckTestError Validate(const CardAvailability& availability, const DeckTestContext& context) {
	if(const auto contextError = Validate(context); contextError != DeckTestError::None)
		return contextError;
	if(availability.code == 0)
		return DeckTestError::MissingCardCode;
	if(availability.owner > 1)
		return DeckTestError::InvalidOwner;
	if(!IsNewCopySource(availability.source) || availability.owner != context.checkpoint.player)
		return DeckTestError::IneligibleSource;
	if(availability.quantity == 0)
		return DeckTestError::InvalidQuantity;
	return DeckTestError::None;
}

DeckTestError Validate(const SelectionPlan& plan) {
	if(const auto contextError = Validate(plan.context); contextError != DeckTestError::None)
		return contextError;
	if(plan.cards.empty())
		return DeckTestError::IncompletePlan;
	const auto requiredStage = RequiredStage(plan.role);
	if(!requiredStage || plan.bindingStage != *requiredStage)
		return DeckTestError::WrongBindingStage;
	std::unordered_set<std::uint64_t> instances;
	for(const auto& card : plan.cards) {
		if(const auto cardError = Validate(card, plan.context); cardError != DeckTestError::None)
			return cardError;
		if(!instances.insert(card.instance.value).second)
			return DeckTestError::DuplicateInstance;
	}
	return DeckTestError::None;
}

DeckTestError Validate(const CardIntroducedEvent& event) {
	if(const auto cardError = Validate(event.card, event.context); cardError != DeckTestError::None)
		return cardError;
	if(event.card.kind != CardReferenceKind::NewCopy)
		return DeckTestError::ContradictoryReference;
	const auto requiredStage = RequiredStage(event.role);
	if(!requiredStage || event.bindingStage != *requiredStage)
		return DeckTestError::WrongBindingStage;
	return DeckTestError::None;
}

} // namespace undo
