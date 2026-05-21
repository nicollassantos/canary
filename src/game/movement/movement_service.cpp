/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/movement/movement_service.hpp"

#include "config/configmanager.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/creature.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "creatures/combat/combat.hpp"
#include "items/tile.hpp"
#include "lib/logging/log_with_spd_log.hpp"
#include "lib/metrics/metrics.hpp"
#include "lua/callbacks/event_callback.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"
#include "map/map.hpp"
#include "utils/definitions.hpp"

ReturnValue MovementService::internalMoveCreature(const std::shared_ptr<Creature> &creature, Direction direction, uint32_t flags /*= 0*/) {
	if (!creature) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (creature->getBaseSpeed() == 0) {
		return RETURNVALUE_NOTMOVABLE;
	}

	creature->setLastPosition(creature->getPosition());
	const Position &currentPos = creature->getPosition();
	Position destPos = getNextPosition(direction, currentPos);
	const auto &player = creature->getPlayer();

	bool diagonalMovement = (direction & DIRECTION_DIAGONAL_MASK) != 0;
	if (player && !diagonalMovement) {
		// try go up
		auto tile = creature->getTile();
		if (currentPos.z != 8 && tile && tile->hasHeight(3)) {
			std::shared_ptr<Tile> tmpTile = game_.map.getTile(currentPos.x, currentPos.y, currentPos.getZ() - 1);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = game_.map.getTile(destPos.x, destPos.y, destPos.getZ() - 1);
				if (tmpTile && tmpTile->getGround() && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;

					if (!tmpTile->hasFlag(TILESTATE_FLOORCHANGE)) {
						player->setDirection(direction);
						destPos.z--;
					}
				}
			}
		}

		// try go down
		if (currentPos.z != 7 && currentPos.z == destPos.z) {
			std::shared_ptr<Tile> tmpTile = game_.map.getTile(destPos.x, destPos.y, destPos.z);
			if (tmpTile == nullptr || (tmpTile->getGround() == nullptr && !tmpTile->hasFlag(TILESTATE_BLOCKSOLID))) {
				tmpTile = game_.map.getTile(destPos.x, destPos.y, destPos.z + 1);
				if (tmpTile && tmpTile->hasHeight(3)) {
					flags |= FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE;
					player->setDirection(direction);
					destPos.z++;
				}
			}
		}
	}

	std::shared_ptr<Tile> toTile = game_.map.getTile(destPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	return internalMoveCreature(creature, toTile, flags);
}

ReturnValue MovementService::internalMoveCreature(const std::shared_ptr<Creature> &creature, const std::shared_ptr<Tile> &toTile, uint32_t flags /*= 0*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (creature->hasCondition(CONDITION_ROOTED)) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	ReturnValue ret = toTile->queryAdd(0, creature, 1, flags);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (creature->hasCondition(CONDITION_FEARED)) {
		std::shared_ptr<MagicField> field = toTile->getFieldItem();
		if (field && !field->isBlocking() && field->getDamage() != 0) {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	game_.map.moveCreature(creature, toTile);
	if (creature->getParent() != toTile) {
		return RETURNVALUE_NOERROR;
	}

	int32_t index = 0;
	std::shared_ptr<Item> toItem = nullptr;
	std::shared_ptr<Tile> subCylinder = nullptr;
	std::shared_ptr<Tile> toCylinder = toTile;
	std::shared_ptr<Tile> fromCylinder = nullptr;
	uint32_t n = 0;

	while ((subCylinder = toCylinder->queryDestination(index, creature, toItem, flags)->getTile()) != toCylinder) {
		if (subCylinder == nullptr) {
			break;
		}

		game_.map.moveCreature(creature, subCylinder);

		if (creature->getParent() != subCylinder) {
			fromCylinder = nullptr;
			break;
		}

		fromCylinder = toCylinder;
		toCylinder = subCylinder;
		flags = 0;

		if (++n >= MAP_MAX_LAYERS) {
			break;
		}
	}

	if (fromCylinder) {
		const Position &fromPosition = fromCylinder->getPosition();
		const Position &toPosition = toCylinder->getPosition();
		if (fromPosition.z != toPosition.z && (fromPosition.x != toPosition.x || fromPosition.y != toPosition.y)) {
			Direction dir = getDirectionTo(fromPosition, toPosition);
			if ((dir & DIRECTION_DIAGONAL_MASK) == 0) {
				game_.internalCreatureTurn(creature, dir);
			}
		}
	}

	return RETURNVALUE_NOERROR;
}

ReturnValue MovementService::internalTeleport(const std::shared_ptr<Thing> &thing, const Position &newPos, bool pushMove /*= true*/, uint32_t flags /*= 0*/) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (thing == nullptr) {
		g_logger().error("[{}] thing is nullptr", __FUNCTION__);
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (newPos == thing->getPosition()) {
		return RETURNVALUE_CONTACTADMINISTRATOR;
	} else if (thing->isRemoved()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	std::shared_ptr<Tile> toTile = game_.map.getTile(newPos);
	if (!toTile) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (std::shared_ptr<Creature> creature = thing->getCreature()) {
		ReturnValue ret = toTile->queryAdd(0, creature, 1, FLAG_NOLIMIT);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		game_.map.moveCreature(creature, toTile, !pushMove);

		return RETURNVALUE_NOERROR;
	} else if (const auto &item = thing->getItem()) {
		return game_.internalMoveItem(item->getParent(), toTile, INDEX_WHEREEVER, item, item->getItemCount(), nullptr, flags);
	}
	return RETURNVALUE_NOTPOSSIBLE;
}

void MovementService::playerMoveCreatureByID(uint32_t playerId, uint32_t movingCreatureId, const Position &movingCreatureOrigPos, const Position &toPos) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &movingCreature = game_.getCreatureByID(movingCreatureId);
	if (!movingCreature) {
		return;
	}

	std::shared_ptr<Tile> toTile = game_.map.getTile(toPos);
	if (!toTile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	playerMoveCreature(player, movingCreature, movingCreatureOrigPos, toTile);
}

void MovementService::playerMoveCreature(const std::shared_ptr<Player> &player, const std::shared_ptr<Creature> &movingCreature, const Position &movingCreatureOrigPos, const std::shared_ptr<Tile> &toTile) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);

	g_dispatcher().addWalkEvent([=, this] {
		if (!player->canDoAction()) {
			const auto &task = game_.createPlayerTask(
				600,
				[this, player, movingCreature, toTile, movingCreatureOrigPos] {
					playerMoveCreatureByID(player->getID(), movingCreature->getID(), movingCreatureOrigPos, toTile->getPosition());
				},
				__FUNCTION__
			);

			player->setNextActionPushTask(task);
			return;
		}

		player->setNextActionTask(nullptr);

		if (!Position::areInRange<1, 1, 0>(movingCreatureOrigPos, player->getPosition())) {
			std::vector<Direction> listDir;
			if (player->getPathTo(movingCreatureOrigPos, listDir, 0, 1, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					600,
					[this, player, movingCreature, toTile, movingCreatureOrigPos] {
						playerMoveCreatureByID(player->getID(), movingCreature->getID(), movingCreatureOrigPos, toTile->getPosition());
					},
					__FUNCTION__
				);
				player->pushEvent(true);
				player->setNextActionPushTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->pushEvent(false);
		std::shared_ptr<Monster> monster = movingCreature->getMonster();
		bool isFamiliar = false;
		if (monster) {
			isFamiliar = monster->isFamiliar();
		}

		if (!isFamiliar && ((!movingCreature->isPushable() && !player->hasFlag(PlayerFlags_t::CanPushAllCreatures)) || (movingCreature->isInGhostMode() && !player->isAccessPlayer()))) {
			player->sendCancelMessage(RETURNVALUE_NOTMOVABLE);
			return;
		}

		const Position &movingCreaturePos = movingCreature->getPosition();
		const Position &toPos = toTile->getPosition();
		if ((Position::getDistanceX(movingCreaturePos, toPos) > movingCreature->getThrowRange()) || (Position::getDistanceY(movingCreaturePos, toPos) > movingCreature->getThrowRange()) || (Position::getDistanceZ(movingCreaturePos, toPos) * 4 > movingCreature->getThrowRange())) {
			player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
			return;
		}

		if (!Position::areInRange<1, 1, 0>(movingCreaturePos, player->getPosition())) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		if (player != movingCreature) {
			if (toTile->hasFlag(TILESTATE_BLOCKPATH)) {
				player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
				return;
			} else if ((movingCreature->getZoneType() == ZONE_PROTECTION && !toTile->hasFlag(TILESTATE_PROTECTIONZONE)) || (movingCreature->getZoneType() == ZONE_NOPVP && !toTile->hasFlag(TILESTATE_NOPVPZONE))) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			} else {
				if (CreatureVector* tileCreatures = toTile->getCreatures()) {
					for (auto &tileCreature : *tileCreatures) {
						if (!tileCreature->isInGhostMode()) {
							player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
							return;
						}
					}
				}

				auto movingNpc = movingCreature->getNpc();
				if (movingNpc && movingNpc->canInteract(toPos)) {
					player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
					return;
				}
			}

			movingCreature->setLastPosition(movingCreature->getPosition());
		}

		if (!g_events().eventPlayerOnMoveCreature(player, movingCreature, movingCreaturePos, toPos)) {
			return;
		}

		if (!g_callbacks().checkCallback(EventCallback_t::playerOnMoveCreature, player, movingCreature, movingCreaturePos, toPos)) {
			return;
		}

		ReturnValue ret = internalMoveCreature(movingCreature, toTile);
		if (ret != RETURNVALUE_NOERROR) {
			player->sendCancelMessage(ret);
		}
		player->setLastPosition(player->getPosition());
	});
}

void MovementService::playerAutoWalk(uint32_t playerId, const std::vector<Direction> &listDir) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->resetLoginProtection();
	player->resetIdleTime();
	player->setNextWalkTask(nullptr);
	player->startAutoWalk(listDir, false);
}

void MovementService::forcePlayerAutoWalk(uint32_t playerId, const std::vector<Direction> &listDir) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->stopEventWalk();
	player->sendCancelTarget();
	player->setFollowCreature(nullptr);
	player->resetLoginProtection();
	player->resetIdleTime();
	player->setNextWalkTask(nullptr);
	player->startAutoWalk(listDir, true);
}

void MovementService::playerStopAutoWalk(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->stopWalk();
}
