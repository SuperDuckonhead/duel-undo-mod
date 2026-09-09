#include "client_field.h"
#include "client_card.h"
#include "game.h"
#include "materials.h"
#include <algorithm>
#include <stdexcept>
namespace ygo {
ClientField::ClientField() {
	for(int p = 0; p < 2; ++p) {
		mzone[p].resize(7, 0);
		szone[p].resize(8, 0);
	}
}
ClientField::~ClientField() = default;
void ClientField::SwapPreparedModel(ClientField& other) noexcept {
	using std::swap;
	swap(deck, other.deck);
	swap(hand, other.hand);
	swap(mzone, other.mzone);
	swap(szone, other.szone);
	swap(grave, other.grave);
	swap(remove, other.remove);
	swap(extra, other.extra);
	swap(overlay_cards, other.overlay_cards);
	swap(summonable_cards, other.summonable_cards);
	swap(spsummonable_cards, other.spsummonable_cards);
	swap(msetable_cards, other.msetable_cards);
	swap(ssetable_cards, other.ssetable_cards);
	swap(reposable_cards, other.reposable_cards);
	swap(activatable_cards, other.activatable_cards);
	swap(attackable_cards, other.attackable_cards);
	swap(conti_cards, other.conti_cards);
	swap(activatable_descs, other.activatable_descs);
	swap(select_options, other.select_options);
	swap(select_options_index, other.select_options_index);
	swap(chains, other.chains);
	swap(extra_p_count, other.extra_p_count);
	swap(selected_option, other.selected_option);
	swap(attacker, other.attacker);
	swap(attack_target, other.attack_target);
	swap(disabled_field, other.disabled_field);
	swap(selectable_field, other.selectable_field);
	swap(selected_field, other.selected_field);
	swap(select_min, other.select_min);
	swap(select_max, other.select_max);
	swap(must_select_count, other.must_select_count);
	swap(select_curval_l, other.select_curval_l);
	swap(select_curval_h, other.select_curval_h);
	swap(select_sumval, other.select_sumval);
	swap(select_mode, other.select_mode);
	swap(select_hint, other.select_hint);
	swap(select_cancelable, other.select_cancelable);
	swap(select_panalmode, other.select_panalmode);
	swap(select_ready, other.select_ready);
	swap(announce_count, other.announce_count);
	swap(select_counter_count, other.select_counter_count);
	swap(select_counter_type, other.select_counter_type);
	swap(selectable_cards, other.selectable_cards);
	swap(selected_cards, other.selected_cards);
	swap(selectsum_cards, other.selectsum_cards);
	swap(selectsum_all, other.selectsum_all);
	swap(declare_opcodes, other.declare_opcodes);
	swap(display_cards, other.display_cards);
	swap(sort_list, other.sort_list);
	swap(player_desc_hints, other.player_desc_hints);
	swap(grave_act, other.grave_act);
	swap(remove_act, other.remove_act);
	swap(deck_act, other.deck_act);
	swap(extra_act, other.extra_act);
	swap(pzone_act, other.pzone_act);
	swap(conti_act, other.conti_act);
	swap(chain_forced, other.chain_forced);
	swap(current_chain, other.current_chain);
	swap(last_chain, other.last_chain);
	swap(deck_reversed, other.deck_reversed);
	swap(select_continuous, other.select_continuous);
	swap(cant_check_grave, other.cant_check_grave);
	swap(tag_surrender, other.tag_surrender);
	swap(tag_teammate_surrender, other.tag_teammate_surrender);
	swap(panel, other.panel);
	swap(ancard, other.ancard);
	swap(hovered_controler, other.hovered_controler);
	swap(hovered_location, other.hovered_location);
	swap(hovered_sequence, other.hovered_sequence);
	swap(command_controler, other.command_controler);
	swap(command_location, other.command_location);
	swap(command_sequence, other.command_sequence);
	swap(hovered_card, other.hovered_card);
	swap(hovered_player, other.hovered_player);
	swap(clicked_card, other.clicked_card);
	swap(command_card, other.command_card);
	swap(highlighting_card, other.highlighting_card);
	swap(menu_card, other.menu_card);
	swap(list_command, other.list_command);
	swap(cards_, other.cards_);
	for(auto& card : cards_) card->field_ = this;
	for(auto& card : other.cards_) card->field_ = &other;
}
void ClientField::PrepareModelGeometry(int duel_rule) {
	for(auto& card : cards_) GetCardLocation(card.get(), &card->curPos, &card->curRot, true, duel_rule);
	auto place = [&](ChainInfo& chain, std::size_t prior_count) {
		GetChainLocation(chain.controler, chain.location, chain.sequence, &chain.chain_pos, duel_rule);
		if(chain.location == LOCATION_HAND) chain.chain_pos.X += 0.35f;
		else {
			unsigned overlap = 0;
			for(std::size_t i = 0; i < prior_count; ++i) {
				const auto& prior = chains[i];
				if(prior.controler == chain.controler && prior.location == chain.location &&
				   ((chain.location & (LOCATION_GRAVE | LOCATION_REMOVED)) || prior.sequence == chain.sequence)) ++overlap;
			}
			chain.chain_pos.Y += overlap * 0.25f;
		}
	};
	for(std::size_t i = 0; i < chains.size(); ++i) place(chains[i], i);
	if(current_chain.chain_card) {
		if(!chains.empty() && current_chain.chain_card == chains.back().chain_card && current_chain.desc == chains.back().desc)
			current_chain.chain_pos = chains.back().chain_pos;
		else place(current_chain, chains.size());
	}
}
ClientCard* ClientField::CreateCard() {
	cards_.emplace_back(std::make_unique<ClientCard>(this));
	return cards_.back().get();
}
void ClientField::DestroyCard(ClientCard* pcard) {
	if (!pcard)
		return;
	overlay_cards.erase(pcard);
	auto it = std::find_if(cards_.begin(), cards_.end(),
		[pcard](const std::unique_ptr<ClientCard>& ptr) {
			return ptr.get() == pcard;
		});
	if (it != cards_.end()) {
		std::swap(*it, cards_.back());
		cards_.pop_back();
	}
}
ClientCard* ClientField::GetCard(int controler, int location, int sequence, int sub_seq) {
	std::vector<ClientCard*>* lst = 0;
	bool is_xyz = (location & LOCATION_OVERLAY) != 0;
	location &= 0x7f;
	switch(location) {
	case LOCATION_DECK:
		lst = &deck[controler];
		break;
	case LOCATION_HAND:
		lst = &hand[controler];
		break;
	case LOCATION_MZONE:
		lst = &mzone[controler];
		break;
	case LOCATION_SZONE:
		lst = &szone[controler];
		break;
	case LOCATION_GRAVE:
		lst = &grave[controler];
		break;
	case LOCATION_REMOVED:
		lst = &remove[controler];
		break;
	case LOCATION_EXTRA:
		lst = &extra[controler];
		break;
	}
	if(!lst)
		return 0;
	if(is_xyz) {
		if(sequence >= (int)lst->size())
			return 0;
		ClientCard* scard = (*lst)[sequence];
		if(scard && (int)scard->overlayed.size() > sub_seq)
			return scard->overlayed[sub_seq];
		else
			return 0;
	} else {
		if(sequence >= (int)lst->size())
			return 0;
		return (*lst)[sequence];
	}
}
void ClientField::GetChainLocation(int controler, int location, int sequence, irr::core::vector3df* t, int duel_rule) {
	t->X = 0;
	t->Y = 0;
	t->Z = 0;
	int rule = ((duel_rule < 0 ? mainGame->dInfo.duel_rule : duel_rule) >= 4) ? 1 : 0;
	switch((location & 0x7f)) {
	case LOCATION_DECK: {
		t->X = (matManager.vFieldDeck[controler][0].Pos.X + matManager.vFieldDeck[controler][1].Pos.X) / 2;
		t->Y = (matManager.vFieldDeck[controler][0].Pos.Y + matManager.vFieldDeck[controler][2].Pos.Y) / 2;
		t->Z = deck[controler].size() * 0.01f + 0.03f;
		break;
	}
	case LOCATION_HAND: {
		if (controler == 0) {
			t->X = 2.95f;
			t->Y = 3.15f;
			t->Z = 0.03f;
		} else {
			t->X = 2.95f;
			t->Y = -3.15f;
			t->Z = 0.03f;
		}
		break;
	}
	case LOCATION_MZONE: {
		t->X = (matManager.vFieldMzone[controler][sequence][0].Pos.X + matManager.vFieldMzone[controler][sequence][1].Pos.X) / 2;
		t->Y = (matManager.vFieldMzone[controler][sequence][0].Pos.Y + matManager.vFieldMzone[controler][sequence][2].Pos.Y) / 2;
		t->Z = 0.03f;
		break;
	}
	case LOCATION_SZONE: {
		t->X = (matManager.vFieldSzone[controler][sequence][rule][0].Pos.X + matManager.vFieldSzone[controler][sequence][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldSzone[controler][sequence][rule][0].Pos.Y + matManager.vFieldSzone[controler][sequence][rule][2].Pos.Y) / 2;
		t->Z = 0.03f;
		break;
	}
	case LOCATION_GRAVE: {
		t->X = (matManager.vFieldGrave[controler][rule][0].Pos.X + matManager.vFieldGrave[controler][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldGrave[controler][rule][0].Pos.Y + matManager.vFieldGrave[controler][rule][2].Pos.Y) / 2;
		t->Z = grave[controler].size() * 0.01f + 0.03f;
		break;
	}
	case LOCATION_REMOVED: {
		t->X = (matManager.vFieldRemove[controler][rule][0].Pos.X + matManager.vFieldRemove[controler][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldRemove[controler][rule][0].Pos.Y + matManager.vFieldRemove[controler][rule][2].Pos.Y) / 2;
		t->Z = remove[controler].size() * 0.01f + 0.03f;
		break;
	}
	case LOCATION_EXTRA: {
		t->X = (matManager.vFieldExtra[controler][0].Pos.X + matManager.vFieldExtra[controler][1].Pos.X) / 2;
		t->Y = (matManager.vFieldExtra[controler][0].Pos.Y + matManager.vFieldExtra[controler][2].Pos.Y) / 2;
		t->Z = extra[controler].size() * 0.01f + 0.03f;
		break;
	}
	}
}
void ClientField::GetCardLocation(ClientCard* pcard, irr::core::vector3df* t, irr::core::vector3df* r, bool setTrans, int duel_rule) {
	int controler = pcard->controler;
	int sequence = pcard->sequence;
	int location = pcard->location;
	int rule = ((duel_rule < 0 ? mainGame->dInfo.duel_rule : duel_rule) >= 4) ? 1 : 0;
	const float overlay_buttom = 0.0f;
	const float material_height = 0.003f;
	const float mzone_buttom = 0.020f;
	switch (location) {
	case LOCATION_DECK: {
		t->X = (matManager.vFieldDeck[controler][0].Pos.X + matManager.vFieldDeck[controler][1].Pos.X) / 2;
		t->Y = (matManager.vFieldDeck[controler][0].Pos.Y + matManager.vFieldDeck[controler][2].Pos.Y) / 2;
		t->Z = 0.01f + 0.01f * sequence;
		if (controler == 0) {
			if(deck_reversed == pcard->is_reversed) {
				r->X = 0.0f;
				r->Y = 3.1415926f;
				r->Z = 0.0f;
			} else {
				r->X = 0.0f;
				r->Y = 0.0f;
				r->Z = 0.0f;
			}
		} else {
			if(deck_reversed == pcard->is_reversed) {
				r->X = 0.0f;
				r->Y = 3.1415926f;
				r->Z = 3.1415926f;
			} else {
				r->X = 0.0f;
				r->Y = 0.0f;
				r->Z = 3.1415926f;
			}
		}
		break;
	}
	case 0:
	case LOCATION_HAND: {
		int count = hand[controler].size();
		if (controler == 0) {
			if (count <= 6)
				t->X = (5.5f - 0.8f * count) / 2 + 1.55f + sequence * 0.8f;
			else
				t->X = 1.9f + sequence * 4.0f / (count - 1);
			if (pcard->is_hovered) {
				t->Y = 3.84f;
				t->Z = 0.656f + 0.001f * sequence;
			} else {
				t->Y = 4.0f;
				t->Z = 0.5f + 0.001f * sequence;
			}
			if(pcard->code) {
				r->X = -0.798056f;
				r->Y = 0.0f;
				r->Z = 0.0f;
			} else {
				r->X = 0.798056f;
				r->Y = 3.1415926f;
				r->Z = 0;
			}
		} else {
			if (count <= 6)
				t->X = 6.25f - (5.5f - 0.8f * count) / 2 - sequence * 0.8f;
			else
				t->X = 5.9f - sequence * 4.0f / (count - 1);
			if (pcard->is_hovered) {
				t->Y = -3.56f;
				t->Z = 0.656f - 0.001f * sequence;
			} else {
				t->Y = -3.4f;
				t->Z = 0.5f - 0.001f * sequence;
			}
			if (pcard->code == 0) {
				r->X = 0.798056f;
				r->Y = 3.1415926f;
				r->Z = 0;
			} else {
				r->X = -0.798056f;
				r->Y = 0;
				r->Z = 0;
			}
		}
		break;
	}
	case LOCATION_MZONE: {
		t->X = (matManager.vFieldMzone[controler][sequence][0].Pos.X + matManager.vFieldMzone[controler][sequence][1].Pos.X) / 2;
		t->Y = (matManager.vFieldMzone[controler][sequence][0].Pos.Y + matManager.vFieldMzone[controler][sequence][2].Pos.Y) / 2;
		t->Z = mzone_buttom;
		if (controler == 0) {
			if (pcard->position & POS_DEFENSE) {
				r->X = 0.0f;
				r->Z = -3.1415926f / 2.0f;
				if (pcard->position & POS_FACEDOWN)
					r->Y = 3.1415926f + 0.001f;
				else r->Y = 0.0f;
			} else {
				r->X = 0.0f;
				r->Z = 0.0f;
				if (pcard->position & POS_FACEDOWN)
					r->Y = 3.1415926f;
				else r->Y = 0.0f;
			}
		} else {
			if (pcard->position & POS_DEFENSE) {
				r->X = 0.0f;
				r->Z = 3.1415926f / 2.0f;
				if (pcard->position & POS_FACEDOWN)
					r->Y = 3.1415926f + 0.001f;
				else r->Y = 0.0f;
			} else {
				r->X = 0.0f;
				r->Z = 3.1415926f;
				if (pcard->position & POS_FACEDOWN)
					r->Y = 3.1415926f;
				else r->Y = 0.0f;
			}
		}
		break;
	}
	case LOCATION_SZONE: {
		t->X = (matManager.vFieldSzone[controler][sequence][rule][0].Pos.X + matManager.vFieldSzone[controler][sequence][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldSzone[controler][sequence][rule][0].Pos.Y + matManager.vFieldSzone[controler][sequence][rule][2].Pos.Y) / 2;
		t->Z = 0.01f;
		if (controler == 0) {
			r->X = 0.0f;
			r->Z = 0.0f;
			if (pcard->position & POS_FACEDOWN)
				r->Y = 3.1415926f;
			else r->Y = 0.0f;
		} else {
			r->X = 0.0f;
			r->Z = 3.1415926f;
			if (pcard->position & POS_FACEDOWN)
				r->Y = 3.1415926f;
			else r->Y = 0.0f;
		}
		break;
	}
	case LOCATION_GRAVE: {
		t->X = (matManager.vFieldGrave[controler][rule][0].Pos.X + matManager.vFieldGrave[controler][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldGrave[controler][rule][0].Pos.Y + matManager.vFieldGrave[controler][rule][2].Pos.Y) / 2;
		t->Z = 0.01f + 0.01f * sequence;
		if (controler == 0) {
			r->X = 0.0f;
			r->Y = 0.0f;
			r->Z = 0.0f;
		} else {
			r->X = 0.0f;
			r->Y = 0.0f;
			r->Z = 3.1415926f;
		}
		break;
	}
	case LOCATION_REMOVED: {
		t->X = (matManager.vFieldRemove[controler][rule][0].Pos.X + matManager.vFieldRemove[controler][rule][1].Pos.X) / 2;
		t->Y = (matManager.vFieldRemove[controler][rule][0].Pos.Y + matManager.vFieldRemove[controler][rule][2].Pos.Y) / 2;
		t->Z = 0.01f + 0.01f * sequence;
		if (controler == 0) {
			if(pcard->position & POS_FACEUP) {
				r->X = 0.0f;
				r->Y = 0.0f;
				r->Z = 0.0f;
			} else {
				r->X = 0.0f;
				r->Y = 3.1415926f;
				r->Z = 0.0f;
			}
		} else {
			if(pcard->position & POS_FACEUP) {
				r->X = 0.0f;
				r->Y = 0.0f;
				r->Z = 3.1415926f;
			} else {
				r->X = 0.0f;
				r->Y = 3.1415926f;
				r->Z = 3.1415926f;
			}
		}
		break;
	}
	case LOCATION_EXTRA: {
		t->X = (matManager.vFieldExtra[controler][0].Pos.X + matManager.vFieldExtra[controler][1].Pos.X) / 2;
		t->Y = (matManager.vFieldExtra[controler][0].Pos.Y + matManager.vFieldExtra[controler][2].Pos.Y) / 2;
		t->Z = 0.01f + 0.01f * sequence;
		if (controler == 0) {
			r->X = 0.0f;
			if(pcard->position & POS_FACEUP)
				r->Y = 0.0f;
			else r->Y = 3.1415926f;
			r->Z = 0.0f;
		} else {
			r->X = 0.0f;
			if(pcard->position & POS_FACEUP)
				r->Y = 0.0f;
			else r->Y = 3.1415926f;
			r->Z = 3.1415926f;
		}
		break;
	}
	case LOCATION_OVERLAY: {
		if (pcard->overlayTarget->location != LOCATION_MZONE) {
			return;
		}
		int oseq = pcard->overlayTarget->sequence;
		int mseq = myclamp(sequence, 0, MAX_LAYER_COUNT - 1);
		if (pcard->overlayTarget->controler == 0) {
			t->X = (matManager.vFieldMzone[0][oseq][0].Pos.X + matManager.vFieldMzone[0][oseq][1].Pos.X) / 2 - 0.12f + 0.06f * mseq;
			t->Y = (matManager.vFieldMzone[0][oseq][0].Pos.Y + matManager.vFieldMzone[0][oseq][2].Pos.Y) / 2 + 0.05f;
			t->Z = overlay_buttom + mseq * material_height;
			r->X = 0.0f;
			r->Y = 0.0f;
			r->Z = 0.0f;
		} else {
			t->X = (matManager.vFieldMzone[1][oseq][0].Pos.X + matManager.vFieldMzone[1][oseq][1].Pos.X) / 2 + 0.12f - 0.06f * mseq;
			t->Y = (matManager.vFieldMzone[1][oseq][0].Pos.Y + matManager.vFieldMzone[1][oseq][2].Pos.Y) / 2 - 0.05f;
			t->Z = overlay_buttom + mseq * material_height;
			r->X = 0.0f;
			r->Y = 0.0f;
			r->Z = 3.1415926f;
		}
		break;
	}
	}
	if(setTrans) {
		pcard->mTransform.setTranslation(*t);
		pcard->mTransform.setRotationRadians(*r);
	}
}
bool ClientField::CheckSelectSum() {
	ConsumeModelWork();
	std::set<ClientCard*> selable;
	for(auto sc : selectsum_all) {
		sc->is_selectable = false;
		sc->is_selected = false;
		selable.insert(sc);
	}
	select_curval_l = 0;
	select_curval_h = 0;
	for(int i = 0; i < (int)selected_cards.size(); ++i) {
		if(i < must_select_count)
			selected_cards[i]->is_selectable = false;
		else
			selected_cards[i]->is_selectable = true;
		selected_cards[i]->is_selected = true;
		selable.erase(selected_cards[i]);

		int op1 = selected_cards[i]->opParam & 0xffff;
		int op2 = selected_cards[i]->opParam >> 16;
		int opmin = (op2 > 0 && op1 > op2) ? op2 : op1;
		int opmax = std::max(op1, op2);
		select_curval_l += opmin;
		select_curval_h += opmax;
	}
	selectsum_cards.clear();
	if (select_mode == 0) { // sum equal
		bool ret = check_sel_sum_s(selable, 0, select_sumval);
		selectable_cards.clear();
		for(auto sc : selectsum_cards) {
			sc->is_selectable = true;
			selectable_cards.push_back(sc);
		}
		for(auto sc : selected_cards) {
			selectable_cards.push_back(sc);
		}
		return ret;
	} else { // sum greater
		int mm = -1, mx = -1, max = 0, sumc = 0;
		bool ret = false;
		for (auto sc : selected_cards) {
			int op1, op2;
			get_sum_params(sc->opParam, op1, op2);
			int opmin = (op2 > 0 && op1 > op2) ? op2 : op1;
			int opmax = std::max(op1, op2);
			if (mm == -1 || opmin < mm)
				mm = opmin;
			if (mx == -1 || opmax < mx)
				mx = opmax;
			sumc += opmin;
			max += opmax;
		}
		if (select_sumval <= sumc)
			return true;
		if (select_sumval <= max && select_sumval > max - mx)
			ret = true;
		for(auto sc : selable) {
			int op1, op2;
			get_sum_params(sc->opParam, op1, op2);
			int m = op1;
			int sums = sumc;
			sums += m;
			int ms = mm;
			if (ms == -1 || m < ms)
				ms = m;
			if (sums >= select_sumval) {
				if (sums - ms < select_sumval)
					selectsum_cards.insert(sc);
			} else {
				std::set<ClientCard*> left(selable);
				left.erase(sc);
				if (check_min(left, left.begin(), select_sumval - sums, select_sumval - sums + ms - 1))
					selectsum_cards.insert(sc);
			}
			if (op2 == 0)
				continue;
			m = op2;
			sums = sumc;
			sums += m;
			ms = mm;
			if (ms == -1 || m < ms)
				ms = m;
			if (sums >= select_sumval) {
				if (sums - ms < select_sumval)
					selectsum_cards.insert(sc);
			} else {
				std::set<ClientCard*> left(selable);
				left.erase(sc);
				if (check_min(left, left.begin(), select_sumval - sums, select_sumval - sums + ms - 1))
					selectsum_cards.insert(sc);
			}
		}
		selectable_cards.clear();
		for(auto sc : selectsum_cards) {
			sc->is_selectable = true;
			selectable_cards.push_back(sc);
		}
		for(auto sc : selected_cards) {
			selectable_cards.push_back(sc);
		}
		return ret;
	}
}
bool ClientField::CheckSelectTribute() {
	ConsumeModelWork();
	std::set<ClientCard*> selable;
	for(auto sit = selectsum_all.begin(); sit != selectsum_all.end(); ++sit) {
		(*sit)->is_selectable = false;
		(*sit)->is_selected = false;
		selable.insert(*sit);
	}
	for(int i = 0; i < (int)selected_cards.size(); ++i) {
		selected_cards[i]->is_selectable = true;
		selected_cards[i]->is_selected = true;
		selable.erase(selected_cards[i]);
	}
	selectsum_cards.clear();
	bool ret = check_sel_sum_trib_s(selable, 0, 0);
	selectable_cards.clear();
	for(auto sit = selectsum_cards.begin(); sit != selectsum_cards.end(); ++sit) {
		(*sit)->is_selectable = true;
		selectable_cards.push_back(*sit);
	}
	return ret;
}
void ClientField::get_sum_params(unsigned int opParam, int& op1, int& op2) {
	op1 = opParam & 0xffff;
	op2 = (opParam >> 16) & 0xffff;
	if (op2 & 0x8000) {
		op1 = opParam & 0x7fffffff;
		op2 = 0;
	}
}
bool ClientField::check_min(const std::set<ClientCard*>& left, std::set<ClientCard*>::const_iterator index, int min, int max) {
	ConsumeModelWork();
	if (index == left.end())
		return false;
	int op1, op2;
	get_sum_params((*index)->opParam, op1, op2);
	int m = (op2 > 0 && op1 > op2) ? op2 : op1;
	if (m >= min && m <= max)
		return true;
	++index;
	return (min > m && check_min(left, index, min - m, max - m))
	        || check_min(left, index, min, max);
}
bool ClientField::check_sel_sum_s(const std::set<ClientCard*>& left, int index, int acc) {
	ConsumeModelWork();
	if (acc < 0)
		return false;
	if (index == (int)selected_cards.size()) {
		if (acc == 0) {
			int count = selected_cards.size() - must_select_count;
			return count >= select_min && count <= select_max;
		}
		check_sel_sum_t(left, acc);
		return false;
	}
	int l1, l2;
	get_sum_params(selected_cards[index]->opParam, l1, l2);
	bool res1 = false, res2 = false;
	res1 = check_sel_sum_s(left, index + 1, acc - l1);
	if (l2 > 0)
		res2 = check_sel_sum_s(left, index + 1, acc - l2);
	return res1 || res2;
}
void ClientField::check_sel_sum_t(const std::set<ClientCard*>& left, int acc) {
	ConsumeModelWork();
	int count = selected_cards.size() + 1 - must_select_count;
	for (auto sit = left.begin(); sit != left.end(); ++sit) {
		if (selectsum_cards.find(*sit) != selectsum_cards.end())
			continue;
		std::set<ClientCard*> testlist(left);
		testlist.erase(*sit);
		int l1, l2;
		get_sum_params((*sit)->opParam, l1, l2);
		if (check_sum(testlist.begin(), testlist.end(), acc - l1, count)
		        || (l2 > 0 && check_sum(testlist.begin(), testlist.end(), acc - l2, count))) {
			selectsum_cards.insert(*sit);
		}
	}
}
bool ClientField::check_sum(std::set<ClientCard*>::const_iterator index, std::set<ClientCard*>::const_iterator end, int acc, int count) {
	ConsumeModelWork();
	if (acc == 0)
		return count >= select_min && count <= select_max;
	if (acc < 0 || index == end)
		return false;
	int l1, l2;
	get_sum_params((*index)->opParam, l1, l2);
	if ((l1 == acc || (l2 > 0 && l2 == acc)) && (count + 1 >= select_min) && (count + 1 <= select_max))
		return true;
	++index;
	return (acc > l1 && check_sum(index, end, acc - l1, count + 1))
	       || (l2 > 0 && acc > l2 && check_sum(index, end, acc - l2, count + 1))
	       || check_sum(index, end, acc, count);
}
bool ClientField::check_sel_sum_trib_s(const std::set<ClientCard*>& left, int index, int acc) {
	ConsumeModelWork();
	if(acc > select_max)
		return false;
	if(index == (int)selected_cards.size()) {
		check_sel_sum_trib_t(left, acc);
		return acc >= select_min && acc <= select_max;
	}
	int l1, l2;
	get_sum_params(selected_cards[index]->opParam, l1, l2);
	bool res1 = false, res2 = false;
	res1 = check_sel_sum_trib_s(left, index + 1, acc + l1);
	if(l2 > 0)
		res2 = check_sel_sum_trib_s(left, index + 1, acc + l2);
	return res1 || res2;
}
void ClientField::check_sel_sum_trib_t(const std::set<ClientCard*>& left, int acc) {
	ConsumeModelWork();
	for(auto sit = left.begin(); sit != left.end(); ++sit) {
		if(selectsum_cards.find(*sit) != selectsum_cards.end())
			continue;
		std::set<ClientCard*> testlist(left);
		testlist.erase(*sit);
		int l1, l2;
		get_sum_params((*sit)->opParam, l1, l2);
		if(check_sum_trib(testlist.begin(), testlist.end(), acc + l1)
			|| (l2 > 0 && check_sum_trib(testlist.begin(), testlist.end(), acc + l2))) {
			selectsum_cards.insert(*sit);
		}
	}
}
bool ClientField::check_sum_trib(std::set<ClientCard*>::const_iterator index, std::set<ClientCard*>::const_iterator end, int acc) {
	ConsumeModelWork();
	if(acc >= select_min && acc <= select_max)
		return true;
	if(acc > select_max || index == end)
		return false;
	int l1, l2;
	get_sum_params((*index)->opParam, l1, l2);
	if((acc + l1 >= select_min && acc + l1 <= select_max) || (l2 > 0 && acc + l2 >= select_min && acc + l2 <= select_max))
		return true;
	++index;
	return check_sum_trib(index, end, acc + l1)
		|| (l2 > 0 && check_sum_trib(index, end, acc + l2))
		|| check_sum_trib(index, end, acc);
}
void ClientField::ConsumeModelWork() {
 if(model_work_budget_ == static_cast<std::size_t>(-1)) return;
 if(!model_work_budget_--) throw std::runtime_error("visible selection work limit");
}
void ClientField::PrepareSelectSum(bool tribute) {
 model_work_budget_ = 1000000;
 try { select_ready = tribute ? CheckSelectTribute() : CheckSelectSum(); }
 catch(...) { model_work_budget_ = static_cast<std::size_t>(-1); throw; }
 model_work_budget_ = static_cast<std::size_t>(-1);
}
}
