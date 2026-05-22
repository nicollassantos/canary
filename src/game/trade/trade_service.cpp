/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/blogs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/trade/trade_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "lib/metrics/metrics.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"

bool TradeService::internalStartTrade(const std::shared_ptr<Player> &player, const std::shared_ptr<Player> &tradePartner, const std::shared_ptr<Item> &tradeItem) {
	if (player->tradeState != TRADE_NONE && !(player->tradeState == TRADE_ACKNOWLEDGE && player->tradePartner == tradePartner)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREALREADYTRADING);
		return false;
	} else if (tradePartner->tradeState != TRADE_NONE && tradePartner->tradePartner != player) {
		player->sendCancelMessage(RETURNVALUE_THISPLAYERISALREADYTRADING);
		return false;
	}
	if (tradeItem->isStoreItem() || tradeItem->hasOwner()) {
		player->sendCancelMessage(RETURNVALUE_ITEMUNTRADEABLE);
		return false;
	}

	player->tradePartner = tradePartner;
	player->tradeItem = tradeItem;
	player->tradeState = TRADE_INITIATED;
	game_.tradeItems[tradeItem] = player->getID();

	player->sendTradeItemRequest(player->getName(), tradeItem, true);

	if (tradePartner->tradeState == TRADE_NONE) {
		std::ostringstream ss;
		ss << player->getName() << " wants to trade with you.";
		tradePartner->sendTextMessage(MESSAGE_TRANSACTION, ss.str());
		tradePartner->tradeState = TRADE_ACKNOWLEDGE;
		tradePartner->tradePartner = player;
	} else {
		std::shared_ptr<Item> counterOfferItem = tradePartner->tradeItem;
		player->sendTradeItemRequest(tradePartner->getName(), counterOfferItem, false);
		tradePartner->sendTradeItemRequest(player->getName(), tradeItem, false);
	}

	return true;
}

void TradeService::playerAcceptTrade(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!(player->getTradeState() == TRADE_ACKNOWLEDGE || player->getTradeState() == TRADE_INITIATED)) {
		return;
	}

	std::shared_ptr<Player> tradePartner = player->tradePartner;
	if (!tradePartner) {
		return;
	}

	if (!game_.canThrowObjectTo(tradePartner->getPosition(), player->getPosition(), SightLine_CheckSightLineAndFloor)) {
		player->sendCancelMessage(RETURNVALUE_CREATUREISNOTREACHABLE);
		return;
	}

	player->setTradeState(TRADE_ACCEPT);

	if (tradePartner->getTradeState() == TRADE_ACCEPT) {
		std::shared_ptr<Item> tradeItem1 = player->tradeItem;
		std::shared_ptr<Item> tradeItem2 = tradePartner->tradeItem;
		if (!g_events().eventPlayerOnTradeAccept(player, tradePartner, tradeItem1, tradeItem2)) {
			internalCloseTrade(player);
			return;
		}

		if (!g_callbacks().checkCallback(EventCallback_t::playerOnTradeAccept, player, tradePartner, tradeItem1, tradeItem2)) {
			internalCloseTrade(player);
			return;
		}

		player->setTradeState(TRADE_TRANSFER);
		tradePartner->setTradeState(TRADE_TRANSFER);

		auto it = game_.tradeItems.find(tradeItem1);
		if (it != game_.tradeItems.end()) {
			game_.tradeItems.erase(it);
		}

		it = game_.tradeItems.find(tradeItem2);
		if (it != game_.tradeItems.end()) {
			game_.tradeItems.erase(it);
		}

		bool isSuccess = false;

		ReturnValue ret1 = game_.internalAddItem(tradePartner, tradeItem1, INDEX_WHEREEVER, 0, true);
		ReturnValue ret2 = game_.internalAddItem(player, tradeItem2, INDEX_WHEREEVER, 0, true);
		if (ret1 == RETURNVALUE_NOERROR && ret2 == RETURNVALUE_NOERROR) {
			ret1 = game_.internalRemoveItem(tradeItem1, tradeItem1->getItemCount(), true);
			ret2 = game_.internalRemoveItem(tradeItem2, tradeItem2->getItemCount(), true);
			if (ret1 == RETURNVALUE_NOERROR && ret2 == RETURNVALUE_NOERROR) {
				std::shared_ptr<Cylinder> cylinder1 = tradeItem1->getParent();
				std::shared_ptr<Cylinder> cylinder2 = tradeItem2->getParent();

				uint32_t count1 = tradeItem1->getItemCount();
				uint32_t count2 = tradeItem2->getItemCount();

				ret1 = game_.internalMoveItem(cylinder1, tradePartner, INDEX_WHEREEVER, tradeItem1, count1, nullptr, FLAG_IGNOREAUTOSTACK, nullptr, tradeItem2);
				if (ret1 == RETURNVALUE_NOERROR) {
					game_.internalMoveItem(cylinder2, player, INDEX_WHEREEVER, tradeItem2, count2, nullptr, FLAG_IGNOREAUTOSTACK);

					tradeItem1->onTradeEvent(ON_TRADE_TRANSFER, tradePartner);
					tradeItem2->onTradeEvent(ON_TRADE_TRANSFER, player);

					isSuccess = true;
				}
			}
		}

		if (!isSuccess) {
			std::string errorDescription;

			if (tradePartner->tradeItem) {
				errorDescription = getTradeErrorDescription(ret1, tradeItem1);
				tradePartner->sendTextMessage(MESSAGE_TRANSACTION, errorDescription);
				tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			}

			if (player->tradeItem) {
				errorDescription = getTradeErrorDescription(ret2, tradeItem2);
				player->sendTextMessage(MESSAGE_TRANSACTION, errorDescription);
				player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
			}
		}

		player->setTradeState(TRADE_NONE);
		player->tradeItem = nullptr;
		player->tradePartner = nullptr;
		player->sendTradeClose();

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradeItem = nullptr;
		tradePartner->tradePartner = nullptr;
		tradePartner->sendTradeClose();
	}
}

std::string TradeService::getTradeErrorDescription(ReturnValue ret, const std::shared_ptr<Item> &item) {
	if (item) {
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			std::ostringstream ss;
			ss << "You do not have enough capacity to carry";

			if (item->isStackable() && item->getItemCount() > 1) {
				ss << " these objects.";
			} else {
				ss << " this object.";
			}

			ss << std::endl
			   << ' ' << item->getWeightDescription();
			return ss.str();
		} else if (ret == RETURNVALUE_NOTENOUGHROOM || ret == RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			std::ostringstream ss;
			ss << "You do not have enough room to carry";

			if (item->isStackable() && item->getItemCount() > 1) {
				ss << " these objects.";
			} else {
				ss << " this object.";
			}

			return ss.str();
		}
	}
	return "Trade could not be completed.";
}

void TradeService::playerLookInTrade(uint32_t playerId, bool lookAtCounterOffer, uint8_t index) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Player> tradePartner = player->tradePartner;
	if (!tradePartner) {
		return;
	}

	std::shared_ptr<Item> tradeItem;
	if (lookAtCounterOffer) {
		tradeItem = tradePartner->getTradeItem();
	} else {
		tradeItem = player->getTradeItem();
	}

	if (!tradeItem) {
		return;
	}

	const Position &playerPosition = player->getPosition();
	const Position &tradeItemPosition = tradeItem->getPosition();

	int32_t lookDistance = std::max<int32_t>(
		Position::getDistanceX(playerPosition, tradeItemPosition),
		Position::getDistanceY(playerPosition, tradeItemPosition)
	);
	if (index == 0) {
		g_events().eventPlayerOnLookInTrade(player, tradePartner, tradeItem, lookDistance);
		g_callbacks().executeCallback(EventCallback_t::playerOnLookInTrade, player, tradePartner, tradeItem, lookDistance);
		return;
	}

	std::shared_ptr<Container> tradeContainer = tradeItem->getContainer();
	if (!tradeContainer) {
		return;
	}

	std::vector<std::shared_ptr<Container>> containers { tradeContainer };
	size_t i = 0;
	while (i < containers.size()) {
		std::shared_ptr<Container> container = containers[i++];
		for (const std::shared_ptr<Item> &item : container->getItemList()) {
			std::shared_ptr<Container> tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			}

			if (--index == 0) {
				g_events().eventPlayerOnLookInTrade(player, tradePartner, item, lookDistance);
				g_callbacks().executeCallback(EventCallback_t::playerOnLookInTrade, player, tradePartner, item, lookDistance);
				return;
			}
		}
	}
}

void TradeService::playerCloseTrade(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	internalCloseTrade(player);
}

void TradeService::internalCloseTrade(const std::shared_ptr<Player> &player) {
	std::shared_ptr<Player> tradePartner = player->tradePartner;
	if ((tradePartner && tradePartner->getTradeState() == TRADE_TRANSFER) || player->getTradeState() == TRADE_TRANSFER) {
		return;
	}

	if (player->getTradeItem()) {
		auto it = game_.tradeItems.find(player->getTradeItem());
		if (it != game_.tradeItems.end()) {
			game_.tradeItems.erase(it);
		}

		player->tradeItem->onTradeEvent(ON_TRADE_CANCEL, player);
		player->tradeItem = nullptr;
	}

	player->setTradeState(TRADE_NONE);
	player->tradePartner = nullptr;

	player->sendTextMessage(MESSAGE_FAILURE, "Trade cancelled.");
	player->sendTradeClose();

	if (tradePartner) {
		if (tradePartner->getTradeItem()) {
			auto it = game_.tradeItems.find(tradePartner->getTradeItem());
			if (it != game_.tradeItems.end()) {
				game_.tradeItems.erase(it);
			}

			tradePartner->tradeItem->onTradeEvent(ON_TRADE_CANCEL, tradePartner);
			tradePartner->tradeItem = nullptr;
		}

		tradePartner->setTradeState(TRADE_NONE);
		tradePartner->tradePartner = nullptr;

		tradePartner->sendTextMessage(MESSAGE_FAILURE, "Trade cancelled.");
		tradePartner->sendTradeClose();
	}
}

void TradeService::playerBuyItem(uint32_t playerId, uint16_t itemId, uint8_t count, uint16_t amount, bool ignoreCap /* = false*/, bool inBackpacks /* = false*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (amount == 0) {
		return;
	}

	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Npc> merchant = player->getShopOwner();
	if (!merchant) {
		return;
	}

	const ItemType &it = Item::items[itemId];
	if (it.id == 0) {
		return;
	}

	if ((it.stackable && amount > 10000) || (!it.stackable && amount > 100)) {
		return;
	}

	if (!player->hasShopItemForSale(it.id, count)) {
		return;
	}

	// Check npc say exhausted
	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	if (inBackpacks || it.isContainer()) {
		uint32_t maxContainer = static_cast<uint32_t>(config_.getNumber(MAX_CONTAINER));
		auto backpack = player->getInventoryItem(CONST_SLOT_BACKPACK);
		auto mainBackpack = backpack ? backpack->getContainer() : nullptr;

		if (mainBackpack && mainBackpack->getContainerHoldingCount() >= maxContainer) {
			player->sendCancelMessage(RETURNVALUE_CONTAINERISFULL);
			return;
		}

		std::shared_ptr<Tile> tile = player->getTile();
		if (tile && tile->getItemCount() >= 20) {
			player->sendCancelMessage(RETURNVALUE_CONTAINERISFULL);
			return;
		}
	}

	merchant->onPlayerBuyItem(player, it.id, count, amount, ignoreCap, inBackpacks);
	player->updateUIExhausted();
}

void TradeService::playerSellItem(uint32_t playerId, uint16_t itemId, uint8_t count, uint16_t amount, bool ignoreEquipped) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (amount == 0) {
		return;
	}

	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Npc> merchant = player->getShopOwner();
	if (!merchant) {
		return;
	}

	const ItemType &it = Item::items[itemId];
	if (it.id == 0) {
		return;
	}

	if ((it.stackable && amount > 10000) || (!it.stackable && amount > 100)) {
		return;
	}

	// Check npc say exhausted
	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	merchant->onPlayerSellItem(player, it.id, count, amount, ignoreEquipped);
	player->updateUIExhausted();
}

void TradeService::playerCloseShop(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->closeShopWindow();
}

void TradeService::playerLookInShop(uint32_t playerId, uint16_t itemId, uint8_t count) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Npc> merchant = player->getShopOwner();
	if (!merchant) {
		return;
	}

	const ItemType &it = Item::items[itemId];
	if (it.id == 0) {
		return;
	}

	if (!g_events().eventPlayerOnLookInShop(player, &it, count)) {
		return;
	}

	if (!g_callbacks().checkCallback(EventCallback_t::playerOnLookInShop, player, &it, count)) {
		return;
	}

	std::ostringstream ss;
	ss << "You see " << Item::getDescription(it, 1, nullptr, count);
	player->sendTextMessage(MESSAGE_LOOK, ss.str());
	merchant->onPlayerCheckItem(player, it.id, count);
}


