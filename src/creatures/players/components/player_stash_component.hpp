/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2024 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/stash_definitions.hpp"

class Player;
class Item;
class Container;
class DepotChest;
class DepotLocker;
class Position;
enum ReturnValue : uint16_t;

class PlayerStashComponent {
public:
	PlayerStashComponent() = delete;
	explicit PlayerStashComponent(Player &player) :
		m_player(player) { }

	// Depot chest/locker access
	bool isNearDepotBox();
	std::shared_ptr<DepotChest> getDepotChest(uint32_t depotId, bool autoCreate);
	std::shared_ptr<DepotLocker> getDepotLocker(uint32_t depotId);

	// Market
	void sendMarketEnter(uint32_t depotId) const;

	// Stash core
	void addItemOnStash(uint16_t itemId, uint32_t amount);
	uint32_t getStashItemCount(uint16_t itemId) const;
	StashItemList getStashItems() const;

	// Depot inventory helpers
	ItemsTierCountList getDepotChestItemsId() const;
	ItemsTierCountList getDepotInboxItemsId() const;

	// Menu availability
	void setSpecialMenuAvailable(bool stashBool, bool marketMenuBool, bool depotSearchBool);
	size_t getMaxDepotItems() const;

	// Send stash
	void sendOpenStash(bool isNpc = false) const;
	void sendDepotItems(const ItemsTierCountList &itemMap, uint16_t count) const;
	void sendCloseDepotSearch() const;
	void sendDepotSearchResultDetail(uint16_t itemId, uint8_t tier, uint32_t depotCount, const std::vector<std::shared_ptr<Item>> &depotItems, uint32_t inboxCount, const std::vector<std::shared_ptr<Item>> &inboxItems, uint32_t stashCount) const;

	// Batch update
	void beginBatchUpdate();
	void endBatchUpdate();
	void sendBatchUpdateContainer(Container* container, bool hasParent);

	// Item retrieval from stash
	ReturnValue addItemFromStash(uint16_t itemId, uint32_t itemCount);

	// Batch item operations
	ReturnValue addItemBatchToPaginedContainer(
		const std::shared_ptr<Container> &container,
		uint16_t itemId,
		uint32_t totalCount,
		uint32_t &actuallyAdded,
		uint32_t flags = 0,
		uint8_t tier = 0,
		bool testOnly = false
	);
	ReturnValue addItemBatch(
		uint16_t itemId,
		uint32_t totalCount,
		uint32_t &actuallyAdded,
		const AddItemBatchOptions &options = {}
	);

	// Stow operations
	void stowItem(const std::shared_ptr<Item> &item, uint32_t count, bool allItems);
	void stashContainer(const std::vector<std::pair<std::shared_ptr<Item>, uint32_t>> &itemDict);

	// Depot search
	void requestDepotItems();
	void requestDepotSearchItem(uint16_t itemId, uint8_t tier);
	void retrieveAllItemsFromDepotSearch(uint16_t itemId, uint8_t tier, bool isDepot);
	void openContainerFromDepotSearch(const Position &pos);
	std::shared_ptr<Item> getItemFromDepotSearch(uint16_t itemId, const Position &pos);

	// Locker helpers
	std::pair<std::vector<std::shared_ptr<Item>>, std::map<uint16_t, std::map<uint8_t, uint32_t>>>
	requestLockerItems(const std::shared_ptr<DepotLocker> &depotLocker, bool sendToClient = false, uint8_t tier = 0) const;

	std::pair<std::vector<std::shared_ptr<Item>>, uint16_t>
	getLockerItemsAndCountById(const std::shared_ptr<DepotLocker> &depotLocker, uint8_t tier, uint16_t itemId) const;

private:
	bool processStashItem(const std::shared_ptr<Item> &item, uint16_t itemCount, uint16_t &refreshDepotSearchOnItem);

	Player &m_player;
};
