/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2024 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_stash_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/containers/depot/depotchest.hpp"
#include "items/containers/depot/depotlocker.hpp"
#include "items/containers/inbox/inbox.hpp"
#include "items/item.hpp"
#include "lib/logging/logger.hpp"
#include "lua/creature/actions.hpp"
#include "lua/creature/movement.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "utils/batch_update.hpp"

namespace {
	uint32_t sendStowItems(const std::shared_ptr<Item> &item, const std::shared_ptr<Item> &stowItem, StashContainerList &itemDict, uint32_t totalItemsToStow, uint32_t maxItemsToStow) {
		uint32_t itemsAdded = 0;

		if (stowItem->getID() == item->getID()) {
			uint32_t stowableToAdd = std::min<uint32_t>(stowItem->getItemAmount(), maxItemsToStow - totalItemsToStow);
			itemDict.emplace_back(stowItem, stowableToAdd);
			itemsAdded += stowableToAdd;
		}

		if (const auto &container = stowItem->getContainer()) {
			for (const auto &[stowableItem, stowableCount] : container->getStowableItems()) {
				if (totalItemsToStow + itemsAdded >= maxItemsToStow) {
					break;
				}

				if (stowableItem->getID() != item->getID()) {
					continue;
				}

				uint32_t stowableToAdd = std::min(stowableCount, maxItemsToStow - (totalItemsToStow + itemsAdded));
				itemDict.emplace_back(stowableItem, stowableToAdd);
				itemsAdded += stowableToAdd;
			}
		}

		return itemsAdded;
	}
} // namespace

bool PlayerStashComponent::isNearDepotBox() const {
	const Position &pos = m_player.getPosition();
	for (int32_t cx = -1; cx <= 1; ++cx) {
		for (int32_t cy = -1; cy <= 1; ++cy) {
			const auto &posTile = g_game().map.getTile(static_cast<uint16_t>(pos.x + cx), static_cast<uint16_t>(pos.y + cy), pos.z);
			if (!posTile) {
				continue;
			}

			if (posTile->hasFlag(TILESTATE_DEPOT)) {
				return true;
			}
		}
	}
	return false;
}

std::shared_ptr<DepotChest> PlayerStashComponent::getDepotChest(uint32_t depotId, bool autoCreate) {
	const auto it = m_player.depotChests.find(depotId);
	if (it != m_player.depotChests.end()) {
		return it->second;
	}

	if (!autoCreate) {
		return nullptr;
	}

	std::shared_ptr<DepotChest> depotChest;
	if (depotId > 0 && depotId < 18) {
		depotChest = std::make_shared<DepotChest>(ITEM_DEPOT_NULL + depotId);
	} else if (depotId == 18) {
		depotChest = std::make_shared<DepotChest>(ITEM_DEPOT_XVIII);
	} else if (depotId == 19) {
		depotChest = std::make_shared<DepotChest>(ITEM_DEPOT_XIX);
	} else {
		depotChest = std::make_shared<DepotChest>(ITEM_DEPOT_XX);
	}

	m_player.depotChests[depotId] = depotChest;
	return depotChest;
}

std::shared_ptr<DepotLocker> PlayerStashComponent::getDepotLocker(uint32_t depotId) {
	const auto it = m_player.depotLockerMap.find(depotId);
	if (it != m_player.depotLockerMap.end()) {
		m_player.inbox->setParent(it->second);
		for (uint32_t i = g_configManager().getNumber(DEPOT_BOXES); i > 0; i--) {
			if (const auto &depotBox = getDepotChest(i, false)) {
				depotBox->setParent(it->second->getItemByIndex(0)->getContainer());
			}
		}
		return it->second;
	}

	const bool createStash = !m_player.client->oldProtocol;

	auto depotLocker = std::make_shared<DepotLocker>(ITEM_LOCKER, createStash ? 4 : 3);
	depotLocker->setDepotId(depotId);
	const auto &marketItem = Item::CreateItem(ITEM_MARKET);
	depotLocker->internalAddThing(marketItem);
	depotLocker->internalAddThing(m_player.inbox);
	if (createStash) {
		const auto &stashPtr = Item::CreateItem(ITEM_STASH);
		depotLocker->internalAddThing(stashPtr);
	}
	const auto &depotChest = Item::CreateItemAsContainer(ITEM_DEPOT, static_cast<uint16_t>(g_configManager().getNumber(DEPOT_BOXES)));
	for (uint32_t i = g_configManager().getNumber(DEPOT_BOXES); i > 0; i--) {
		const auto &depotBox = getDepotChest(i, true);
		depotChest->internalAddThing(depotBox);
		depotBox->setParent(depotChest);
	}
	depotLocker->internalAddThing(depotChest);
	m_player.depotLockerMap[depotId] = depotLocker;
	return depotLocker;
}

void PlayerStashComponent::sendMarketEnter(uint32_t depotId) const {
	if (!m_player.client || m_player.getLastDepotId() == -1 || !depotId) {
		return;
	}
	m_player.client->sendMarketEnter(depotId);
}

void PlayerStashComponent::addItemOnStash(uint16_t itemId, uint32_t amount) {
	const auto it = m_player.stashItems.find(itemId);
	if (it != m_player.stashItems.end()) {
		m_player.stashItems[itemId] += amount;
		return;
	}
	m_player.stashItems[itemId] = amount;
}

uint32_t PlayerStashComponent::getStashItemCount(uint16_t itemId) const {
	const auto it = m_player.stashItems.find(itemId);
	if (it != m_player.stashItems.end()) {
		return it->second;
	}
	return 0;
}

StashItemList PlayerStashComponent::getStashItems() const {
	return m_player.stashItems;
}

ItemsTierCountList PlayerStashComponent::getDepotChestItemsId() const {
	ItemsTierCountList inventoryCache;

	for (const auto &[index, depot] : m_player.depotChests) {
		const auto &container = depot->getContainer();
		if (!container) {
			continue;
		}

		const auto &items = container->getItems(true);
		for (const auto &containerItem : items) {
			if (!containerItem) {
				continue;
			}

			inventoryCache[{ containerItem->getID(), containerItem->getTier() }] += containerItem->getItemAmount();
		}
	}

	return inventoryCache;
}

ItemsTierCountList PlayerStashComponent::getDepotInboxItemsId() const {
	ItemsTierCountList inventoryCache;

	const auto &container = m_player.getInbox();
	if (container) {
		const auto &items = container->getItems(true);
		for (const auto &containerItem : items) {
			if (!containerItem) {
				continue;
			}

			inventoryCache[{ containerItem->getID(), containerItem->getTier() }] += containerItem->getItemAmount();
		}
	}

	return inventoryCache;
}

void PlayerStashComponent::setSpecialMenuAvailable(bool stashBool, bool marketMenuBool, bool depotSearchBool) {
	if (m_player.isDepotSearchOpen() && !depotSearchBool && m_player.depotSearch) {
		m_player.depotSearchOnItem = { 0, 0 };
		sendCloseDepotSearch();
	}

	m_player.m_isStashMenuAvailable = stashBool;
	m_player.marketMenu = marketMenuBool;
	m_player.depotSearch = depotSearchBool;
	if (m_player.client) {
		m_player.client->sendSpecialContainersAvailable();
	}
}

size_t PlayerStashComponent::getMaxDepotItems() const {
	if (m_player.group->maxDepotItems != 0) {
		return m_player.group->maxDepotItems;
	}
	if (m_player.isPremium()) {
		return g_configManager().getNumber(PREMIUM_DEPOT_LIMIT);
	}
	return g_configManager().getNumber(FREE_DEPOT_LIMIT);
}

void PlayerStashComponent::sendOpenStash(bool isNpc) const {
	if (m_player.client && ((m_player.getLastDepotId() != -1) || isNpc)) {
		m_player.client->sendOpenStash();
	}
}

void PlayerStashComponent::beginBatchUpdate() {
	++m_player.m_batching;
}

void PlayerStashComponent::endBatchUpdate() {
	if (!m_player.m_batching) {
		return;
	}

	--m_player.m_batching;
	if (m_player.m_batching > 0) {
		return;
	}

	m_player.updateState();
	m_player.closeContainersOutOfRange();
}

void PlayerStashComponent::sendBatchUpdateContainer(Container* container, bool hasParent) {
	if (!container || !m_player.client) {
		g_logger().warn("PlayerStashComponent::sendBatchUpdateContainer - Invalid container or client for player {}.", m_player.getName());
		return;
	}

	if (!m_player.m_batching) {
		m_player.updateState();
		m_player.closeContainersOutOfRange();
	}

	for (auto &[cid, containerInfo] : m_player.openContainers) {
		if (containerInfo.container.get() != container) {
			continue;
		}

		auto &firstIndex = containerInfo.index;
		const uint32_t containerSize = containerInfo.container->size();
		if (firstIndex >= containerSize) {
			const uint32_t pageSize = std::max<uint32_t>(containerInfo.container->capacity(), 1);
			firstIndex = containerSize == 0 ? 0 : static_cast<uint16_t>(((containerSize - 1) / pageSize) * pageSize);
		}

		m_player.client->sendContainer(cid, containerInfo.container, hasParent, firstIndex);
		g_logger().debug("PlayerStashComponent::sendBatchUpdateContainer - Sent batch update for container {} to player {}.", cid, m_player.getName());
	}
}

void PlayerStashComponent::sendDepotItems(const ItemsTierCountList &itemMap, uint16_t count) const {
	if (m_player.client) {
		m_player.client->sendDepotItems(itemMap, count);
	}
}

void PlayerStashComponent::sendCloseDepotSearch() const {
	if (m_player.client) {
		m_player.client->sendCloseDepotSearch();
	}
}

void PlayerStashComponent::sendDepotSearchResultDetail(uint16_t itemId, uint8_t tier, uint32_t depotCount, const std::vector<std::shared_ptr<Item>> &depotItems, uint32_t inboxCount, const std::vector<std::shared_ptr<Item>> &inboxItems, uint32_t stashCount) const {
	if (m_player.client) {
		m_player.client->sendDepotSearchResultDetail(itemId, tier, depotCount, depotItems, inboxCount, inboxItems, stashCount);
	}
}

ReturnValue PlayerStashComponent::addItemFromStash(uint16_t itemId, uint32_t itemCount) {
	const auto &itemType = Item::items[itemId];
	if (!itemType.id) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	double availableCapacity = m_player.getFreeCapacity();
	double itemWeight = itemType.weight;

	auto maxRetrievableByWeight = static_cast<uint32_t>(availableCapacity / itemWeight);
	uint32_t retrievableCount = std::min(maxRetrievableByWeight, itemCount);

	if (retrievableCount == 0) {
		m_player.sendMessageDialog("Not enough capacity. You could not retrieve any items.");
		return RETURNVALUE_NOTENOUGHROOM;
	}

	const auto &thisPtr = m_player.getPlayer();
	std::vector<std::shared_ptr<Container>> containersCache;
	size_t cacheIndex = 0;
	bool fallbackConsumed = false;
	uint32_t freeStackSpace = 0;
	std::vector<std::shared_ptr<Item>> stackableItemsCache;
	auto objectCategory = g_game().getObjectCategory(itemType);
	const auto &obtainContainer = g_game().findManagedContainer(thisPtr, fallbackConsumed, objectCategory, false);
	if (obtainContainer) {
		if (obtainContainer->capacity() > obtainContainer->size()) {
			containersCache.emplace_back(obtainContainer);
		}

		for (const auto &item : obtainContainer->getItems(true)) {
			const auto &subContainer = item->getContainer();
			if (subContainer && subContainer->capacity() > subContainer->size()) {
				containersCache.emplace_back(subContainer);
			}

			if (item && item->getID() == itemId && item->isStackable()) {
				uint32_t availableSpace = item->getStackSize() - item->getItemCount();
				if (availableSpace > 0) {
					stackableItemsCache.emplace_back(item);
					freeStackSpace += availableSpace;
				}
			}
		}
	} else {
		containersCache = m_player.getAllContainers();
	}

	uint32_t maxBySlots = 0;
	if (itemType.stackable) {
		uint32_t freeSlots = m_player.getFreeBackpackSlots();
		maxBySlots = freeStackSpace + (freeSlots * itemType.stackSize);
	} else {
		maxBySlots = m_player.getFreeBackpackSlots();
	}

	uint32_t finalRetrievable = std::min({ maxBySlots, retrievableCount });

	bool canAddItems = false;
	if (itemType.stackable) {
		uint32_t totalSpace = freeStackSpace;
		for (const auto &container : containersCache) {
			uint32_t freeContainerSlots = container->capacity() - container->size();
			totalSpace += freeContainerSlots * itemType.stackSize;
		}
		canAddItems = totalSpace >= finalRetrievable;
	} else {
		uint32_t totalFreeSlots = 0;
		for (const auto &container : containersCache) {
			totalFreeSlots += container->capacity() - container->size();
		}
		canAddItems = totalFreeSlots >= finalRetrievable;
	}

	if (!canAddItems) {
		m_player.sendMessageDialog("Not enough space. You could not retrieve any items.");
		return RETURNVALUE_NOTENOUGHROOM;
	}

	if (!m_player.withdrawItem(itemId, finalRetrievable)) {
		g_logger().warn("Failed to remove itemId: {} from stash, to player: {}, requested: {}", itemId, m_player.getName(), finalRetrievable);
		return RETURNVALUE_NOTPOSSIBLE;
	}

	uint32_t addedItemCount = 0;
	uint32_t remainingToRetrieve = finalRetrievable;
	BatchUpdate batchUpdate(m_player.getPlayer());
	batchUpdate.addContainers(containersCache);

	if (itemType.stackable) {
		while (remainingToRetrieve > 0 && availableCapacity >= itemWeight) {
			uint32_t addValue = std::min<uint32_t>(itemType.stackSize, remainingToRetrieve);
			remainingToRetrieve -= addValue;

			bool itemAdded = false;

			for (auto it = stackableItemsCache.begin(); it != stackableItemsCache.end();) {
				auto &stackableItem = *it;
				if (addValue == 0) {
					break;
				}

				uint32_t spaceInStack = stackableItem->getStackSize() - stackableItem->getItemCount();
				uint32_t stackableCount = std::min(spaceInStack, addValue);

				if (stackableCount > 0) {
					stackableItem->getParent()->updateThing(
						stackableItem, stackableItem->getID(),
						stackableItem->getItemCount() + stackableCount
					);
					addValue -= stackableCount;
					addedItemCount += stackableCount;
					itemAdded = true;
					availableCapacity -= stackableCount * itemWeight;

					if (stackableItem->getItemCount() >= stackableItem->getStackSize()) {
						it = stackableItemsCache.erase(it);
						continue;
					}
				}
				++it;
			}

			while (addValue > 0 && cacheIndex < containersCache.size()) {
				const auto &targetContainer = containersCache[cacheIndex];
				if (!targetContainer) {
					++cacheIndex;
					continue;
				}

				if (targetContainer->capacity() > targetContainer->size()) {
					uint32_t toCreate = std::min<uint32_t>(addValue, itemType.stackSize);
					if (availableCapacity < toCreate * itemWeight) {
						break;
					}

					const auto &newItem = Item::createItemBatch(itemId, toCreate, 0);
					if (!newItem) {
						g_logger().warn("[addItemFromStash] Failed to create new stackable itemId: {} for player {}", itemId, m_player.getName());
						break;
					}

					targetContainer->addThing(newItem);
					if (!m_player.isBatching()) {
						m_player.onSendContainer(targetContainer);
					}
					addedItemCount += toCreate;
					availableCapacity -= toCreate * itemWeight;
					addValue -= toCreate;
					itemAdded = true;
				}

				if (targetContainer->capacity() <= targetContainer->size()) {
					++cacheIndex;
				}
			}

			if (!itemAdded && addValue > 0) {
				g_logger().warn("No more space available for itemId: {}, remaining: {}", itemId, addValue);
				break;
			}
		}
	}

	if (!itemType.stackable) {
		while (finalRetrievable > 0 && cacheIndex < containersCache.size()) {
			auto &targetContainer = containersCache[cacheIndex];
			if (!targetContainer) {
				++cacheIndex;
				continue;
			}

			if (targetContainer->capacity() > targetContainer->size()) {
				const auto &newItem = Item::createItemBatch(itemId, 1, 0);
				if (!newItem) {
					g_logger().warn("[addItemFromStash] Failed to create new itemId: {} for player {}", itemId, m_player.getName());
					break;
				}

				targetContainer->addThing(newItem);
				if (!m_player.isBatching()) {
					m_player.onSendContainer(targetContainer);
				}
				addedItemCount += 1;
				finalRetrievable -= 1;
			}

			if (targetContainer->capacity() <= targetContainer->size()) {
				++cacheIndex;
			}
		}
	}

	std::string itemName = itemType.name + (addedItemCount > 1 ? "s" : "");
	m_player.sendTextMessage(MESSAGE_STATUS, fmt::format("Retrieved {}x {}.", addedItemCount, itemName));

	if (!m_player.isDepotSearchOpenOnItem(itemId)) {
		sendOpenStash();
	}

	if (addedItemCount > 0) {
		m_player.updateState();
	}

	availableCapacity = m_player.getFreeCapacity();
	bool limitedByCapacity = (addedItemCount < itemCount) && (availableCapacity < itemWeight);
	bool limitedBySlots = (addedItemCount < retrievableCount) && !limitedByCapacity;

	if (limitedByCapacity) {
		m_player.sendMessageDialog("Not enough capacity. You could not retrieve all items.");
	} else if (limitedBySlots) {
		m_player.sendMessageDialog("Not enough space. You could not retrieve all items.");
	}

	return addedItemCount > 0 ? RETURNVALUE_NOERROR : RETURNVALUE_NOTENOUGHROOM;
}

ReturnValue PlayerStashComponent::addItemBatchToPaginedContainer(
	const std::shared_ptr<Container> &container,
	uint16_t itemId,
	uint32_t totalCount,
	uint32_t &actuallyAdded,
	uint32_t flags /*= 0*/,
	uint8_t tier /*= 0*/,
	bool testOnly /*= false*/
) {
	actuallyAdded = 0;

	if (!container) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (totalCount == 0) {
		return RETURNVALUE_NOERROR;
	}

	const auto &itemType = Item::items[itemId];
	if (!itemType.id) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	uint32_t maxStackSize = itemType.stackable && itemType.stackSize > 0 ? itemType.stackSize : 1;
	std::shared_ptr<Item> mergeProbeItem;
	if (itemType.stackable) {
		mergeProbeItem = Item::createItemBatch(itemId, 1, 0);
		if (!mergeProbeItem) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		if (tier > 0) {
			mergeProbeItem->setTier(tier);
		}
	}

	const auto canMergeWithExistingStack = [&](const std::shared_ptr<Item> &existingItem) {
		return existingItem
			&& mergeProbeItem
			&& existingItem->isStackable()
			&& existingItem->equals(mergeProbeItem)
			&& existingItem->getItemCount() < existingItem->getStackSize();
	};

	struct StackMergeSnapshot {
		std::shared_ptr<Item> item;
		std::shared_ptr<Cylinder> parent;
		uint32_t count = 0;
	};

	std::vector<StackMergeSnapshot> stackMergeSnapshots;
	std::vector<std::shared_ptr<Item>> addedItems;

	auto rollbackBatchChanges = [&]() {
		for (auto it = addedItems.rbegin(); it != addedItems.rend(); ++it) {
			const auto &addedItem = *it;
			if (!addedItem || !addedItem->getParent()) {
				continue;
			}

			const ReturnValue removeResult = m_player.removeItem(addedItem, addedItem->getItemCount());
			if (removeResult != RETURNVALUE_NOERROR) {
				g_logger().error("{} - Failed to rollback added item {} amount {} for player {}, error code: {}", __FUNCTION__, addedItem->getID(), addedItem->getItemCount(), m_player.getName(), getReturnMessage(removeResult));
			}
		}

		for (auto it = stackMergeSnapshots.rbegin(); it != stackMergeSnapshots.rend(); ++it) {
			if (!it->item || !it->parent) {
				continue;
			}

			it->parent->updateThing(it->item, it->item->getID(), it->count);
		}

		actuallyAdded = 0;
	};

	const auto failAfterBatchMutation = [&](ReturnValue returnValue) {
		if (!testOnly) {
			rollbackBatchChanges();
		}
		return returnValue;
	};

	bool hasReservedCapacity = false;
	uint64_t remainingItemCapacity = 0;
	ReturnValue capacityError = RETURNVALUE_CONTAINERISFULL;

	if (const auto &inboxContainer = std::dynamic_pointer_cast<Inbox>(container)) {
		hasReservedCapacity = true;
		remainingItemCapacity = inboxContainer->getRemainingItemCapacity();
		capacityError = RETURNVALUE_DEPOTISFULL;
	} else if (container->hasPagination()) {
		hasReservedCapacity = true;
		const uint64_t currentItems = container->getItemHoldingCount();
		const uint64_t maxItems = container->getMaxCapacity();
		remainingItemCapacity = currentItems >= maxItems ? 0 : maxItems - currentItems;
	}

	if (hasReservedCapacity) {
		uint64_t mergeableCount = 0;
		for (const auto &existingItem : container->getItemList()) {
			if (!canMergeWithExistingStack(existingItem)) {
				continue;
			}

			mergeableCount += existingItem->getStackSize() - existingItem->getItemCount();
			if (mergeableCount >= totalCount) {
				break;
			}
		}

		const uint64_t remainingAfterMerge = totalCount > mergeableCount ? static_cast<uint64_t>(totalCount) - mergeableCount : 0;
		const uint64_t chunksNeeded = (remainingAfterMerge + maxStackSize - 1) / maxStackSize;
		if (chunksNeeded > remainingItemCapacity) {
			return capacityError;
		}
	}

	std::unique_ptr<BatchUpdate> batchUpdate;
	if (!testOnly && m_player.client) {
		batchUpdate = std::make_unique<BatchUpdate>(m_player.getPlayer());
		batchUpdate->add(container);
	}

	uint32_t remaining = totalCount;
	if (itemType.stackable) {
		for (const auto &existingItem : container->getItemList()) {
			if (remaining == 0) {
				break;
			}

			if (!canMergeWithExistingStack(existingItem)) {
				continue;
			}

			uint32_t spaceInStack = existingItem->getStackSize() - existingItem->getItemCount();
			uint32_t toMerge = std::min(remaining, spaceInStack);
			if (toMerge == 0) {
				continue;
			}

			if (!testOnly) {
				const auto &parent = existingItem->getParent();
				if (!parent) {
					return failAfterBatchMutation(RETURNVALUE_NOTPOSSIBLE);
				}

				const uint32_t previousCount = existingItem->getItemCount();
				stackMergeSnapshots.push_back({ existingItem, parent, previousCount });
				parent->updateThing(existingItem, existingItem->getID(), existingItem->getItemCount() + toMerge);
				if (existingItem->getItemCount() != previousCount + toMerge) {
					return failAfterBatchMutation(RETURNVALUE_NOTPOSSIBLE);
				}
				actuallyAdded += toMerge;
			}

			remaining -= toMerge;
		}
	}

	while (remaining > 0) {
		uint32_t toStack = std::min(remaining, maxStackSize);

		std::shared_ptr<Item> newItem = Item::createItemBatch(itemId, toStack, 0);
		if (!newItem) {
			return failAfterBatchMutation(RETURNVALUE_NOTPOSSIBLE);
		}

		auto charges = newItem->getCharges();
		if (charges > 0) {
			toStack = std::min<uint32_t>(toStack, charges);
		}

		if (tier > 0) {
			newItem->setTier(tier);
		}

		ReturnValue rv = container->queryAdd(INDEX_WHEREEVER, newItem, toStack, flags);
		if (rv != RETURNVALUE_NOERROR) {
			return failAfterBatchMutation(rv);
		}

		if (!testOnly) {
			container->addThing(newItem);
			if (newItem->getParent() != container) {
				return failAfterBatchMutation(RETURNVALUE_NOTPOSSIBLE);
			}

			addedItems.emplace_back(newItem);
			actuallyAdded += toStack;
		}
		remaining -= toStack;
	}

	if (testOnly) {
		return RETURNVALUE_NOERROR;
	}

	if (actuallyAdded != totalCount) {
		return failAfterBatchMutation(RETURNVALUE_NOTPOSSIBLE);
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue PlayerStashComponent::addItemBatch(
	uint16_t itemId,
	uint32_t totalCount,
	uint32_t &actuallyAdded,
	const AddItemBatchOptions &options /*= {}*/
) {
	struct AddItemBatchState {
		uint32_t totalCount;
		uint32_t remaining;
		uint32_t &actuallyAdded;
		ReturnValue returnError = RETURNVALUE_NOERROR;
	};

	actuallyAdded = 0;
	if (totalCount == 0) {
		return RETURNVALUE_NOERROR;
	}

	const auto &itemType = Item::items[itemId];
	if (!itemType.id) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	uint32_t flags = options.flags;
	const bool dropOnMap = options.dropOnMap || ((flags & FLAG_DROPONMAP) != 0);

	if (!itemType.movable && !itemType.pickupable) {
		const auto &tile = m_player.getTile();
		if (!tile) {
			return RETURNVALUE_NOTPOSSIBLE;
		}

		const auto &newItem = Item::createItemBatch(itemId, totalCount, 0, true);
		if (!newItem) {
			g_logger().error("[PlayerStashComponent::addItemBatch] Failed to create non-movable item {} for player {}", itemId, m_player.getName());
			return RETURNVALUE_NOTPOSSIBLE;
		}

		auto queryAdd = tile->queryAdd(0, newItem, totalCount, flags);
		if (queryAdd != RETURNVALUE_NOERROR) {
			g_logger().warn("[PlayerStashComponent::addItemBatch] Could not add item {} to tile for player {}", itemId, m_player.getName());
			return queryAdd;
		}

		tile->addThing(newItem);
		actuallyAdded = totalCount;
		return RETURNVALUE_NOERROR;
	}

	const auto &thisPlayer = m_player.getPlayer();
	BatchUpdate batchUpdate(thisPlayer);

	std::vector<std::shared_ptr<Container>> containersCache;
	bool fallbackConsumed = false;
	auto objectCategory = g_game().getObjectCategory(itemType);
	const auto &obtainContainer = g_game().findManagedContainer(thisPlayer, fallbackConsumed, objectCategory, false);
	if (options.backpackId == ITEM_SHOPPING_BAG && obtainContainer) {
		if (obtainContainer->capacity() > obtainContainer->size()) {
			containersCache.push_back(obtainContainer);
		}

		for (const auto &item : obtainContainer->getItems(true)) {
			const auto &subContainer = item->getContainer();
			if (subContainer && subContainer->capacity() > subContainer->size()) {
				containersCache.push_back(subContainer);
			}
		}
	} else {
		containersCache = m_player.getAllContainers(!itemType.isAmmo());
	}

	auto collectStackableItems = [&](const std::vector<std::shared_ptr<Container>> &containers) {
		std::vector<std::shared_ptr<Item>> stackableItemsCache;
		stackableItemsCache.reserve(128);
		for (const auto &container : containers) {
			for (const auto &item : container->getItemList()) {
				if (item->getID() == itemId && item->isStackable() && item->getItemCount() < item->getStackSize()) {
					stackableItemsCache.push_back(item);
				}
			}
		}
		for (int slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			const auto &invItem = m_player.getInventoryItem(static_cast<Slots_t>(slot));
			if (invItem && invItem->getID() == itemId && invItem->isStackable() && invItem->getItemCount() < invItem->getStackSize()) {
				stackableItemsCache.push_back(invItem);
			}
		}
		return stackableItemsCache;
	};

	auto stackableItemsCache = collectStackableItems(containersCache);

	uint32_t remaining = totalCount;
	const uint32_t maxStackSize = itemType.stackable ? itemType.stackSize : 1;
	const bool hasFreeSlots = std::ranges::any_of(m_player.inventory, [](const auto &it) { return !it; }) || std::ranges::any_of(containersCache, [](const auto &c) { return c && c->capacity() > c->size(); });

	AddItemBatchState state { totalCount, remaining, actuallyAdded };

	auto checkOverflow = [&](const char* errorCode, bool setError) {
		if (state.actuallyAdded <= state.totalCount) {
			return false;
		}

		g_logger().error("[Error code: {}] player: {}, overflow detected: actuallyAdded ({}) > totalCount ({})", errorCode, m_player.getName(), state.actuallyAdded, state.totalCount);
		if (setError) {
			state.returnError = RETURNVALUE_NOTPOSSIBLE;
		}
		return true;
	};

	auto stackExistingItems = [&](std::vector<std::shared_ptr<Item>> &items) {
		for (size_t i = 0; i < items.size() && state.remaining > 0; i++) {
			auto &existingItem = items[i];

			uint32_t spaceLeft = existingItem->getStackSize() - existingItem->getItemCount();
			uint32_t toStack = std::min(spaceLeft, state.remaining);

			const auto &itemParent = existingItem->getParent();
			const auto &itemParentContainer = itemParent ? itemParent->getContainer() : nullptr;
			if (itemParentContainer) {
				batchUpdate.add(itemParentContainer);
				itemParentContainer->updateThing(
					existingItem,
					existingItem->getID(),
					existingItem->getItemCount() + toStack
				);
			} else {
				existingItem->setItemCount(existingItem->getItemCount() + toStack);
			}

			state.actuallyAdded += toStack;
			state.remaining -= toStack;

			if (existingItem->getItemCount() >= existingItem->getStackSize()) {
				items[i] = items.back();
				items.pop_back();
				i--;
			}

			if (checkOverflow("01", true)) {
				return false;
			}
		}
		return true;
	};

	auto addItemsToEmptySlots = [&]() {
		for (uint32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO && state.remaining > 0; ++slot) {
			auto slotType = static_cast<Slots_t>(slot);
			const auto &item = m_player.getInventoryItem(slotType);
			if (item) {
				continue;
			}

			uint32_t toStack = std::min(state.remaining, maxStackSize);
			if (toStack == 0) {
				continue;
			}

			const auto &newItem = Item::createItemBatch(itemId, toStack, options.subType);

			ReturnValue ret = m_player.queryAdd(slot, newItem, toStack, flags);
			if (ret != RETURNVALUE_NOERROR) {
				continue;
			}

			if (options.tier > 0) {
				newItem->setTier(options.tier);
			}

			auto charges = newItem->getCharges();
			if (charges > 0) {
				toStack = std::min<uint32_t>(toStack, charges);
			}

			Slots_t updateSlot = slotType;
			const auto &mainBackpack = m_player.getInventoryItem(CONST_SLOT_BACKPACK);
			if (options.backpackId == ITEM_SHOPPING_BAG && !mainBackpack && options.inBackpacks && m_player.queryAdd(CONST_SLOT_BACKPACK, newItem, toStack, flags) == RETURNVALUE_NOERROR) {
				m_player.addThing(CONST_SLOT_BACKPACK, newItem);
				updateSlot = CONST_SLOT_BACKPACK;
			} else {
				m_player.addThing(slot, newItem);
			}

			const auto equipResult = g_moveEvents().onPlayerEquip(thisPlayer, newItem, updateSlot, false);
			if (equipResult == 0) {
				state.returnError = RETURNVALUE_NOTPOSSIBLE;
				return false;
			}

			state.actuallyAdded += toStack;
			state.remaining -= toStack;

			if (checkOverflow("02", false)) {
				return false;
			}
		}

		return true;
	};

	auto addItemsToContainers = [&]() {
		size_t containerIndex = 0;
		while (!containersCache.empty() && state.remaining > 0) {
			if (containerIndex >= containersCache.size()) {
				break;
			}

			auto &container = containersCache[containerIndex];
			if (container->capacity() <= container->size()) {
				containerIndex++;
				continue;
			}

			uint32_t toStack = std::min(state.remaining, maxStackSize);
			if (!itemType.stackable || itemType.isWrappable()) {
				toStack = 1;
			}
			const auto &newItem = Item::createItemBatch(itemId, toStack, options.subType, true);
			if (options.tier > 0) {
				newItem->setTier(options.tier);
			}

			auto charges = newItem->getCharges();
			if (charges > 0) {
				toStack = std::min<uint32_t>(toStack, charges);
			}

			auto queryAddResult = container->queryAdd(INDEX_WHEREEVER, newItem, toStack, flags);
			if (queryAddResult != RETURNVALUE_NOERROR) {
				g_logger().warn("[PlayerStashComponent::addItemBatch] Failed to add item: {} to container for player: {}, error code: {}", itemId, m_player.getName(), getReturnMessage(queryAddResult));
				state.returnError = queryAddResult;
				return false;
			}

			batchUpdate.add(container);
			container->addThing(newItem);
			state.actuallyAdded += toStack;
			state.remaining -= toStack;

			if (checkOverflow("03", false)) {
				return false;
			}
		}
		return true;
	};

	auto addItemsToBackpacks = [&]() {
		std::shared_ptr<Container> currentBackpack = nullptr;
		while (state.remaining > 0) {
			if (!currentBackpack || currentBackpack->size() >= currentBackpack->capacity()) {
				currentBackpack = Container::create(options.backpackId);
				if (!currentBackpack) {
					g_logger().error("[PlayerStashComponent::addItemBatch] Failed to create backpack for player {}", m_player.getName());
					return false;
				}

				bool addedBackpack = false;
				for (uint32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; ++slot) {
					auto slotType = static_cast<Slots_t>(slot);
					const auto &inventoryItem = m_player.getInventoryItem(slotType);
					if (!inventoryItem && m_player.queryAdd(slot, currentBackpack, 1, flags) == RETURNVALUE_NOERROR) {
						m_player.addThing(slot, currentBackpack);
						addedBackpack = true;
						break;
					}
				}

				if (!addedBackpack) {
					for (auto &container : containersCache) {
						addedBackpack = container->capacity() > container->size()
							&& container->queryAdd(
								   INDEX_WHEREEVER,
								   currentBackpack,
								   currentBackpack->getItemHoldingCount(),
								   FLAG_IGNORENOTMOVABLE
							   ) == RETURNVALUE_NOERROR;
						if (addedBackpack) {
							batchUpdate.add(container);
							container->addThing(currentBackpack);
							containersCache.push_back(currentBackpack);
							break;
						}
					}
				}

				if (!addedBackpack) {
					return false;
				}
			}

			uint32_t itemsToAdd = std::min(state.remaining, maxStackSize);
			if (!itemType.stackable || itemType.isWrappable()) {
				itemsToAdd = 1;
			}
			const auto &newItem = Item::createItemBatch(itemId, itemsToAdd, options.subType, true);
			if (!newItem) {
				g_logger().warn("[PlayerStashComponent::addItemBatch] Failed to create item for player {}", m_player.getName());
				return false;
			}

			if (options.tier > 0) {
				newItem->setTier(options.tier);
			}

			auto returnQueryAdd = currentBackpack->queryAdd(INDEX_WHEREEVER, newItem, 1, flags);
			if (returnQueryAdd != RETURNVALUE_NOERROR) {
				g_logger().warn("[PlayerStashComponent::addItemBatch] Failed to add item to backpack for player: {}, error code: {}", m_player.getName(), getReturnMessage(returnQueryAdd));
				state.returnError = returnQueryAdd;
				return false;
			}

			batchUpdate.add(currentBackpack);
			currentBackpack->addItem(newItem);
			state.remaining -= itemsToAdd;
			state.actuallyAdded += itemsToAdd;

			if (checkOverflow("06", false)) {
				return false;
			}
		}

		return true;
	};

	auto addItemsToTile = [&]() {
		const auto &tile = m_player.getTile();
		while (state.remaining > 0) {
			uint32_t toStack = std::min(state.remaining, maxStackSize);
			const auto &newItem = Item::createItemBatch(itemId, toStack, options.subType, true);
			if (!tile) {
				break;
			}

			auto queryAddResult = tile->queryAdd(0, newItem, toStack, flags);
			if (queryAddResult != RETURNVALUE_NOERROR) {
				g_logger().warn("[PlayerStashComponent::addItemBatch] Failed to add item: {} to tile for player: {}, error code: {}", itemId, m_player.getName(), getReturnMessage(queryAddResult));
				state.returnError = queryAddResult;
				break;
			}

			if (options.tier > 0) {
				newItem->setTier(options.tier);
			}
			auto charges = newItem->getCharges();
			if (charges > 0) {
				toStack = std::min<uint32_t>(toStack, charges);
			}

			tile->addThing(newItem);
			state.actuallyAdded += toStack;
			state.remaining -= toStack;

			if (checkOverflow("07", false)) {
				break;
			}
		}
	};

	if (!hasFreeSlots || !options.inBackpacks) {
		if (!stackExistingItems(stackableItemsCache)) {
			return state.returnError != RETURNVALUE_NOERROR ? state.returnError : RETURNVALUE_NOTPOSSIBLE;
		}
	}

	if (!addItemsToEmptySlots()) {
		return state.returnError != RETURNVALUE_NOERROR ? state.returnError : RETURNVALUE_NOTPOSSIBLE;
	}

	if (!options.inBackpacks && !addItemsToContainers()) {
		return state.returnError != RETURNVALUE_NOERROR ? state.returnError : RETURNVALUE_NOTPOSSIBLE;
	}

	if (options.inBackpacks && !addItemsToBackpacks()) {
		return state.returnError != RETURNVALUE_NOERROR ? state.returnError : RETURNVALUE_NOTPOSSIBLE;
	}

	if (state.remaining > 0 && dropOnMap) {
		addItemsToTile();
	}

	if (state.remaining > 0) {
		g_logger().debug("[PlayerStashComponent::addItemBatch] Player: {} missing slots for: {} items from item: {}", m_player.getName(), state.remaining, itemType.name);
	}

	m_player.updateState();

	if (state.actuallyAdded > 0) {
		return RETURNVALUE_NOERROR;
	}

	if (state.returnError != RETURNVALUE_NOERROR) {
		return state.returnError;
	}

	return RETURNVALUE_NOTENOUGHROOM;
}

bool PlayerStashComponent::processStashItem(const std::shared_ptr<Item> &item, uint16_t itemCount, uint16_t &refreshDepotSearchOnItem) {
	if (!item) {
		return false;
	}

	if (!item->isItemStorable()) {
		return false;
	}

	const auto &selfPlayer = m_player.getPlayer();
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const auto &inventoryItem = m_player.inventory[i];
		if (!inventoryItem) {
			continue;
		}

		if (inventoryItem == item) {
			if (!item->isStackable() || itemCount >= item->getItemCount()) {
				if (g_moveEvents().onPlayerDeEquip(selfPlayer, item, static_cast<Slots_t>(i)) == 0) {
					return false;
				}
			}
		}
	}

	const uint16_t iteratorCID = item->getID();
	bool success = false;

	if (const auto &player = item->getHoldingPlayer()) {
		if (player == selfPlayer) {
			success = (m_player.removeItem(item, itemCount) == RETURNVALUE_NOERROR);
		}
	} else {
		if (const auto &parent = item->getParent()) {
			const auto &parentItem = parent->getItem();
			if (parentItem && parentItem->getID() == ITEM_BROWSEFIELD) {
				const auto &parentTile = parent->getTile();
				if (parentTile) {
					parentTile->removeThing(item, itemCount);
				}
			} else {
				parent->removeThing(item, itemCount);
			}
			success = true;
		}
	}

	if (success) {
		addItemOnStash(iteratorCID, itemCount);
		if (m_player.isDepotSearchOpenOnItem(iteratorCID)) {
			refreshDepotSearchOnItem = iteratorCID;
		}
	}

	return success;
}

void PlayerStashComponent::stashContainer(const std::vector<std::pair<std::shared_ptr<Item>, uint32_t>> &itemDict) {
	StashItemList stashItemDict;
	for (const auto &[item, itemCount] : itemDict) {
		if (!item) {
			continue;
		}

		stashItemDict[item->getID()] = itemCount;
	}

	for (const auto &[itemId, itemCount] : getStashItems()) {
		if (!stashItemDict[itemId]) {
			stashItemDict[itemId] = itemCount;
		} else {
			stashItemDict[itemId] += itemCount;
		}
	}

	uint16_t refreshDepotSearchOnItem = 0;

	uint32_t totalStowed = 0;
	for (const auto &[item, itemCount] : itemDict) {
		if (!item) {
			continue;
		}
		if (processStashItem(item, itemCount, refreshDepotSearchOnItem)) {
			totalStowed += itemCount;
		}
	}

	m_player.updateState();

	if (totalStowed == 0) {
		m_player.sendCancelMessage("Sorry, not possible.");
		return;
	}

	m_player.sendStats();
	m_player.sendInventoryIds();

	std::ostringstream retString;
	retString << "Stowed " << totalStowed << " object" << (totalStowed > 1 ? "s." : ".");
	if (m_player.moved) {
		retString << " Moved " << m_player.movedItems << " object" << (m_player.movedItems > 1 ? "s." : ".");
		m_player.movedItems = 0;
	}
	m_player.sendTextMessage(MESSAGE_STATUS, retString.str());

	if (refreshDepotSearchOnItem != 0) {
		requestDepotSearchItem(refreshDepotSearchOnItem, 0);
	}
}

void PlayerStashComponent::stowItem(const std::shared_ptr<Item> &item, uint32_t count, bool allItems) {
	if (!item || (!item->isItemStorable() && item->getID() != ITEM_GOLD_POUCH)) {
		m_player.sendCancelMessage("This item cannot be stowed here.");
		return;
	}

	if (!item->isItemStorable() && item->getID() != ITEM_GOLD_POUCH) {
		if (!item->getParent()) {
			m_player.sendCancelMessage("This item cannot be stowed here.");
			return;
		}
		if (!item->getParent()->getItem()) {
			m_player.sendCancelMessage("This item cannot be stowed here.");
			return;
		}
		if (item->getParent()->getItem()->getID() != ITEM_GOLD_POUCH) {
			m_player.sendCancelMessage("This item cannot be stowed here.");
			return;
		}
	}

	StashContainerList itemDict;
	uint32_t totalItemsToStow = 0;
	uint32_t maxItemsToStow = g_configManager().getNumber(STASH_MANAGE_AMOUNT);

	if (allItems) {
		if (item->getContainer()) {
			m_player.sendCancelMessage("You cannot stow containers.");
			return;
		}

		if (!item->isInsideDepot(true)) {
			if (const auto &backpack = m_player.getBackpack()) {
				totalItemsToStow += sendStowItems(item, backpack, itemDict, totalItemsToStow, maxItemsToStow);
			}

			if (const auto &itemParent = item->getParent()) {
				if (const auto &lootPouch = itemParent->getItem(); lootPouch && lootPouch->getID() == ITEM_GOLD_POUCH) {
					totalItemsToStow += sendStowItems(item, lootPouch, itemDict, totalItemsToStow, maxItemsToStow);
				}
			}
		}

		const auto &depotLocker = getDepotLocker(m_player.getLastDepotId());
		const auto &[itemVector, itemMap] = requestLockerItems(depotLocker);
		for (const auto &lockerItem : itemVector) {
			if (lockerItem && item->isInsideDepot(true)) {
				totalItemsToStow += sendStowItems(item, lockerItem, itemDict, totalItemsToStow, maxItemsToStow);
			}
		}
	} else if (const auto &container = item->getContainer()) {
		for (const auto &[stowableItem, stowableCount] : container->getStowableItems()) {
			if (totalItemsToStow >= maxItemsToStow) {
				break;
			}

			uint32_t stowableToAdd = std::min(stowableCount, maxItemsToStow - totalItemsToStow);
			itemDict.emplace_back(stowableItem, stowableToAdd);
			totalItemsToStow += stowableToAdd;
		}
	} else {
		uint32_t stowableToAdd = std::min(count, maxItemsToStow - totalItemsToStow);
		itemDict.emplace_back(item, stowableToAdd);
		totalItemsToStow += stowableToAdd;
	}

	if (itemDict.empty()) {
		m_player.sendCancelMessage("There are no stowable items in this container.");
		return;
	}

	if (totalItemsToStow >= maxItemsToStow) {
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You have reached the maximum stow limit of {} items. Try to stow again.", maxItemsToStow));
	}

	stashContainer(itemDict);
}

void PlayerStashComponent::requestDepotItems() {
	ItemsTierCountList inventoryCache;
	uint16_t count = 0;

	const auto &depotLocker = getDepotLocker(m_player.getLastDepotId());
	if (!depotLocker) {
		return;
	}

	for (const auto &locker : depotLocker->getItemList()) {
		const auto &container = locker->getContainer();
		if (!container) {
			continue;
		}

		const auto &items = container->getItems(true);
		for (const auto &containerItem : items) {
			if (!containerItem) {
				continue;
			}

			const uint16_t itemId = containerItem->getID();
			uint8_t itemTier = Item::items[itemId].upgradeClassification > 0
				? containerItem->getTier() + 1
				: 0;

			auto key = std::make_pair(itemId, itemTier);
			auto [it, inserted] = inventoryCache.try_emplace(key, containerItem->getItemAmount());
			if (!inserted) {
				it->second += containerItem->getItemAmount();
			} else {
				count++;
			}
		}
	}

	for (const auto &[itemId, itemCount] : getStashItems()) {
		if (Item::items[itemId].wareId <= 0) {
			g_logger().error("{} - Player {} has an invalid item with ID {} in stash without market flag", __FUNCTION__, m_player.getName(), itemId);
			continue;
		}

		uint8_t itemTier = Item::items[itemId].upgradeClassification > 0 ? 1 : 0;
		auto key = std::make_pair(itemId, itemTier);
		auto [it, inserted] = inventoryCache.try_emplace(key, itemCount);
		if (!inserted) {
			it->second += itemCount;
		} else {
			count++;
		}
	}

	m_player.setDepotSearchIsOpen(1, 0);
	sendDepotItems(inventoryCache, count);
}

void PlayerStashComponent::requestDepotSearchItem(uint16_t itemId, uint8_t tier) {
	ItemVector depotItems;
	ItemVector inboxItems;
	uint32_t depotCount = 0;
	uint32_t inboxCount = 0;
	uint32_t stashCount = 0;

	if (const ItemType &iType = Item::items[itemId];
	    iType.wareId > 0 && tier == 0) {
		stashCount = getStashItemCount(itemId);
	}

	const auto &depotLocker = getDepotLocker(m_player.getLastDepotId());
	if (!depotLocker) {
		return;
	}

	for (const auto &locker : depotLocker->getItemList()) {
		const auto &c = locker->getContainer();
		if (!c || c->empty()) {
			continue;
		}

		inboxItems.reserve(inboxItems.size());
		depotItems.reserve(depotItems.size());

		for (ContainerIterator it = c->iterator(); it.hasNext(); it.advance()) {
			const auto item = *it;
			if (!item || item->getID() != itemId || item->getTier() != tier) {
				continue;
			}

			if (c->isInbox()) {
				if (inboxItems.size() < 255) {
					inboxItems.emplace_back(item);
				}
				inboxCount += Item::countByType(item, -1);
			} else {
				if (depotItems.size() < 255) {
					depotItems.emplace_back(item);
				}
				depotCount += Item::countByType(item, -1);
			}
		}
	}

	m_player.setDepotSearchIsOpen(itemId, tier);
	sendDepotSearchResultDetail(itemId, tier, depotCount, depotItems, inboxCount, inboxItems, stashCount);
}

void PlayerStashComponent::retrieveAllItemsFromDepotSearch(uint16_t itemId, uint8_t tier, bool isDepot) {
	const auto &depotLocker = getDepotLocker(m_player.getLastDepotId());
	if (!depotLocker) {
		return;
	}

	std::vector<std::shared_ptr<Item>> itemsVector;
	for (const auto &locker : depotLocker->getItemList()) {
		const auto &c = locker->getContainer();
		if (!c || c->empty() ||
		    (c->isInbox() && isDepot) ||
		    (!c->isInbox() && !isDepot)) {
			continue;
		}

		for (ContainerIterator it = c->iterator(); it.hasNext(); it.advance()) {
			const auto &item = *it;
			if (!item) {
				continue;
			}

			if (item->getID() == itemId && item->getTier() == m_player.depotSearchOnItem.second) {
				itemsVector.emplace_back(item);
			}
		}
	}

	uint32_t totalCount = 0;
	for (const auto &item : itemsVector) {
		totalCount += item->getItemAmount();
	}

	double freeCap = m_player.getFreeCapacity();
	const ItemType &it = Item::items[itemId];
	double unitWeight = it.weight;

	uint32_t maxByWeight = unitWeight > 0.0
		? static_cast<uint32_t>(freeCap / unitWeight)
		: totalCount;
	uint32_t retrievableCount = std::min(totalCount, maxByWeight);

	uint32_t actuallyRetrieved = 0;
	AddItemBatchOptions options;
	options.tier = tier;
	auto returnValue = addItemBatch(itemId, retrievableCount, actuallyRetrieved, options);

	if (returnValue != RETURNVALUE_NOERROR) {
		m_player.sendCancelMessage(returnValue);
		return;
	}

	if (actuallyRetrieved == 0) {
		m_player.sendCancelMessage(RETURNVALUE_NOTENOUGHCAPACITY);
		return;
	}

	uint32_t totalRemoved = 0;
	for (const auto &item : itemsVector) {
		if (totalRemoved >= actuallyRetrieved) {
			break;
		}
		uint16_t amountToRemove = std::min<uint16_t>(
			static_cast<uint16_t>(actuallyRetrieved - totalRemoved),
			item->getItemAmount()
		);
		if (m_player.removeItem(item, amountToRemove) == RETURNVALUE_NOERROR) {
			totalRemoved += amountToRemove;
		}
	}

	m_player.updateState();

	if (totalRemoved != actuallyRetrieved) {
		g_logger().error(
			"[retrieveAllItemsFromDepotSearch] Removed ({}) != Inserted ({}) for itemId: {}, player: {}",
			totalRemoved, actuallyRetrieved, itemId, m_player.getName()
		);
	}

	if (actuallyRetrieved < retrievableCount) {
		m_player.sendCancelMessage(RETURNVALUE_NOTENOUGHCAPACITY);
	}

	requestDepotSearchItem(itemId, tier);
}

void PlayerStashComponent::openContainerFromDepotSearch(const Position &pos) {
	if (!m_player.isDepotSearchOpen()) {
		m_player.sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &item = getItemFromDepotSearch(m_player.depotSearchOnItem.first, pos);
	if (!item) {
		m_player.sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &container = item->getParent() ? item->getParent()->getContainer() : nullptr;
	if (!container) {
		m_player.sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	g_actions().useItem(m_player.getPlayer(), pos, 0, container, false);
}

std::shared_ptr<Item> PlayerStashComponent::getItemFromDepotSearch(uint16_t itemId, const Position &pos) {
	const auto &depotLocker = getDepotLocker(m_player.getLastDepotId());
	if (!depotLocker) {
		return nullptr;
	}

	uint8_t index = 0;
	for (const auto &locker : depotLocker->getItemList()) {
		const auto &c = locker->getContainer();
		if (!c || c->empty() || (c->isInbox() && pos.y != 0x21) ||
		    (!c->isInbox() && pos.y != 0x20)) {
			continue;
		}

		for (ContainerIterator it = c->iterator(); it.hasNext(); it.advance()) {
			const auto &item = *it;
			if (!item || item->getID() != itemId || item->getTier() != m_player.depotSearchOnItem.second) {
				continue;
			}

			if (pos.z == index) {
				return item;
			}
			index++;
		}
	}

	return nullptr;
}

std::pair<std::vector<std::shared_ptr<Item>>, std::map<uint16_t, std::map<uint8_t, uint32_t>>>
PlayerStashComponent::requestLockerItems(const std::shared_ptr<DepotLocker> &depotLocker, bool sendToClient, uint8_t tier) const {
	if (!depotLocker) {
		g_logger().error("{} - Depot locker is nullptr", __FUNCTION__);
		return {};
	}

	std::map<uint16_t, std::map<uint8_t, uint32_t>> lockerItems;
	std::vector<std::shared_ptr<Item>> itemVector;
	std::vector<std::shared_ptr<Container>> containers { depotLocker };

	for (size_t i = 0; i < containers.size(); ++i) {
		const auto &container = containers[i];

		for (const auto &item : container->getItemList()) {
			const auto &lockerContainers = item->getContainer();
			if (lockerContainers && !lockerContainers->empty()) {
				containers.emplace_back(lockerContainers);
				continue;
			}

			const ItemType &itemType = Item::items[item->getID()];
			if (item->isStoreItem() || itemType.wareId == 0) {
				continue;
			}

			if (lockerContainers && (!itemType.isContainer() || lockerContainers->capacity() != itemType.maxItems)) {
				continue;
			}

			if (!item->hasMarketAttributes() || (!sendToClient && item->getTier() != tier)) {
				continue;
			}

			lockerItems[itemType.wareId][item->getTier()] += Item::countByType(item, -1);
			itemVector.emplace_back(item);
		}
	}

	StashItemList stashToSend = getStashItems();
	for (const auto &[itemId, itemCount] : stashToSend) {
		const ItemType &itemType = Item::items[itemId];
		if (itemType.wareId != 0) {
			lockerItems[itemType.wareId][0] += itemCount;
		}
	}

	return { itemVector, lockerItems };
}

std::pair<std::vector<std::shared_ptr<Item>>, uint16_t>
PlayerStashComponent::getLockerItemsAndCountById(const std::shared_ptr<DepotLocker> &depotLocker, uint8_t tier, uint16_t itemId) const {
	std::vector<std::shared_ptr<Item>> lockerItems;
	const auto &[itemVector, itemMap] = requestLockerItems(depotLocker, false, tier);
	uint16_t totalCount = 0;
	for (const auto &item : itemVector) {
		if (!item || item->getID() != itemId) {
			continue;
		}

		totalCount++;
		lockerItems.emplace_back(item);
	}

	return std::make_pair(lockerItems, totalCount);
}
