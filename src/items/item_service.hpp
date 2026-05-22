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
#include <utility>

#include "items/cylinder.hpp"
#include "creatures/creatures_definitions.hpp"

class Game;
class IConfigManager;
class Item;
class Creature;
class Player;
enum ReturnValue : uint16_t;

class ItemService {
public:
	ItemService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	ReturnValue internalMoveItem(std::shared_ptr<Cylinder> fromCylinder, std::shared_ptr<Cylinder> toCylinder, int32_t index, const std::shared_ptr<Item> &item, uint32_t count, std::shared_ptr<Item>* movedItem, uint32_t flags = 0, const std::shared_ptr<Creature> &actor = nullptr, const std::shared_ptr<Item> &tradeItem = nullptr, bool checkTile = true);
	ReturnValue internalAddItem(std::shared_ptr<Cylinder> toCylinder, const std::shared_ptr<Item> &item, int32_t index = INDEX_WHEREEVER, uint32_t flags = 0, bool test = false);
	ReturnValue internalAddItem(std::shared_ptr<Cylinder> toCylinder, const std::shared_ptr<Item> &item, int32_t index, uint32_t flags, bool test, uint32_t &remainderCount);
	ReturnValue internalRemoveItem(const std::shared_ptr<Item> &item, int32_t count = -1, bool test = false, uint32_t flags = 0, bool force = false);
	ReturnValue internalPlayerAddItem(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item, bool dropOnMap = true, Slots_t slot = CONST_SLOT_WHEREEVER);
	std::shared_ptr<Item> findItemOfType(const std::shared_ptr<Cylinder> &cylinder, uint16_t itemId, bool depthSearch = true, int32_t subType = -1) const;
	bool removeMoney(const std::shared_ptr<Cylinder> &cylinder, uint64_t money, uint32_t flags = 0, bool useBank = false);
	std::pair<uint64_t, ReturnValue> addMoney(const std::shared_ptr<Cylinder> &cylinder, uint64_t money, uint32_t flags = 0);
	std::shared_ptr<Item> transformItem(std::shared_ptr<Item> item, uint16_t newId, int32_t newCount = -1);

private:
	Game &game_;
	IConfigManager &config_;
};
