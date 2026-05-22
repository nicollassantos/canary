/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "items/item_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "lib/metrics/metrics.hpp"
#include "utils/tools.hpp"

ReturnValue ItemService::internalMoveItem(std::shared_ptr<Cylinder> fromCylinder, std::shared_ptr<Cylinder> toCylinder, int32_t index, const std::shared_ptr<Item> &item, uint32_t count, std::shared_ptr<Item>* movedItem, uint32_t flags /*= 0*/, const std::shared_ptr<Creature> &actor /*=nullptr*/, const std::shared_ptr<Item> &tradeItem /* = nullptr*/, bool checkTile /* = true*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (fromCylinder == nullptr) {
		g_logger().error("[{}] fromCylinder is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}
	if (toCylinder == nullptr) {
		g_logger().error("[{}] toCylinder is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (checkTile) {
		if (const std::shared_ptr<Tile> &fromTile = fromCylinder->getTile()) {
			if (fromTile && game_.browseFields.contains(fromTile) && game_.browseFields[fromTile].lock() == fromCylinder) {
				fromCylinder = fromTile;
			}
		}
	}

	std::shared_ptr<Item> toItem = nullptr;

	std::shared_ptr<Cylinder> subCylinder = nullptr;
	int floorN = 0;

	while ((subCylinder = toCylinder->queryDestination(index, item, toItem, flags)) != toCylinder) {
		if (subCylinder == nullptr) {
			break;
		}

		toCylinder = subCylinder;
		flags = 0;

		// to prevent infinite loop
		if (++floorN >= MAP_MAX_LAYERS) {
			break;
		}
	}

	// destination is the same as the source?
	if (item == toItem) {
		return RETURNVALUE_NOERROR; // silently ignore move
	}

	// 'Move up' stackable items fix
	//  Cip's client never sends the count of stackables when using "Move up" menu option
	if (item->isStackable() && count == 255 && fromCylinder->getParent() == toCylinder) {
		count = item->getItemCount();
	}

	// check if we can remove this item (using count of 1 since we don't know how
	// much we can move yet)
	ReturnValue ret = fromCylinder->queryRemove(item, 1, flags, actor);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	// check if we can add this item
	ret = toCylinder->queryAdd(index, item, count, flags, actor);
	if (ret == RETURNVALUE_NEEDEXCHANGE) {
		// check if we can add it to source cylinder
		ret = fromCylinder->queryAdd(fromCylinder->getThingIndex(item), toItem, toItem->getItemCount(), 0);
		if (ret == RETURNVALUE_NOERROR) {
			// check how much we can move
			uint32_t maxExchangeQueryCount = 0;
			ReturnValue retExchangeMaxCount = fromCylinder->queryMaxCount(INDEX_WHEREEVER, toItem, toItem->getItemCount(), maxExchangeQueryCount, 0);

			if (retExchangeMaxCount != RETURNVALUE_NOERROR && maxExchangeQueryCount == 0) {
				return retExchangeMaxCount;
			}

			if (toCylinder->queryRemove(toItem, toItem->getItemCount(), flags, actor) == RETURNVALUE_NOERROR) {
				int32_t oldToItemIndex = toCylinder->getThingIndex(toItem);
				toCylinder->removeThing(toItem, toItem->getItemCount());
				fromCylinder->addThing(toItem);

				if (oldToItemIndex != -1) {
					toCylinder->postRemoveNotification(toItem, fromCylinder, oldToItemIndex);
				}

				int32_t newToItemIndex = fromCylinder->getThingIndex(toItem);
				if (newToItemIndex != -1) {
					fromCylinder->postAddNotification(toItem, toCylinder, newToItemIndex);
				}

				ret = toCylinder->queryAdd(index, item, count, flags);
				toItem = nullptr;
			}
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	// check how much we can move
	uint32_t maxQueryCount = 0;
	ReturnValue retMaxCount = toCylinder->queryMaxCount(index, item, count, maxQueryCount, flags);
	if (retMaxCount != RETURNVALUE_NOERROR && maxQueryCount == 0) {
		return retMaxCount;
	}

	uint32_t m;
	if (item->isStackable()) {
		m = std::min<uint32_t>(count, maxQueryCount);
	} else {
		m = maxQueryCount;
	}

	std::shared_ptr<Item> moveItem = item;
	// check if we can remove this item
	ret = fromCylinder->queryRemove(item, m, flags, actor);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (tradeItem) {
		if (toCylinder->getItem() == tradeItem) {
			return RETURNVALUE_NOTENOUGHROOM;
		}

		std::shared_ptr<Cylinder> tmpCylinder = toCylinder->getParent();
		while (tmpCylinder) {
			if (tmpCylinder->getItem() == tradeItem) {
				return RETURNVALUE_NOTENOUGHROOM;
			}

			tmpCylinder = tmpCylinder->getParent();
		}
	}

	// remove the item
	int32_t itemIndex = fromCylinder->getThingIndex(item);
	std::shared_ptr<Item> updateItem = nullptr;
	fromCylinder->removeThing(item, m);

	// update item(s)
	if (item->isStackable()) {
		uint32_t n;

		if (toItem && item->equals(toItem)) {
			n = std::min<uint32_t>(toItem->getStackSize() - toItem->getItemCount(), m);
			toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);
			updateItem = toItem;
		} else {
			n = 0;
		}

		int32_t newCount = m - n;
		if (newCount > 0) {
			moveItem = item->clone();
			moveItem->setItemCount(newCount);
		} else {
			moveItem = nullptr;
		}

		if (item->isRemoved()) {
			item->stopDecaying();
		}
	}

	// add item
	if (moveItem /*m - n > 0*/) {
		toCylinder->addThing(index, moveItem);
	}

	if (itemIndex != -1) {
		fromCylinder->postRemoveNotification(item, toCylinder, itemIndex);
	}

	if (moveItem) {
		int32_t moveItemIndex = toCylinder->getThingIndex(moveItem);
		if (moveItemIndex != -1) {
			toCylinder->postAddNotification(moveItem, fromCylinder, moveItemIndex);
		}
		moveItem->startDecaying();
	}

	if (updateItem) {
		int32_t updateItemIndex = toCylinder->getThingIndex(updateItem);
		if (updateItemIndex != -1) {
			toCylinder->postAddNotification(updateItem, fromCylinder, updateItemIndex);
		}
		updateItem->startDecaying();
	}

	if (movedItem) {
		if (moveItem) {
			*movedItem = moveItem;
		} else {
			*movedItem = item;
		}
	}

	std::shared_ptr<Item> quiver = toCylinder->getItem();
	if (quiver && quiver->isQuiver()
	    && quiver->getHoldingPlayer()
	    && quiver->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == quiver) {
		quiver->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, quiver);
	} else {
		quiver = fromCylinder->getItem();
		if (quiver && quiver->isQuiver()
		    && quiver->getHoldingPlayer()
		    && quiver->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == quiver) {
			quiver->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, quiver);
		}
	}

	if (SoundEffect_t soundEffect = item->getMovementSound(toCylinder);
	    toCylinder && soundEffect != SoundEffect_t::SILENCE) {
		if (toCylinder->getContainer() && actor && actor->getPlayer() && (toCylinder->getContainer()->isInsideDepot(true) || toCylinder->getContainer()->getHoldingPlayer())) {
			actor->getPlayer()->sendSingleSoundEffect(toCylinder->getPosition(), soundEffect, SourceEffect_t::OWN);
		} else {
			game_.sendSingleSoundEffect(toCylinder->getPosition(), soundEffect, actor);
		}
	}

	// we could not move all, inform the player
	if (item->isStackable() && maxQueryCount < count) {
		return retMaxCount;
	}

	auto fromContainer = fromCylinder ? fromCylinder->getContainer() : nullptr;
	auto toContainer = toCylinder ? toCylinder->getContainer() : nullptr;
	auto player = actor ? actor->getPlayer() : nullptr;
	if (player) {
		// Update containers
		player->onSendContainer(toContainer);
		player->onSendContainer(fromContainer);
	}

	// Actor related actions
	if (fromCylinder && actor && toCylinder) {
		if (!fromContainer || !toContainer || !player) {
			return ret;
		}

		if (const auto &playerActor = actor->getPlayer()) {
			// Refresh depot search window if necessary
			if (playerActor->isDepotSearchOpenOnItem(item->getID()) && ((fromCylinder->getItem() && fromCylinder->getItem()->isInsideDepot(true)) || (toCylinder->getItem() && toCylinder->getItem()->isInsideDepot(true)))) {
				playerActor->requestDepotSearchItem(item->getID(), item->getTier());
			}

			const ItemType &it = Item::items[fromCylinder->getItem()->getID()];
			if (it.id <= 0) {
				return ret;
			}

			// Looting analyser
			if (it.isCorpse && toContainer->getTopParent() == playerActor && item->getIsLootTrackeable()) {
				playerActor->sendLootStats(item, static_cast<uint8_t>(item->getItemCount()));
			}
		}
	}

	return ret;
}

ReturnValue ItemService::internalAddItem(std::shared_ptr<Cylinder> toCylinder, const std::shared_ptr<Item> &item, int32_t index /*= INDEX_WHEREEVER*/, uint32_t flags /* = 0*/, bool test /* = false*/) {
	uint32_t remainderCount = 0;
	return internalAddItem(std::move(toCylinder), item, index, flags, test, remainderCount);
}

ReturnValue ItemService::internalAddItem(std::shared_ptr<Cylinder> toCylinder, const std::shared_ptr<Item> &item, int32_t index, uint32_t flags, bool test, uint32_t &remainderCount) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (toCylinder == nullptr) {
		g_logger().error("[{}] fromCylinder is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}
	if (item == nullptr) {
		g_logger().error("[{}] item is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}

	auto addedItem = toCylinder->getItem();

	std::shared_ptr<Cylinder> destCylinder = toCylinder;
	std::shared_ptr<Item> toItem = nullptr;
	toCylinder = toCylinder->queryDestination(index, item, toItem, flags);

	// check if we can add this item
	ReturnValue ret = toCylinder->queryAdd(index, item, item->getItemCount(), flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	/*
	Check if we can move add the whole amount, we do this by checking against the original cylinder,
	since the queryDestination can return a cylinder that might only hold a part of the full amount.
	*/
	uint32_t maxQueryCount = 0;
	ret = destCylinder->queryMaxCount(INDEX_WHEREEVER, item, item->getItemCount(), maxQueryCount, flags);

	if (ret != RETURNVALUE_NOERROR && addedItem && addedItem->getID() != ITEM_REWARD_CONTAINER) {
		return ret;
	}

	if (test) {
		return RETURNVALUE_NOERROR;
	}

	if (item->isStackable() && item->equals(toItem)) {
		uint32_t m = std::min<uint32_t>(item->getItemCount(), maxQueryCount);
		uint32_t n = std::min<uint32_t>(toItem->getStackSize() - toItem->getItemCount(), m);

		toCylinder->updateThing(toItem, toItem->getID(), toItem->getItemCount() + n);

		int32_t count = m - n;
		if (count > 0) {
			if (item->getItemCount() != count) {
				std::shared_ptr<Item> remainderItem = item->clone();
				remainderItem->setItemCount(count);
				if (internalAddItem(destCylinder, remainderItem, INDEX_WHEREEVER, flags, false) != RETURNVALUE_NOERROR) {
					remainderCount = count;
				}
			} else {
				toCylinder->addThing(index, item);

				int32_t itemIndex = toCylinder->getThingIndex(item);
				if (itemIndex != -1) {
					toCylinder->postAddNotification(item, nullptr, itemIndex);
				}
			}
		} else {
			// fully merged with toItem, item will be destroyed
			item->onRemoved();

			int32_t itemIndex = toCylinder->getThingIndex(toItem);
			if (itemIndex != -1) {
				toCylinder->postAddNotification(toItem, nullptr, itemIndex);
			}
		}
	} else {
		toCylinder->addThing(index, item);

		int32_t itemIndex = toCylinder->getThingIndex(item);
		if (itemIndex != -1) {
			toCylinder->postAddNotification(item, nullptr, itemIndex);
		}
	}

	if (addedItem && addedItem->isQuiver()
	    && addedItem->getHoldingPlayer()
	    && addedItem->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == addedItem) {
		addedItem->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, addedItem);
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue ItemService::internalRemoveItem(const std::shared_ptr<Item> &items, int32_t count /*= -1*/, bool test /*= false*/, uint32_t flags /*= 0*/, bool force /*= false*/) {
	auto item = items;
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (item == nullptr) {
		g_logger().debug("{} - Item is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}
	std::shared_ptr<Cylinder> cylinder = item->getParent();
	if (cylinder == nullptr) {
		g_logger().debug("{} - Cylinder is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}
	const auto &fromTile = cylinder->getTile();
	if (fromTile) {
		if (fromTile && game_.browseFields.contains(fromTile) && game_.browseFields[fromTile].lock() == cylinder) {
			cylinder = fromTile;
		}
	}
	if (count == -1) {
		count = item->getItemCount();
	}

	ReturnValue ret = cylinder->queryRemove(item, count, flags | FLAG_IGNORENOTMOVABLE);
	if (!force && ret != RETURNVALUE_NOERROR) {
		g_logger().debug("{} - Failed to execute query remove", __FUNCTION__);
		return ret;
	}
	if (!force && !item->canRemove()) {
		g_logger().debug("{} - Failed to remove item", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}

	// Not remove item with decay loaded from map
	if (!force && item->canDecay() && cylinder->getTile() && item->isLoadedFromMap()) {
		g_logger().debug("Cannot remove item with id {}, name {}, on position {}", item->getID(), item->getName(), cylinder->getPosition().toString());
		item->stopDecaying();
		return RETURNVALUE_THISISIMPOSSIBLE;
	}

	if (!test) {
		item->playerUpdateSupplyTracker();
		int32_t index = cylinder->getThingIndex(item);
		// remove the item
		cylinder->removeThing(item, count);

		if (item->isRemoved()) {
			item->onRemoved();
			item->stopDecaying();
		}

		cylinder->postRemoveNotification(item, nullptr, index);
	}

	const auto &quiver = cylinder->getItem();
	if (quiver && quiver->isQuiver()
	    && quiver->getHoldingPlayer()
	    && quiver->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == quiver) {
		quiver->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, quiver);
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue ItemService::internalPlayerAddItem(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item, bool dropOnMap /*= true*/, Slots_t slot /*= CONST_SLOT_WHEREEVER*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	uint32_t remainderCount = 0;
	ReturnValue ret;
	if (slot == CONST_SLOT_WHEREEVER) {
		ret = game_.internalCollectManagedItems(player, item, game_.getObjectCategory(item), false);
		// If cannot place it in the obtain containers, will add it normally
		if (ret != RETURNVALUE_NOERROR) {
			ret = internalAddItem(player, item, slot, 0, false, remainderCount);
		}
	} else {
		ret = internalAddItem(player, item, slot, 0, false, remainderCount);
	}
	if (remainderCount != 0) {
		std::shared_ptr<Item> remainderItem = Item::CreateItem(item->getID(), remainderCount);
		ReturnValue remaindRet = internalAddItem(player->getTile(), remainderItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
		if (remaindRet != RETURNVALUE_NOERROR) {
			player->sendLootStats(item, static_cast<uint8_t>(item->getItemCount()));
		}
	}

	if (ret != RETURNVALUE_NOERROR && dropOnMap) {
		ret = internalAddItem(player->getTile(), item, INDEX_WHEREEVER, FLAG_NOLIMIT);
	}

	if (ret == RETURNVALUE_NOERROR) {
		player->sendForgingData();
	}

	return ret;
}

std::shared_ptr<Item> ItemService::findItemOfType(const std::shared_ptr<Cylinder> &cylinder, uint16_t itemId, bool depthSearch /*= true*/, int32_t subType /*= -1*/) const {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (cylinder == nullptr) {
		g_logger().error("[{}] Cylinder is nullptr", __FUNCTION__);
		return nullptr;
	}

	std::vector<std::shared_ptr<Container>> containers;
	for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
		const std::shared_ptr<Thing> &thing = cylinder->getThing(i);
		if (!thing) {
			continue;
		}

		const auto &item = thing->getItem();
		if (!item) {
			continue;
		}

		if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
			return item;
		}

		if (depthSearch) {
			const std::shared_ptr<Container> &container = item->getContainer();
			if (container) {
				containers.push_back(container);
			}
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		const std::shared_ptr<Container> &container = containers[i++];
		for (const auto &item : container->getItemList()) {
			if (item->getID() == itemId && (subType == -1 || subType == item->getSubType())) {
				return item;
			}

			const std::shared_ptr<Container> &subContainer = item->getContainer();
			if (subContainer) {
				containers.push_back(subContainer);
			}
		}
	}
	return nullptr;
}

namespace {

	struct MoneyCollection {
		uint64_t total = 0;
		std::multimap<uint32_t, std::shared_ptr<Item>> moneyMap;
	};

	void collectMoneyFromItem(
		const std::shared_ptr<Item> &item,
		std::vector<std::shared_ptr<Container>> &containers,
		MoneyCollection &collection
	) {
		if (!item) {
			return;
		}

		if (const auto &container = item->getContainer()) {
			containers.push_back(container);
			return;
		}

		const uint32_t worth = item->getWorth();
		if (worth != 0) {
			const auto it = collection.moneyMap.emplace(worth, item);
			collection.total += it->first;
		}
	}

	MoneyCollection collectMoney(const std::shared_ptr<Cylinder> &cylinder) {
		MoneyCollection collection;
		std::vector<std::shared_ptr<Container>> containers;

		for (size_t i = cylinder->getFirstIndex(), j = cylinder->getLastIndex(); i < j; ++i) {
			const auto &thing = cylinder->getThing(i);
			if (!thing) {
				continue;
			}

			collectMoneyFromItem(thing->getItem(), containers, collection);
		}

		size_t i = 0;
		while (i < containers.size()) {
			const auto &container = containers[i];
			++i;
			for (const auto &item : container->getItemList()) {
				collectMoneyFromItem(item, containers, collection);
			}
		}

		return collection;
	}

	bool removeItemFromOwner(
		ItemService &service,
		const std::shared_ptr<Player> &player,
		const std::shared_ptr<Item> &item,
		uint32_t count
	) {
		const auto removeCount = count == 0 ? -1 : static_cast<int32_t>(count);
		const auto ret = player
			? player->removeItem(item, count)
			: service.internalRemoveItem(item, removeCount);
		if (ret != RETURNVALUE_NOERROR) {
			g_logger().error(
				"Game::removeMoney: failed to remove item {} from {} (reason {}).",
				item ? item->getID() : 0,
				player ? player->getName() : "cylinder",
				getReturnMessage(ret)
			);
			return false;
		}
		return true;
	}

	bool deliverChange(
		ItemService &service,
		const std::shared_ptr<Cylinder> &cylinder,
		uint64_t expectedChange,
		uint32_t flags,
		const std::string &playerName
	) {
		auto [addedMoney, returnValue] = service.addMoney(cylinder, expectedChange, flags);
		if (addedMoney < expectedChange
		    && (flags & FLAG_DROPONMAP) == 0
		    && (returnValue == RETURNVALUE_NOTENOUGHCAPACITY
		        || returnValue == RETURNVALUE_NOTENOUGHROOM
		        || returnValue == RETURNVALUE_CONTAINERNOTENOUGHROOM)) {
			std::tie(addedMoney, returnValue) = service.addMoney(cylinder, expectedChange, flags | FLAG_DROPONMAP);
		}

		if (addedMoney < expectedChange) {
			g_logger().error(
				"Game::removeMoney: INCONSISTENT STATE — could not deliver full change to player {}. "
				"Expected change: {}, added: {}. Aborting transaction.",
				playerName, expectedChange, addedMoney
			);
			return false;
		}

		return true;
	}

	enum class MoneyRemovalOutcome {
		Continue,
		Done,
		Failed
	};

	MoneyRemovalOutcome processMoneyEntry(
		ItemService &service,
		const std::shared_ptr<Player> &player,
		const std::shared_ptr<Cylinder> &cylinder,
		const std::shared_ptr<Item> &item,
		uint32_t worth,
		uint64_t &money,
		uint32_t flags
	) {
		using enum MoneyRemovalOutcome;
		if (worth < money) {
			if (!removeItemFromOwner(service, player, item, 0)) {
				return Failed;
			}
			money -= worth;
			return Continue;
		}

		if (worth > money) {
			const uint64_t unitWorth = worth / item->getItemCount();
			const auto removeCount = static_cast<uint32_t>((money + unitWorth - 1) / unitWorth);
			const uint64_t expectedChange = (unitWorth * removeCount) - money;

			if (expectedChange > 0 && !deliverChange(service, cylinder, expectedChange, flags, player ? player->getName() : "unknown")) {
				return Failed;
			}

			if (!removeItemFromOwner(service, player, item, removeCount)) {
				return Failed;
			}

			money = 0;
			return Done;
		}

		if (!removeItemFromOwner(service, player, item, 0)) {
			return Failed;
		}
		money = 0;
		return Done;
	}

	struct AddItemOutcome {
		uint32_t placed = 0;
		uint32_t remainder = 0;
		ReturnValue ret = RETURNVALUE_NOERROR;
	};

	AddItemOutcome addItemToCylinder(
		ItemService &service,
		const std::shared_ptr<Cylinder> &cylinder,
		uint16_t itemId,
		uint16_t count,
		uint32_t flags
	) {
		AddItemOutcome outcome;
		const auto &item = Item::CreateItem(itemId, count);
		outcome.ret = service.internalAddItem(cylinder, item, INDEX_WHEREEVER, flags, false, outcome.remainder);
		if (outcome.ret == RETURNVALUE_NOERROR) {
			outcome.placed = count - outcome.remainder;
		}
		return outcome;
	}

	ReturnValue addCoinStack(
		ItemService &service,
		const std::shared_ptr<Cylinder> &cylinder,
		uint16_t itemId,
		uint16_t count,
		uint64_t unitValue,
		uint32_t flags,
		uint64_t &totalAdded
	) {
		auto outcome = addItemToCylinder(service, cylinder, itemId, count, flags);
		if (outcome.placed > 0) {
			totalAdded += static_cast<uint64_t>(outcome.placed) * unitValue;
		}

		const bool fullyPlaced = outcome.ret == RETURNVALUE_NOERROR && outcome.remainder == 0;
		if (fullyPlaced) {
			return RETURNVALUE_NOERROR;
		}

		if ((flags & FLAG_DROPONMAP) == 0 || !cylinder->getTile()) {
			return outcome.ret != RETURNVALUE_NOERROR ? outcome.ret : RETURNVALUE_NOTENOUGHROOM;
		}

		const auto dropCount = static_cast<uint16_t>(count - outcome.placed);
		if (dropCount == 0) {
			return outcome.ret != RETURNVALUE_NOERROR ? outcome.ret : RETURNVALUE_NOTENOUGHROOM;
		}

		auto dropOutcome = addItemToCylinder(service, cylinder->getTile(), itemId, dropCount, FLAG_NOLIMIT);
		if (dropOutcome.placed > 0) {
			totalAdded += static_cast<uint64_t>(dropOutcome.placed) * unitValue;
		}
		if (dropOutcome.ret != RETURNVALUE_NOERROR) {
			return dropOutcome.ret;
		}
		if (dropOutcome.remainder != 0) {
			return outcome.ret != RETURNVALUE_NOERROR ? outcome.ret : RETURNVALUE_NOTENOUGHROOM;
		}

		return RETURNVALUE_NOERROR;
	}

	ReturnValue addCoins(
		ItemService &service,
		const std::shared_ptr<Cylinder> &cylinder,
		uint16_t itemId,
		uint64_t count,
		uint64_t unitValue,
		uint32_t flags,
		uint64_t &totalAdded
	) {
		while (count > 0) {
			const auto createCount = static_cast<uint16_t>(std::min<uint64_t>(100, count));
			const auto ret = addCoinStack(service, cylinder, itemId, createCount, unitValue, flags, totalAdded);
			if (ret != RETURNVALUE_NOERROR) {
				return ret;
			}
			count -= createCount;
		}

		return RETURNVALUE_NOERROR;
	}

} // namespace

bool ItemService::removeMoney(const std::shared_ptr<Cylinder> &cylinder, uint64_t money, uint32_t flags /*= 0*/, bool useBalance /*= false*/) {
	if (cylinder == nullptr) {
		g_logger().error("[{}] cylinder is nullptr", __FUNCTION__);
		return false;
	}

	if (money == 0) {
		return true;
	}

	const auto collection = collectMoney(cylinder);
	const auto &player = std::dynamic_pointer_cast<Player>(cylinder);
	const uint64_t balance = (useBalance && player) ? player->getBankBalance() : 0;

	if (collection.total + balance < money) {
		return false;
	}

	using enum MoneyRemovalOutcome;
	bool removedItems = false;
	for (const auto &[worth, item] : collection.moneyMap) {
		const auto outcome = processMoneyEntry(*this, player, cylinder, item, worth, money, flags);
		if (outcome == Failed) {
			if (player && removedItems) {
				player->updateState();
			}
			return false;
		}
		if (outcome == Done) {
			removedItems = true;
			break;
		}
		if (outcome == Continue) {
			removedItems = true;
		}
	}

	if (player && removedItems) {
		player->updateState();
	}

	if (money == 0) {
		return true;
	}

	if (useBalance && player && player->getBankBalance() >= money) {
		uint64_t oldBalance = player->getBankBalance();
		player->setBankBalance(oldBalance - money);
		player->sendResourceBalance(RESOURCE_BANK, player->getBankBalance());
		uint64_t newBalance = player->getBankBalance();

		const uint64_t expectedBalance = oldBalance - money;
		if (newBalance != expectedBalance) {
			g_logger().error(
				"Game::removeMoney: inconsistent bank debit for player {}. Old balance: {}, expected: {}, got: {}.",
				player->getName(), oldBalance, expectedBalance, newBalance
			);
		} else {
			g_logger().debug(
				"Game::removeMoney: debited {} gold from player {}'s bank. Old balance: {}, new balance: {}.",
				money, player->getName(), oldBalance, newBalance
			);
		}
	}

	return true;
}

std::pair<uint64_t, ReturnValue> ItemService::addMoney(const std::shared_ptr<Cylinder> &cylinder, uint64_t money, uint32_t flags /*= 0*/) {
	if (cylinder == nullptr) {
		g_logger().error("[{}] cylinder is nullptr", __FUNCTION__);
		return std::make_pair(0, RETURNVALUE_NOTPOSSIBLE);
	}

	if (money == 0) {
		return std::make_pair(0, RETURNVALUE_NOERROR);
	}

	ReturnValue returnValue = RETURNVALUE_NOERROR;
	uint64_t totalAdded = 0;

	uint64_t crystalCoins = money / 10000;
	money -= crystalCoins * 10000;
	returnValue = addCoins(*this, cylinder, ITEM_CRYSTAL_COIN, crystalCoins, 10000, flags, totalAdded);
	if (returnValue != RETURNVALUE_NOERROR) {
		return std::make_pair(totalAdded, returnValue);
	}

	uint64_t platinumCoins = money / 100;
	money -= platinumCoins * 100;
	returnValue = addCoins(*this, cylinder, ITEM_PLATINUM_COIN, platinumCoins, 100, flags, totalAdded);
	if (returnValue != RETURNVALUE_NOERROR) {
		return std::make_pair(totalAdded, returnValue);
	}

	if (money > 0) {
		returnValue = addCoins(*this, cylinder, ITEM_GOLD_COIN, money, 1, flags, totalAdded);
	}

	return std::make_pair(totalAdded, returnValue);
}

std::shared_ptr<Item> ItemService::transformItem(std::shared_ptr<Item> item, uint16_t newId, int32_t newCount /*= -1*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (item->getID() == newId && (newCount == -1 || (newCount == item->getSubType() && newCount != 0))) { // chargeless item placed on map = infinite
		return item;
	}

	std::shared_ptr<Cylinder> cylinder = item->getParent();
	if (cylinder == nullptr) {
		return nullptr;
	}

	std::shared_ptr<Tile> fromTile = cylinder->getTile();
	if (fromTile && game_.browseFields.contains(fromTile) && game_.browseFields[fromTile].lock() == cylinder) {
		cylinder = fromTile;
	}

	int32_t itemIndex = cylinder->getThingIndex(item);
	if (itemIndex == -1) {
		return item;
	}

	if (!item->canTransform()) {
		return item;
	}

	const ItemType &newType = Item::items[newId];
	if (newType.id == 0) {
		return item;
	}

	const ItemType &curType = Item::items[item->getID()];
	if (item->isAlwaysOnTop() != (newType.alwaysOnTopOrder != 0)) {
		// This only occurs when you transform items on tiles from a downItem to a topItem (or vice versa)
		// Remove the old, and add the new
		cylinder->removeThing(item, item->getItemCount());
		cylinder->postRemoveNotification(item, cylinder, itemIndex);

		item->setID(newId);
		if (newCount != -1) {
			item->setSubType(newCount);
		}
		cylinder->addThing(item);

		std::shared_ptr<Cylinder> newParent = item->getParent();
		if (newParent == nullptr) {
			item->stopDecaying();
			return nullptr;
		}

		newParent->postAddNotification(item, cylinder, newParent->getThingIndex(item));
		item->startDecaying();

		return item;
	}

	if (curType.type == newType.type) {
		// Both items has the same type so we can safely change id/subtype
		if (newCount == 0 && (item->isStackable() || item->hasAttribute(ItemAttribute_t::CHARGES))) {
			if (item->isStackable()) {
				internalRemoveItem(item);
				return nullptr;
			} else {
				int32_t newItemId = newId;
				if (curType.id == newType.id) {
					newItemId = curType.decayTo;
				}

				if (newItemId < 0) {
					internalRemoveItem(item);
					return nullptr;
				} else if (newItemId != newId) {
					// Replacing the the old item with the std::make_shared< while> maintaining the old position
					auto newItem = item->transform(newItemId);
					if (newItem == nullptr) {
						g_logger().error("[{}] new item with id {} is nullptr, (ERROR CODE: 01)", __FUNCTION__, newItemId);
						return nullptr;
					}

					return newItem;
				} else {
					return transformItem(item, newItemId);
				}
			}
		} else {
			cylinder->postRemoveNotification(item, cylinder, itemIndex);
			uint16_t itemId = item->getID();
			int32_t count = item->getSubType();

			auto decaying = item->getDecaying();
			// If the item is decaying, we need to transform it to the new item
			if (decaying > DECAYING_FALSE && item->getDuration() <= 1 && newType.decayTo) {
				g_logger().debug("Decay duration old type {}, transformEquipTo {}, transformDeEquipTo {}", curType.decayTo, curType.transformEquipTo, curType.transformDeEquipTo);
				g_logger().debug("Decay duration new type decayTo {}, transformEquipTo {}, transformDeEquipTo {}", newType.decayTo, newType.transformEquipTo, newType.transformDeEquipTo);
				itemId = newType.decayTo;
				item->playerUpdateSupplyTracker();
			} else if (curType.id != newType.id) {
				if (newType.group != curType.group) {
					item->setDefaultSubtype();
				}

				itemId = newId;
			}

			if (newCount != -1 && newType.hasSubType()) {
				count = newCount;
			}

			cylinder->updateThing(item, itemId, count);
			cylinder->postAddNotification(item, cylinder, itemIndex);

			std::shared_ptr<Item> quiver = cylinder->getItem();
			if (quiver && quiver->isQuiver()
			    && quiver->getHoldingPlayer()
			    && quiver->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == quiver) {
				quiver->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, quiver);
			}
			item->startDecaying();

			return item;
		}
	}

	std::shared_ptr<Item> quiver = cylinder->getItem();
	if (quiver && quiver->isQuiver()
	    && quiver->getHoldingPlayer()
	    && quiver->getHoldingPlayer()->getInventoryItem(CONST_SLOT_RIGHT) == quiver) {
		quiver->getHoldingPlayer()->sendInventoryItem(CONST_SLOT_RIGHT, quiver);
	}

	// Replacing the the old item with the new while maintaining the old position
	auto newItem = item->transform(newId, newCount);
	if (newItem == nullptr) {
		g_logger().error("[{}] new item with id {} is nullptr (ERROR CODE: 02)", __FUNCTION__, newId);
		return nullptr;
	}

	return newItem;
}


