#include "client_card.h"
#include "client_field.h"
#include <algorithm>
namespace ygo {
ClientCard::ClientCard(ClientField* field) : field_(field) {
}
ClientCard::~ClientCard() {
	ClearTarget();
	if (equipTarget) {
		equipTarget->is_showequip = false;
		equipTarget->equipped.erase(this);
		equipTarget = nullptr;
	}
	for (auto& card : equipped) {
		card->is_showequip = false;
		card->equipTarget = nullptr;
	}
	equipped.clear();
	if (overlayTarget) {
		for (auto it = overlayTarget->overlayed.begin(); it != overlayTarget->overlayed.end(); ) {
			if (*it == this) {
				it = overlayTarget->overlayed.erase(it);
			}
			else
				++it;
		}
		overlayTarget = nullptr;
	}
	for (auto& card : overlayed) {
		card->overlayTarget = nullptr;
	}
	overlayed.clear();
}
void ClientCard::ClearTarget() {
	for (auto& pcard : cardTarget) {
		pcard->is_showtarget = false;
		pcard->ownerTarget.erase(this);
	}
	for (auto& pcard : ownerTarget) {
		pcard->is_showtarget = false;
		pcard->cardTarget.erase(this);
	}
	cardTarget.clear();
	ownerTarget.clear();
}
void ClientCard::ClearData() {
	alias = 0;
	type = 0;
	level = 0;
	rank = 0;
	race = 0;
	attribute = 0;
	attack = 0;
	defense = 0;
	base_attack = 0;
	base_defense = 0;
	lscale = 0;
	rscale = 0;
	link = 0;
	link_marker = 0;
	status = 0;

	atkstring[0] = 0;
	defstring[0] = 0;
	lvstring[0] = 0;
	linkstring[0] = 0;
	rscstring[0] = 0;
	lscstring[0] = 0;
	counters.clear();
}
bool ClientCard::client_card_sort(ClientCard* c1, ClientCard* c2) {
	if(c1->is_selected != c2->is_selected)
		return c1->is_selected < c2->is_selected;
	int cp1 = c1->overlayTarget ? c1->overlayTarget->controler : c1->controler;
	int cp2 = c2->overlayTarget ? c2->overlayTarget->controler : c2->controler;
	if(cp1 != cp2)
		return cp1 < cp2;
	if(c1->location != c2->location)
		return c1->location < c2->location;
	if (c1->location == LOCATION_OVERLAY) {
		if (c1->overlayTarget != c2->overlayTarget)
			return c1->overlayTarget->sequence < c2->overlayTarget->sequence;
		else
			return c1->sequence < c2->sequence;
	}
	else if (c1->location == LOCATION_DECK) {
		return c1->sequence > c2->sequence;
	}
	else if (c1->location & (LOCATION_GRAVE | LOCATION_REMOVED | LOCATION_EXTRA)) {
		auto it1 = std::find_if(c1->field_->chains.rbegin(), c1->field_->chains.rend(), [c1](const ChainInfo& ch) {
			return c1 == ch.chain_card || ch.target.find(c1) != ch.target.end();
		});
		auto it2 = std::find_if(c1->field_->chains.rbegin(), c1->field_->chains.rend(), [c2](const ChainInfo& ch) {
			return c2 == ch.chain_card || ch.target.find(c2) != ch.target.end();
		});
		if (it1 != c1->field_->chains.rend() || it2 != c1->field_->chains.rend()) {
			return it1 < it2;
		}
		return c1->sequence > c2->sequence;
	}
	else {
		return c1->sequence < c2->sequence;
	}
}
}
