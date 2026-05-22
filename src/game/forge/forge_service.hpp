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

class Game;
class IConfigManager;
class Monster;
class Player;
enum class ForgeAction_t : uint8_t;
enum class ForgeClassifications_t : uint8_t;

class ForgeService {
public:
	ForgeService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerForgeFuseItems(uint32_t playerId, ForgeAction_t actionType, uint16_t firstItemId, uint8_t tier, uint16_t secondItemId, bool usedCore, bool reduceTierLoss, bool convergence);
	void playerForgeTransferItemTier(uint32_t playerId, ForgeAction_t actionType, uint16_t donorItemId, uint8_t tier, uint16_t receiveItemId, bool convergence);
	void playerForgeResourceConversion(uint32_t playerId, ForgeAction_t actionType);
	void playerBrowseForgeHistory(uint32_t playerId, uint8_t page);

	uint32_t makeFiendishMonster(uint32_t forgeableMonsterId = 0, bool createForgeableMonsters = false);
	uint32_t makeInfluencedMonster();
	bool removeForgeMonster(uint32_t id, ForgeClassifications_t monsterForgeClassification, bool create);
	void updateForgeableMonsters();
	void createFiendishMonsters();
	void createInfluencedMonsters();
	void checkForgeEventId(uint32_t monsterId);
	bool addInfluencedMonster(const std::shared_ptr<Monster> &monster, uint16_t stack = 0);
	bool addItemStoreInbox(const std::shared_ptr<Player> &player, uint32_t itemId);
	void updateFiendishMonsterStatus(uint32_t monsterId, const std::string &monsterName);

private:
	Game &game_;
	IConfigManager &config_;
};
