#include <algorithm>
#include <random>
#include <stack>
#include "client_field.h"
#include "client_card.h"
#include "duelclient.h"
#include "data_manager.h"
#include "image_manager.h"
#include "game.h"
#include "materials.h"

namespace ygo {

void ClientField::Clear() {
	for(int i = 0; i < 2; ++i) {
		deck[i].clear();
		hand[i].clear();
		for (auto& card : mzone[i]) {
			card = nullptr;
		}
		for (auto& card : szone[i]) {
			card = nullptr;
		}
		grave[i].clear();
		remove[i].clear();
		extra[i].clear();
		deck_act[i] = false;
		grave_act[i] = false;
		remove_act[i] = false;
		extra_act[i] = false;
		pzone_act[i] = false;
	}
	cards_.clear();
	overlay_cards.clear();
	extra_p_count[0] = 0;
	extra_p_count[1] = 0;
	player_desc_hints[0].clear();
	player_desc_hints[1].clear();
	chains.clear();
	activatable_cards.clear();
	summonable_cards.clear();
	spsummonable_cards.clear();
	msetable_cards.clear();
	ssetable_cards.clear();
	reposable_cards.clear();
	attackable_cards.clear();
	disabled_field = 0;
	panel = 0;
	hovered_card = 0;
	clicked_card = 0;
	highlighting_card = 0;
	menu_card = 0;
	hovered_controler = 0;
	hovered_location = 0;
	hovered_sequence = 0;
	conti_act = false;
	deck_reversed = false;
	cant_check_grave = false;
	tag_surrender = false;
	tag_teammate_surrender = false;
}
void ClientField::Initial(int player, int deckc, int extrac, int sidec) {
	auto load_location = [&](std::vector<ClientCard*>& container, int count, uint8_t location) {
		for(int i = 0; i < count; ++i) {
			ClientCard* pcard = CreateCard();
			container.push_back(pcard);
			pcard->owner = player;
			pcard->controler = player;
			pcard->location = location;
			pcard->sequence = i;
			pcard->position = POS_FACEDOWN_DEFENSE;
			GetCardLocation(pcard, &pcard->curPos, &pcard->curRot, true);
		}
	};

	load_location(deck[player], deckc, LOCATION_DECK);
	load_location(extra[player], extrac, LOCATION_EXTRA);
	load_location(remove[player], sidec, LOCATION_REMOVED);
}
void ClientField::ResetSequence(std::vector<ClientCard*>& list, bool reset_height) {
	unsigned char seq = 0;
	for (auto& pcard : list) {
		pcard->sequence = seq++;
		if (reset_height) {
			pcard->curPos.Z = 0.01f + 0.01f * pcard->sequence;
			pcard->mTransform.setTranslation(pcard->curPos);
		}
	}
}

void ClientField::AddCard(ClientCard* pcard, int controler, int location, int sequence) {
	pcard->controler = controler;
	pcard->location = location;
	pcard->sequence = sequence;
	switch(location) {
	case LOCATION_DECK: {
		if (sequence != 0 || deck[controler].size() == 0) {
			deck[controler].push_back(pcard);
		} else {
			deck[controler].insert(deck[controler].begin(), pcard);
		}
		ResetSequence(deck[controler], true);
		pcard->is_reversed = false;
		pcard->ClearData();
		pcard->ClearTarget();
		SetShowMark(pcard, false);
		break;
	}
	case LOCATION_HAND: {
		hand[controler].push_back(pcard);
		ResetSequence(hand[controler], false);
		break;
	}
	case LOCATION_MZONE: {
		mzone[controler][sequence] = pcard;
		break;
	}
	case LOCATION_SZONE: {
		szone[controler][sequence] = pcard;
		break;
	}
	case LOCATION_GRAVE: {
		grave[controler].push_back(pcard);
		pcard->sequence = (unsigned char)(grave[controler].size() - 1);
		break;
	}
	case LOCATION_REMOVED: {
		remove[controler].push_back(pcard);
		pcard->sequence = (unsigned char)(remove[controler].size() - 1);
		break;
	}
	case LOCATION_EXTRA: {
		if(extra_p_count[controler] == 0 || (pcard->position & POS_FACEUP)) {
			extra[controler].push_back(pcard);
		} else {
			size_t faceup_begin = extra[controler].size() - extra_p_count[controler];
			extra[controler].insert(extra[controler].begin() + faceup_begin, pcard);
		}
		ResetSequence(extra[controler], true);
		if (pcard->position & POS_FACEUP)
			extra_p_count[controler]++;
		break;
	}
	}
}
ClientCard* ClientField::RemoveCard(int controler, int location, int sequence) {
	ClientCard* pcard = nullptr;
	switch (location) {
	case LOCATION_DECK: {
		pcard = deck[controler][sequence];
		for (size_t i = sequence; i < deck[controler].size() - 1; ++i) {
			deck[controler][i] = deck[controler][i + 1];
			deck[controler][i]->sequence--;
			deck[controler][i]->curPos -= irr::core::vector3df(0, 0, 0.01f);
			deck[controler][i]->mTransform.setTranslation(deck[controler][i]->curPos);
		}
		deck[controler].erase(deck[controler].end() - 1);
		break;
	}
	case LOCATION_HAND: {
		pcard = hand[controler][sequence];
		hand[controler].erase(hand[controler].begin() + sequence);
		ResetSequence(hand[controler], false);
		break;
	}
	case LOCATION_MZONE: {
		pcard = mzone[controler][sequence];
		mzone[controler][sequence] = nullptr;
		break;
	}
	case LOCATION_SZONE: {
		pcard = szone[controler][sequence];
		szone[controler][sequence] = nullptr;
		break;
	}
	case LOCATION_GRAVE: {
		pcard = grave[controler][sequence];
		for (size_t i = sequence; i < grave[controler].size() - 1; ++i) {
			grave[controler][i] = grave[controler][i + 1];
			grave[controler][i]->sequence--;
			grave[controler][i]->curPos -= irr::core::vector3df(0, 0, 0.01f);
			grave[controler][i]->mTransform.setTranslation(grave[controler][i]->curPos);
		}
		grave[controler].erase(grave[controler].end() - 1);
		break;
	}
	case LOCATION_REMOVED: {
		pcard = remove[controler][sequence];
		for (size_t i = sequence; i < remove[controler].size() - 1; ++i) {
			remove[controler][i] = remove[controler][i + 1];
			remove[controler][i]->sequence--;
			remove[controler][i]->curPos -= irr::core::vector3df(0, 0, 0.01f);
			remove[controler][i]->mTransform.setTranslation(remove[controler][i]->curPos);
		}
		remove[controler].erase(remove[controler].end() - 1);
		break;
	}
	case LOCATION_EXTRA: {
		pcard = extra[controler][sequence];
		for (size_t i = sequence; i < extra[controler].size() - 1; ++i) {
			extra[controler][i] = extra[controler][i + 1];
			extra[controler][i]->sequence--;
			extra[controler][i]->curPos -= irr::core::vector3df(0, 0, 0.01f);
			extra[controler][i]->mTransform.setTranslation(extra[controler][i]->curPos);
		}
		extra[controler].erase(extra[controler].end() - 1);
		if (pcard->position & POS_FACEUP)
			extra_p_count[controler]--;
		break;
	}
	default:
		return nullptr;
	}
	pcard->location = 0;
	return pcard;
}
void ClientField::UpdateCard(int controler, int location, int sequence, unsigned char* data) {
	ClientCard* pcard = GetCard(controler, location, sequence);
	int len = BufferIO::Read<int32_t>(data);
	if (pcard && len > LEN_HEADER)
		pcard->UpdateInfo(data);
}
void ClientField::UpdateFieldCard(int controler, int location, unsigned char* data) {
	std::vector<ClientCard*>* lst = 0;
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
		return;
	int len;
	for(auto cit = lst->begin(); cit != lst->end(); ++cit) {
		len = BufferIO::Read<int32_t>(data);
		if(len > LEN_HEADER)
			(*cit)->UpdateInfo(data);
		data += len - 4;
	}
}
void ClientField::ClearCommandFlag() {
	for(auto cit = activatable_cards.begin(); cit != activatable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = summonable_cards.begin(); cit != summonable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = spsummonable_cards.begin(); cit != spsummonable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = msetable_cards.begin(); cit != msetable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = ssetable_cards.begin(); cit != ssetable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = reposable_cards.begin(); cit != reposable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(auto cit = attackable_cards.begin(); cit != attackable_cards.end(); ++cit)
		(*cit)->cmdFlag = 0;
	for(int i = 0; i < 2; ++i) {
		deck_act[i] = false;
		extra_act[i] = false;
		grave_act[i] = false;
		remove_act[i] = false;
		pzone_act[i] = false;
	}
	conti_cards.clear();
	conti_act = false;
}
void ClientField::ClearSelect() {
	for(auto cit = selectable_cards.begin(); cit != selectable_cards.end(); ++cit) {
		(*cit)->is_selectable = false;
		(*cit)->is_selected = false;
	}
	for(auto cit = selected_cards.begin(); cit != selected_cards.end(); ++cit) {
		(*cit)->is_selectable = false;
		(*cit)->is_selected = false;
	}
	for(auto cit = selectsum_all.begin(); cit != selectsum_all.end(); ++cit) {
		(*cit)->is_selectable = false;
		(*cit)->is_selected = false;
	}
	for(auto cit = selectsum_cards.begin(); cit != selectsum_cards.end(); ++cit) {
		(*cit)->is_selectable = false;
		(*cit)->is_selected = false;
	}
}
void ClientField::ClearChainSelect() {
	for(auto cit = activatable_cards.begin(); cit != activatable_cards.end(); ++cit) {
		(*cit)->cmdFlag = 0;
		(*cit)->chain_code = 0;
		(*cit)->is_selectable = false;
		(*cit)->is_selected = false;
	}
	for(int i = 0; i < 2; ++i) {
		deck_act[i] = false;
		extra_act[i] = false;
		grave_act[i] = false;
		remove_act[i] = false;
		pzone_act[i] = false;
	}
	conti_cards.clear();
	conti_act = false;
}
void ClientField::SetCardListLabel(irr::gui::IGUIStaticText* label, ClientCard* pcard, bool selecting_card) {
	wchar_t formatBuffer[2048];
	if(selecting_card && select_continuous)
		myswprintf(formatBuffer, L"%ls", dataManager.unknown_string);
	else if(selecting_card && cant_check_grave && pcard->location == LOCATION_GRAVE)
		myswprintf(formatBuffer, L"%ls", dataManager.FormatLocation(pcard->location, 0));
	else if(pcard->location == LOCATION_OVERLAY)
		myswprintf(formatBuffer, L"%ls[%d](%d)",
			dataManager.FormatLocation(pcard->overlayTarget), pcard->overlayTarget->sequence + 1, pcard->sequence + 1);
	else
		myswprintf(formatBuffer, L"%ls[%d]", dataManager.FormatLocation(pcard), pcard->sequence + 1);
	label->setText(formatBuffer);
	label->enableOverrideColor(false); // setOverrideColor will turn this on again automatically
	if(selecting_card && select_continuous) {
		label->setBackgroundColor(CARD_LIST_DEFAULT_BACKGROUND_COLOR);
		return;
	}
	if(pcard->location == LOCATION_OVERLAY) {
		if(pcard->owner != pcard->overlayTarget->controler)
			label->setOverrideColor(CARD_LIST_OVERRIDE_TEXT_COLOR);
		if(selecting_card && pcard->is_selected)
			label->setBackgroundColor(CARD_LIST_SELECTED_BACKGROUND_COLOR);
		else if(pcard->overlayTarget->controler)
			label->setBackgroundColor(CARD_LIST_OPPONENT_BACKGROUND_COLOR);
		else
			label->setBackgroundColor(CARD_LIST_DEFAULT_BACKGROUND_COLOR);
	} else if(pcard->location == LOCATION_EXTRA || pcard->location == LOCATION_REMOVED || pcard->location == LOCATION_DECK) {
		if(pcard->position & POS_FACEDOWN)
			label->setOverrideColor(CARD_LIST_OVERRIDE_TEXT_COLOR);
		if(selecting_card && pcard->is_selected)
			label->setBackgroundColor(CARD_LIST_SELECTED_BACKGROUND_COLOR);
		else if(pcard->controler)
			label->setBackgroundColor(CARD_LIST_OPPONENT_BACKGROUND_COLOR);
		else
			label->setBackgroundColor(CARD_LIST_DEFAULT_BACKGROUND_COLOR);
	} else {
		if(selecting_card && pcard->is_selected)
			label->setBackgroundColor(CARD_LIST_SELECTED_BACKGROUND_COLOR);
		else if(pcard->controler)
			label->setBackgroundColor(CARD_LIST_OPPONENT_BACKGROUND_COLOR);
		else
			label->setBackgroundColor(CARD_LIST_DEFAULT_BACKGROUND_COLOR);
	}
}
// needs to be synchronized with EGET_SCROLL_BAR_CHANGED
void ClientField::ShowSelectCard(bool buttonok, bool is_continuous) {
	select_continuous = is_continuous;
	if(cant_check_grave) {
		bool has_card_in_grave = false;
		for (auto& pcard : selectable_cards) {
			if (pcard->location == LOCATION_GRAVE) {
				has_card_in_grave = true;
				break;
			}
		}
		if(has_card_in_grave) {
			thread_local std::mt19937 rnd{std::random_device{}()};
			std::shuffle(selectable_cards.begin(), selectable_cards.end(), rnd);
		}
	}
	int ct = 5;
	if(selectable_cards.size() <= 5) {
		ct = selectable_cards.size();
	}
	for(int i = 0; i < ct; ++i) {
		// image
		if(selectable_cards[i]->code)
			mainGame->btnImagePending[mainGame->btnCardSelect[i]] = std::make_pair(selectable_cards[i]->code, false);
		else if(select_continuous)
			mainGame->btnImagePending[mainGame->btnCardSelect[i]] = std::make_pair(selectable_cards[i]->chain_code, false);
		else {
			mainGame->btnCardSelect[i]->setImage(imageManager.tButtonFacedown[selectable_cards[i]->controler]);
			mainGame->btnFacedownImgInfo[mainGame->btnCardSelect[i]] = {selectable_cards[i]->controler, false};
			mainGame->btnCardImgInfo.erase(mainGame->btnCardSelect[i]);
		}
		mainGame->btnCardSelect[i]->setPressed(false);
		mainGame->btnCardSelect[i]->setVisible(true);
		if(mainGame->dInfo.curMsg != MSG_SORT_CARD) {
			SetCardListLabel(mainGame->stCardPos[i], selectable_cards[i], true);
		} else {
			if(sort_list[i]) {
				wchar_t formatBuffer[2048];
				myswprintf(formatBuffer, L"%d", sort_list[i]);
				mainGame->stCardPos[i]->setText(formatBuffer);
			} else
				mainGame->stCardPos[i]->setText(L"");
			mainGame->stCardPos[i]->enableOverrideColor(false);
			mainGame->stCardPos[i]->setBackgroundColor(CARD_LIST_DEFAULT_BACKGROUND_COLOR);
		}
		mainGame->stCardPos[i]->setVisible(true);
	}
	if(selectable_cards.size() <= 5) {
		for(int i = selectable_cards.size(); i < 5; ++i) {
			mainGame->btnCardSelect[i]->setVisible(false);
			mainGame->stCardPos[i]->setVisible(false);
		}
		mainGame->scrCardList->setPos(0);
		mainGame->scrCardList->setVisible(false);
	} else {
		mainGame->scrCardList->setVisible(true);
		mainGame->scrCardList->setMin(0);
		mainGame->scrCardList->setMax((selectable_cards.size() - 5) * 10 + 9);
		mainGame->scrCardList->setPos(0);
	}
	mainGame->btnSelectOK->setVisible(buttonok);
	mainGame->ResizeCardSelectButtons(mainGame->wCardSelect, mainGame->stCardPos, mainGame->btnCardSelect, mainGame->scrCardList, mainGame->btnSelectOK, selectable_cards);
	mainGame->PopupElement(mainGame->wCardSelect);
}
void ClientField::ShowChainCard() {
	int ct = 5;
	if(selectable_cards.size() <= 5) {
		ct = selectable_cards.size();
	}
	for(int i = 0; i < ct; ++i) {
		if(selectable_cards[i]->code)
			mainGame->btnImagePending[mainGame->btnCardSelect[i]] = std::make_pair(selectable_cards[i]->code, false);
		else {
			mainGame->btnCardSelect[i]->setImage(imageManager.tButtonFacedown[selectable_cards[i]->controler]);
			mainGame->btnFacedownImgInfo[mainGame->btnCardSelect[i]] = {selectable_cards[i]->controler, false};
			mainGame->btnCardImgInfo.erase(mainGame->btnCardSelect[i]);
		}
		mainGame->btnCardSelect[i]->setPressed(false);
		mainGame->btnCardSelect[i]->setVisible(true);
		SetCardListLabel(mainGame->stCardPos[i], selectable_cards[i], false);
		mainGame->stCardPos[i]->setVisible(true);
	} 
	if(selectable_cards.size() <= 5) {
		for(int i = selectable_cards.size(); i < 5; ++i) {
			mainGame->btnCardSelect[i]->setVisible(false);
			mainGame->stCardPos[i]->setVisible(false);
		}
		mainGame->scrCardList->setPos(0);
		mainGame->scrCardList->setVisible(false);
	} else {
		mainGame->scrCardList->setVisible(true);
		mainGame->scrCardList->setMin(0);
		mainGame->scrCardList->setMax((selectable_cards.size() - 5) * 10 + 9);
		mainGame->scrCardList->setPos(0);
	}
	mainGame->btnSelectOK->setVisible(!chain_forced);
	mainGame->ResizeCardSelectButtons(mainGame->wCardSelect, mainGame->stCardPos, mainGame->btnCardSelect, mainGame->scrCardList, mainGame->btnSelectOK, selectable_cards);
	mainGame->PopupElement(mainGame->wCardSelect);
}
void ClientField::ShowLocationCard() {
	int ct = 5;
	if(display_cards.size() <= 5) {
		ct = display_cards.size();
	}
	for(int i = 0; i < ct; ++i) {
		if(display_cards[i]->code)
			mainGame->btnImagePending[mainGame->btnCardDisplay[i]] = std::make_pair(display_cards[i]->code, false);
		else {
			mainGame->btnCardDisplay[i]->setImage(imageManager.tButtonFacedown[display_cards[i]->controler]);
			mainGame->btnFacedownImgInfo[mainGame->btnCardDisplay[i]] = {display_cards[i]->controler, false};
			mainGame->btnCardImgInfo.erase(mainGame->btnCardDisplay[i]);
		}
		mainGame->btnCardDisplay[i]->setPressed(false);
		mainGame->btnCardDisplay[i]->setVisible(true);
		SetCardListLabel(mainGame->stDisplayPos[i], display_cards[i], false);
		mainGame->stDisplayPos[i]->setVisible(true);
	}
	if(display_cards.size() <= 5) {
		for(int i = display_cards.size(); i < 5; ++i) {
			mainGame->btnCardDisplay[i]->setVisible(false);
			mainGame->stDisplayPos[i]->setVisible(false);
		}
		mainGame->scrDisplayList->setPos(0);
		mainGame->scrDisplayList->setVisible(false);
	} else {
		mainGame->scrDisplayList->setVisible(true);
		mainGame->scrDisplayList->setMin(0);
		mainGame->scrDisplayList->setMax((display_cards.size() - 5) * 10 + 9);
		mainGame->scrDisplayList->setPos(0);
	}
	mainGame->btnDisplayOK->setVisible(true);
	mainGame->ResizeCardSelectButtons(mainGame->wCardDisplay, mainGame->stDisplayPos, mainGame->btnCardDisplay, mainGame->scrDisplayList, mainGame->btnDisplayOK, display_cards);
	mainGame->PopupElement(mainGame->wCardDisplay);
}
void ClientField::ShowSelectOption(int select_hint) {
	selected_option = 0;
	wchar_t textBuffer[256];
	int count = select_options.size();
	bool quickmode = true;
	mainGame->gMutex.lock();
	for(auto option : select_options) {
		if(mainGame->GetGUIFontDimension(dataManager.GetDesc(option)).Width > 310) {
			quickmode = false;
			break;
		}
	}
	for(int i = 0; (i < count) && (i < 5) && quickmode; i++) {
		const wchar_t* option = dataManager.GetDesc(select_options[i]);
		mainGame->btnOption[i]->setText(option);
	}
	if(quickmode) {
		bool scrollbar = count > 5;
		mainGame->scrOption->setVisible(scrollbar);
		mainGame->scrOption->setPos(0);
		mainGame->scrOption->setMax(scrollbar ? (count - 5) : 1);
		mainGame->stOptions->setVisible(false);
		mainGame->btnOptionp->setVisible(false);
		mainGame->btnOptionn->setVisible(false);
		mainGame->btnOptionOK->setVisible(false);
		for(int i = 0; i < 5; i++)
			mainGame->btnOption[i]->setVisible(i < count);
		irr::core::recti pos = mainGame->wOptions->getRelativePosition();
		int newheight = 30 + 40 * (scrollbar ? 5 : count);
		int oldheight = pos.LowerRightCorner.Y - pos.UpperLeftCorner.Y;
		pos.UpperLeftCorner.Y = pos.UpperLeftCorner.Y + (oldheight - newheight) / 2;
		pos.LowerRightCorner.X = pos.UpperLeftCorner.X + (scrollbar ? 375 : 350);
		pos.LowerRightCorner.Y = pos.UpperLeftCorner.Y + newheight;
		mainGame->wOptions->setRelativePosition(pos);
	} else {
		mainGame->SetStaticText(mainGame->stOptions, 310, mainGame->guiFont, dataManager.GetDesc(select_options[0]));
		mainGame->stOptions->setVisible(true);
		mainGame->btnOptionp->setVisible(false);
		mainGame->btnOptionn->setVisible(count > 1);
		mainGame->btnOptionOK->setVisible(true);
		for(int i = 0; i < 5; i++)
			mainGame->btnOption[i]->setVisible(false);
		irr::core::recti pos = mainGame->wOptions->getRelativePosition();
		pos.LowerRightCorner.Y = pos.UpperLeftCorner.Y + 140;
		mainGame->wOptions->setRelativePosition(pos);
	}
	if(select_hint)
		myswprintf(textBuffer, L"%ls", dataManager.GetDesc(select_hint));
	else
		myswprintf(textBuffer, dataManager.GetSysString(555));
	mainGame->wOptions->setText(textBuffer);
	mainGame->PopupElement(mainGame->wOptions);
	mainGame->gMutex.unlock();
}
void ClientField::ReplaySwap() {
	std::swap(deck[0], deck[1]);
	std::swap(hand[0], hand[1]);
	std::swap(mzone[0], mzone[1]);
	std::swap(szone[0], szone[1]);
	std::swap(grave[0], grave[1]);
	std::swap(remove[0], remove[1]);
	std::swap(extra[0], extra[1]);
	std::swap(extra_p_count[0], extra_p_count[1]);
	for(int p = 0; p < 2; ++p) {
		for(auto cit = deck[p].begin(); cit != deck[p].end(); ++cit) {
			(*cit)->controler = 1 - (*cit)->controler;
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = hand[p].begin(); cit != hand[p].end(); ++cit) {
			(*cit)->controler = 1 - (*cit)->controler;
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = mzone[p].begin(); cit != mzone[p].end(); ++cit) {
			if(*cit) {
				(*cit)->controler = 1 - (*cit)->controler;
				GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
				(*cit)->is_moving = false;
			}
		}
		for(auto cit = szone[p].begin(); cit != szone[p].end(); ++cit) {
			if(*cit) {
				(*cit)->controler = 1 - (*cit)->controler;
				GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
				(*cit)->is_moving = false;
			}
		}
		for(auto cit = grave[p].begin(); cit != grave[p].end(); ++cit) {
			(*cit)->controler = 1 - (*cit)->controler;
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = remove[p].begin(); cit != remove[p].end(); ++cit) {
			(*cit)->controler = 1 - (*cit)->controler;
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = extra[p].begin(); cit != extra[p].end(); ++cit) {
			(*cit)->controler = 1 - (*cit)->controler;
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
	}
	for(auto cit = overlay_cards.begin(); cit != overlay_cards.end(); ++cit) {
		(*cit)->controler = 1 - (*cit)->controler;
		GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
		(*cit)->is_moving = false;
	}
	mainGame->dInfo.isFirst = !mainGame->dInfo.isFirst;
	mainGame->dInfo.isReplaySwapped = !mainGame->dInfo.isReplaySwapped;
	std::swap(mainGame->dInfo.lp[0], mainGame->dInfo.lp[1]);
	std::swap(mainGame->dInfo.strLP[0], mainGame->dInfo.strLP[1]);
	std::swap(mainGame->dInfo.hostname, mainGame->dInfo.clientname);
	std::swap(mainGame->dInfo.hostname_tag, mainGame->dInfo.clientname_tag);
	for(auto chit = chains.begin(); chit != chains.end(); ++chit) {
		chit->controler = 1 - chit->controler;
		GetChainLocation(chit->controler, chit->location, chit->sequence, &chit->chain_pos);
	}
	disabled_field = (disabled_field >> 16) | (disabled_field << 16);
}
void ClientField::RefreshAllCards() {
	for(int p = 0; p < 2; ++p) {
		for(auto cit = deck[p].begin(); cit != deck[p].end(); ++cit) {
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = hand[p].begin(); cit != hand[p].end(); ++cit) {
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = mzone[p].begin(); cit != mzone[p].end(); ++cit) {
			if(*cit) {
				GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
				(*cit)->is_moving = false;
			}
		}
		for(auto cit = szone[p].begin(); cit != szone[p].end(); ++cit) {
			if(*cit) {
				GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
				(*cit)->is_moving = false;
			}
		}
		for(auto cit = grave[p].begin(); cit != grave[p].end(); ++cit) {
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = remove[p].begin(); cit != remove[p].end(); ++cit) {
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
		for(auto cit = extra[p].begin(); cit != extra[p].end(); ++cit) {
			GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
			(*cit)->is_moving = false;
		}
	}
	for(auto cit = overlay_cards.begin(); cit != overlay_cards.end(); ++cit) {
		GetCardLocation(*cit, &(*cit)->curPos, &(*cit)->curRot, true);
		(*cit)->is_moving = false;
	}
}

void ClientField::MoveCard(ClientCard * pcard, int frame) {
	irr::core::vector3df trans = pcard->curPos;
	irr::core::vector3df rot = pcard->curRot;
	GetCardLocation(pcard, &trans, &rot);
	pcard->dPos = (trans - pcard->curPos) / frame;
	float diff = rot.X - pcard->curRot.X;
	while (diff < 0) diff += 3.1415926f * 2;
	while (diff > 3.1415926f * 2)
		diff -= 3.1415926f * 2;
	if (diff < 3.1415926f)
		pcard->dRot.X = diff / frame;
	else
		pcard->dRot.X = -(3.1415926f * 2 - diff) / frame;
	diff = rot.Y - pcard->curRot.Y;
	while (diff < 0) diff += 3.1415926f * 2;
	while (diff > 3.1415926f * 2) diff -= 3.1415926f * 2;
	if (diff < 3.1415926f)
		pcard->dRot.Y = diff / frame;
	else
		pcard->dRot.Y = -(3.1415926f * 2 - diff) / frame;
	diff = rot.Z - pcard->curRot.Z;
	while (diff < 0) diff += 3.1415926f * 2;
	while (diff > 3.1415926f * 2) diff -= 3.1415926f * 2;
	if (diff < 3.1415926f)
		pcard->dRot.Z = diff / frame;
	else
		pcard->dRot.Z = -(3.1415926f * 2 - diff) / frame;
	pcard->is_moving = true;
	pcard->aniFrame = frame;
}
void ClientField::FadeCard(ClientCard * pcard, int alpha, int frame) {
	pcard->dAlpha = (alpha - pcard->curAlpha) / frame;
	pcard->is_fading = true;
	pcard->aniFrame = frame;
}
bool ClientField::ShowSelectSum(bool panelmode) {
	select_ready = CheckSelectSum();
	if(select_ready && (selectsum_cards.size() == 0 || selectable_cards.size() == 0)) {
		SetResponseSelectedCards();
		ShowCancelOrFinishButton(0);
		if(mainGame->wCardSelect->isVisible())
			mainGame->HideElement(mainGame->wCardSelect, true);
		else 
			DuelClient::SendResponse();
		return true;
	}

	auto display_hint = select_hint ? dataManager.GetDesc(select_hint) : dataManager.GetSysString(560);

	wchar_t cur_hint[20];
	if (select_curval_l == select_curval_h) {
		myswprintf(cur_hint, L"%d", select_curval_l);
	} else {
		myswprintf(cur_hint, L"%d-%d", select_curval_l, select_curval_h);
	}

	wchar_t target_hint[20];
	if (select_mode == 0) { // sum equal
		myswprintf(target_hint, L"%d", select_sumval);
	} else { // sum greater
		myswprintf(target_hint, L"%d+", select_sumval);
	}

	wchar_t textBuffer[256];
	myswprintf(textBuffer, L"%ls(%ls/%ls)", display_hint, cur_hint, target_hint);

	if(panelmode) {
		mainGame->wCardSelect->setText(textBuffer);
		mainGame->wCardSelect->setVisible(false);
		mainGame->dField.ShowSelectCard();
	} else {
		mainGame->stHintMsg->setText(textBuffer);
		mainGame->stHintMsg->setVisible(true);
	}
	if (select_ready) {
		ShowCancelOrFinishButton(2);
	} else {
		ShowCancelOrFinishButton(0);
	}
	return false;
}

template <class T>
static bool is_declarable(const T& cd, const std::vector<unsigned int>& opcode) {
	if (cd.alias)
		return false;
	std::stack<int> stack;
	for(auto it = opcode.begin(); it != opcode.end(); ++it) {
		switch(*it) {
		case OPCODE_ADD: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(lhs + rhs);
			}
			break;
		}
		case OPCODE_SUB: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(lhs - rhs);
			}
			break;
		}
		case OPCODE_MUL: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(lhs * rhs);
			}
			break;
		}
		case OPCODE_DIV: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(rhs != 0 ? lhs / rhs : 0);
			}
			break;
		}
		case OPCODE_AND: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(static_cast<int>(lhs && rhs));
			}
			break;
		}
		case OPCODE_OR: {
			if (stack.size() >= 2) {
				int rhs = stack.top();
				stack.pop();
				int lhs = stack.top();
				stack.pop();
				stack.push(static_cast<int>(lhs || rhs));
			}
			break;
		}
		case OPCODE_NEG: {
			if (stack.size() >= 1) {
				int val = stack.top();
				stack.pop();
				stack.push(-val);
			}
			break;
		}
		case OPCODE_NOT: {
			if (stack.size() >= 1) {
				int val = stack.top();
				stack.pop();
				stack.push(static_cast<int>(!val));
			}
			break;
		}
		case OPCODE_ISCODE: {
			if (stack.size() >= 1) {
				int code = stack.top();
				stack.pop();
				stack.push(cd.code == code);
			}
			break;
		}
		case OPCODE_ISSETCARD: {
			if (stack.size() >= 1) {
				uint32_t set_code = stack.top();
				stack.pop();
				bool res = false;
				for (const auto& x : cd.setcode) {
					if(check_setcode(x, set_code)) {
						res = true;
						break;
					}
				}
				stack.push(res);
			}
			break;
		}
		case OPCODE_ISTYPE: {
			if (stack.size() >= 1) {
				int val = stack.top();
				stack.pop();
				stack.push(cd.type & val);
			}
			break;
		}
		case OPCODE_ISRACE: {
			if (stack.size() >= 1) {
				int race = stack.top();
				stack.pop();
				stack.push(cd.race & race);
			}
			break;
		}
		case OPCODE_ISATTRIBUTE: {
			if (stack.size() >= 1) {
				int attribute = stack.top();
				stack.pop();
				stack.push(cd.attribute & attribute);
			}
			break;
		}
		default: {
			stack.push(*it);
			break;
		}
		}
	}
	if(stack.size() != 1 || stack.top() == 0)
		return false;
	if (!second_code.count(cd.code) && (cd.rule_code || (cd.type & TYPE_TOKEN)))
		return false;
	return true;
}
void ClientField::UpdateDeclarableList() {
	const wchar_t* pname = mainGame->ebANCard->getText();
	int trycode = BufferIO::GetVal(pname);
	CardData cd;
	if (dataManager.GetData(trycode, &cd) && is_declarable(cd, declare_opcodes)) {
		auto& _strings = dataManager.GetStringTable();
		auto it = _strings.find(trycode);
		mainGame->lstANCard->clear();
		ancard.clear();
		mainGame->lstANCard->addItem(it->second.name.c_str());
		ancard.push_back(trycode);
		return;
	}
	if(pname[0] == 0) {
		int sel = mainGame->lstANCard->getSelected();
		trycode = (sel == -1) ? 0 : ancard[sel];
	}
	auto setcodes = dataManager.GetSetCodes(pname);
	mainGame->lstANCard->clear();
	ancard.clear();
	auto& _datas = dataManager.GetDataTable();
	auto& _strings = dataManager.GetStringTable();
	for(auto& entry : _strings) {
		auto& code = entry.first;
		auto& str = entry.second;
		auto cp = _datas.find(code);
		if (cp == _datas.end())
			continue;
		auto& data = cp->second;
		if(DataManager::CardNameContains(str.name.c_str(), pname) || data.is_setcodes(setcodes)) {
			if(is_declarable(data, declare_opcodes)) {
				if(pname == str.name || trycode == code) { //exact match or last used
					mainGame->lstANCard->insertItem(0, str.name.c_str(), -1);
					ancard.insert(ancard.begin(), code);
				} else {
					mainGame->lstANCard->addItem(str.name.c_str());
					ancard.push_back(code);
				}
			}
		}
	}
}
}
