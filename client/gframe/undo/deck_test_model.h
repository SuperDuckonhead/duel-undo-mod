#pragma once

#include "duel_history.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace undo {

struct DeckTestSessionId {
	std::uint64_t value{};
};

struct DeckTestBranchId {
	std::uint64_t value{};
};

struct StableInstanceId {
	std::uint64_t value{};
};

constexpr bool operator==(StableInstanceId left, StableInstanceId right) {
	return left.value == right.value;
}

constexpr bool operator!=(StableInstanceId left, StableInstanceId right) {
	return !(left == right);
}

struct DeckTestContext {
	DeckTestSessionId session;
	DeckTestBranchId branch;
	std::size_t historyCursor{};
	Checkpoint checkpoint;
	Digest resourceVersion{};
	std::uint64_t modeGeneration{};
};

enum class CardReferenceKind : std::uint8_t {
	Existing,
	NewCopy,
};

enum class CardSource : std::uint8_t {
	ExistingState,
	OwnMainDeck,
	OwnFacedownExtraDeck,
};

struct CardLocation {
	std::uint8_t controller{};
	std::uint32_t zone{};
	std::uint32_t sequence{};
	std::uint32_t position{};
};

struct CardReference {
	CardReferenceKind kind{CardReferenceKind::Existing};
	StableInstanceId instance;
	std::uint32_t code{};
	std::uint8_t owner{};
	CardSource source{CardSource::ExistingState};
	std::optional<CardLocation> location;
};

// Availability describes how many prospective copies a query may offer.
// A plan still names every selected copy with an independent instance ID.
struct CardAvailability {
	std::uint32_t code{};
	std::uint8_t owner{};
	CardSource source{CardSource::OwnMainDeck};
	std::uint32_t quantity{};
};

enum class SelectionRole : std::uint8_t {
	ActivationCost,
	ActivationTarget,
	ResolutionTarget,
	ResolutionMaterial,
};

enum class BindingStage : std::uint8_t {
	Activation,
	Resolution,
};

struct SelectionPlan {
	DeckTestContext context;
	SelectionRole role{SelectionRole::ActivationCost};
	BindingStage bindingStage{BindingStage::Activation};
	std::vector<CardReference> cards;
};

enum class DeckTestMode : std::uint8_t {
	Disabled,
	Browsing,
	Editing,
};

struct ModeChangedEvent {
	DeckTestContext context;
	DeckTestMode mode{DeckTestMode::Disabled};
};

struct CardIntroducedEvent {
	DeckTestContext context;
	CardReference card;
	SelectionRole role{SelectionRole::ActivationCost};
	BindingStage bindingStage{BindingStage::Activation};
};

struct ResponseAcceptedEvent {
	DeckTestContext context;
	ResponseRecord response;
};

using DeckTestEvent = std::variant<ModeChangedEvent, CardIntroducedEvent, ResponseAcceptedEvent>;

enum class DeckTestError : std::uint8_t {
	None,
	MissingContextIdentity,
	MissingInstance,
	MissingCardCode,
	InvalidOwner,
	ContradictoryReference,
	IneligibleSource,
	InvalidQuantity,
	IncompletePlan,
	DuplicateInstance,
	WrongBindingStage,
};

DeckTestError Validate(const DeckTestContext& context);
DeckTestError Validate(const CardReference& card);
DeckTestError Validate(const CardReference& card, const DeckTestContext& context);
DeckTestError Validate(const CardAvailability& availability, const DeckTestContext& context);
DeckTestError Validate(const SelectionPlan& plan);
DeckTestError Validate(const CardIntroducedEvent& event);

} // namespace undo
