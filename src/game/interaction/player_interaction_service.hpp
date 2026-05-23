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
#include <string>
#include <vector>

class Game;
class IConfigManager;
class Container;
class House;
class Item;
class Player;
struct Outfit_t;
struct Position;

class PlayerInteractionService {
public:
	PlayerInteractionService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerUseItemEx(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint16_t fromItemId, const Position &toPos, uint8_t toStackPos, uint16_t toItemId);
	void playerUseItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint8_t index, uint16_t itemId);
	void playerUseWithCreature(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint32_t creatureId, uint16_t itemId);

	void playerCloseContainer(uint32_t playerId, uint8_t cid);
	void playerMoveUpContainer(uint32_t playerId, uint8_t cid);
	void playerUpdateContainer(uint32_t playerId, uint8_t cid);

	void playerRotateItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	void playerConfigureShowOffSocket(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	void playerSetShowOffSocket(uint32_t playerId, Outfit_t &outfit, const Position &pos, uint8_t stackPos, uint16_t itemId, uint8_t podiumVisible, uint8_t direction);

	void playerWrapableItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	std::shared_ptr<Item> wrapItem(const std::shared_ptr<Item> &item, const std::shared_ptr<House> &house);
	void unwrapItem(const std::shared_ptr<Item> &item, uint16_t unWrapId, const std::shared_ptr<House> &house, const std::shared_ptr<Player> &player);

	void playerWriteItem(uint32_t playerId, uint32_t windowTextId, const std::string &text);
	void playerBrowseField(uint32_t playerId, const Position &pos);
	void playerStowItem(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackpos, uint8_t count, bool allItems);
	void playerStashWithdraw(uint32_t playerId, uint16_t itemId, uint32_t count, uint8_t stackpos);
	void playerSeekInContainer(uint32_t playerId, uint8_t containerId, uint16_t index, uint8_t containerCategory);
	void playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, const std::string &text);

	void playerApplyImbuement(uint32_t playerId, uint16_t imbuementid, uint8_t slot);
	void playerClearImbuement(uint32_t playerid, uint8_t slot);
	void playerCloseImbuementWindow(uint32_t playerid);
	void playerRequestInventoryImbuements(uint32_t playerId, bool isTrackerOpen);

	void playerRequestAddVip(uint32_t playerId, const std::string &name);
	void playerRequestRemoveVip(uint32_t playerId, uint32_t guid);
	void playerRequestEditVip(uint32_t playerId, uint32_t guid, const std::string &description, uint32_t icon, bool notify, std::vector<uint8_t> vipGroupsId);

	void playerCheckActivity(const std::string &playerName, int interval);

private:
	Game &game_;
	IConfigManager &config_;
};
