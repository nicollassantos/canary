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

class Game;
class IConfigManager;
class Creature;
class Player;
struct Outfit_t;
struct Position;

class OutfitService {
public:
	OutfitService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerChangeOutfit(uint32_t playerId, Outfit_t outfit, bool setMount, uint8_t isMountRandomized = 0);
	void playerChangeOutfit(const std::shared_ptr<Player> &player, Outfit_t outfit, bool setMount, uint8_t isMountRandomized = 0);
	void internalCreatureChangeOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit);
	void playerSetMonsterPodium(uint32_t playerId, uint32_t monsterRaceId, const Position &pos, uint8_t stackPos, uint16_t itemId, uint8_t direction, const std::pair<uint8_t, uint8_t> &podiumAndMonsterVisible);
	void playerRotatePodium(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);

private:
	Game &game_;
	IConfigManager &config_;
};
