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

class Game;
class IConfigManager;
class Creature;
class Player;
class Tile;
class Thing;
struct Position;
enum Direction : uint8_t;
enum ReturnValue : uint16_t;

class MovementService {
public:
	MovementService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	ReturnValue internalMoveCreature(const std::shared_ptr<Creature> &creature, Direction direction, uint32_t flags = 0);
	ReturnValue internalMoveCreature(const std::shared_ptr<Creature> &creature, const std::shared_ptr<Tile> &toTile, uint32_t flags = 0);
	ReturnValue internalTeleport(const std::shared_ptr<Thing> &thing, const Position &newPos, bool pushMove = true, uint32_t flags = 0);

	void playerMoveCreatureByID(uint32_t playerId, uint32_t movingCreatureId, const Position &movingCreatureOrigPos, const Position &toPos);
	void playerMoveCreature(const std::shared_ptr<Player> &player, const std::shared_ptr<Creature> &movingCreature, const Position &movingCreatureOrigPos, const std::shared_ptr<Tile> &toTile);

	void playerAutoWalk(uint32_t playerId, const std::vector<Direction> &listDir);
	void forcePlayerAutoWalk(uint32_t playerId, const std::vector<Direction> &listDir);
	void playerStopAutoWalk(uint32_t playerId);

private:
	Game &game_;
	IConfigManager &config_;
};
