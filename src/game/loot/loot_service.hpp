/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "game/game_definitions.hpp"

class Game;
class IConfigManager;
class Container;
class ContainerIterator;
class Item;
class Player;
struct ItemType;
struct Position;
enum ObjectCategory_t : uint8_t;
enum ReturnValue : uint16_t;

class LootService {
public:
	LootService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerQuickLootCorpse(const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &corpse, const Position &position);
	void playerQuickLoot(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackPos, const std::shared_ptr<Item> &defaultItem, bool lootAllCorpses, bool autoLoot);
	void playerLootAllCorpses(const std::shared_ptr<Player> &player, const Position &pos, bool lootAllCorpses);
	void playerLootNearby(uint32_t playerId);

	void playerSetManagedContainer(uint32_t playerId, ObjectCategory_t category, const Position &pos, uint16_t itemId, uint8_t stackPos, bool isLootContainer);
	void playerClearManagedContainer(uint32_t playerId, ObjectCategory_t category, bool isLootContainer);
	void playerOpenManagedContainer(uint32_t playerId, ObjectCategory_t category, bool isLootContainer);
	void playerSetQuickLootFallback(uint32_t playerId, bool fallback);
	void playerQuickLootBlackWhitelist(uint32_t playerId, QuickLootFilter_t filter, const std::vector<uint16_t> &itemIds);

	void playerRequestDepotItems(uint32_t playerId);
	void playerRequestCloseDepotSearch(uint32_t playerId);
	void playerRequestDepotSearchItem(uint32_t playerId, uint16_t itemId, uint8_t tier);
	void playerRequestDepotSearchRetrieve(uint32_t playerId, uint16_t itemId, uint8_t tier, uint8_t type);
	void playerRequestOpenContainerFromDepotSearch(uint32_t playerId, const Position &pos);

	void playerRewardChestCollect(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackPos, uint32_t maxMoveItems = 0);

	ReturnValue collectRewardChestItems(const std::shared_ptr<Player> &player, uint32_t maxMoveItems = 0);
	ReturnValue internalCollectManagedItems(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item, ObjectCategory_t category, bool isLootContainer = true);
	bool tryRetrieveStashItems(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item);

	[[nodiscard]] ObjectCategory_t getObjectCategory(const std::shared_ptr<Item> &item) const;
	[[nodiscard]] ObjectCategory_t getObjectCategory(const ItemType &it) const;

	std::shared_ptr<Container> findManagedContainer(const std::shared_ptr<Player> &player, bool &fallbackConsumed, ObjectCategory_t category, bool isLootContainer);

private:
	std::shared_ptr<Container> findNextAvailableContainer(ContainerIterator &containerIterator, std::shared_ptr<Container> &lootContainer, std::shared_ptr<Container> &lastSubContainer);
	bool handleFallbackLogic(const std::shared_ptr<Player> &player, std::shared_ptr<Container> &lootContainer, ContainerIterator &containerIterator, const bool &fallbackConsumed);
	ReturnValue processMoveOrAddItemToLootContainer(const std::shared_ptr<Item> &item, const std::shared_ptr<Container> &lootContainer, uint32_t &remainderCount, const std::shared_ptr<Player> &player);
	ReturnValue processLootItems(const std::shared_ptr<Player> &player, std::shared_ptr<Container> lootContainer, const std::shared_ptr<Item> &item, bool &fallbackConsumed);

	Game &game_;
	IConfigManager &config_;
};
