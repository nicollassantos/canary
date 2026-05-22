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
#include "items/item.hpp"
#include "items/tile.hpp"
#include "lib/logging/log_with_spd_log.hpp"
#include "lib/metrics/metrics.hpp"
#include "lua/callbacks/event_callback.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"
#include "map/house/housetile.hpp"
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

void MovementService::playerMoveThing(uint32_t playerId, const Position &fromPos, uint16_t itemId, uint8_t fromStackPos, const Position &toPos, uint8_t count) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	// Prevent the player from being able to move the item within the imbuement window
	if (player->hasImbuingItem()) {
		return;
	}

	uint8_t fromIndex = 0;
	if (fromPos.x == 0xFFFF) {
		if (fromPos.y & 0x40) {
			fromIndex = fromPos.z;
		} else if ((fromPos.y == 0x20 || fromPos.y == 0x21) && !player->isDepotSearchOpenOnItem(itemId)) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		} else {
			fromIndex = static_cast<uint8_t>(fromPos.y);
		}
	} else {
		fromIndex = fromStackPos;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, fromPos, fromIndex, itemId, STACKPOS_MOVE);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (const std::shared_ptr<Creature> &movingCreature = thing->getCreature()) {
		const std::shared_ptr<Tile> &tile = game_.map.getTile(toPos);
		if (!tile) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		if (Position::areInRange<1, 1, 0>(movingCreature->getPosition(), player->getPosition())) {
			const auto &task = game_.createPlayerTask(
				config_.getNumber(PUSH_DELAY),
				[this, player, movingCreature, tile] {
					playerMoveCreatureByID(player->getID(), movingCreature->getID(), movingCreature->getPosition(), tile->getPosition());
				},
				__FUNCTION__
			);
			player->setNextActionPushTask(task);
		} else {
			playerMoveCreature(player, movingCreature, movingCreature->getPosition(), tile);
		}
	} else if (thing->getItem()) {
		std::shared_ptr<Cylinder> toCylinder = game_.internalGetCylinder(player, toPos);
		if (!toCylinder) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		playerMoveItem(player, fromPos, itemId, fromStackPos, toPos, count, thing->getItem(), toCylinder);
	}
}

void MovementService::playerMoveItemByPlayerID(uint32_t playerId, const Position &fromPos, uint16_t itemId, uint8_t fromStackPos, const Position &toPos, uint8_t count) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}
	playerMoveItem(player, fromPos, itemId, fromStackPos, toPos, count, nullptr, nullptr);
}

void MovementService::playerMoveItem(const std::shared_ptr<Player> &player, const Position &fromPos, uint16_t itemId, uint8_t fromStackPos, const Position &toPos, uint8_t count, std::shared_ptr<Item> item, std::shared_ptr<Cylinder> toCylinder) {
	if (!player->canDoAction()) {
		const uint32_t delay = player->getNextActionTime();
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId = player->getID(), fromPos, itemId, fromStackPos, toPos, count] {
				playerMoveItemByPlayerID(playerId, fromPos, itemId, fromStackPos, toPos, count);
			},
			__FUNCTION__
		);
		player->setNextActionTask(task);
		return;
	}

	player->setNextActionTask(nullptr);

	if (item == nullptr) {
		uint8_t fromIndex = 0;
		if (fromPos.x == 0xFFFF) {
			if (fromPos.y & 0x40) {
				fromIndex = fromPos.z;
			} else if ((fromPos.y == 0x20 || fromPos.y == 0x21) && !player->isDepotSearchOpenOnItem(itemId)) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			} else {
				fromIndex = static_cast<uint8_t>(fromPos.y);
			}
		} else {
			fromIndex = fromStackPos;
		}

		const auto &thing = game_.internalGetThing(player, fromPos, fromIndex, itemId, STACKPOS_MOVE);
		if (!thing || !thing->getItem()) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		item = thing->getItem();
	}

	if (!item || item->getID() != itemId) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	std::shared_ptr<Cylinder> fromCylinder = nullptr;
	if (fromPos.x == 0xFFFF && (fromPos.y == 0x20 || fromPos.y == 0x21)) {
		if (!player->isDepotSearchOpenOnItem(itemId)) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		fromCylinder = item->getParent();
	} else {
		fromCylinder = game_.internalGetCylinder(player, fromPos);
	}

	if (fromCylinder == nullptr) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (toCylinder == nullptr) {
		toCylinder = game_.internalGetCylinder(player, toPos);
		if (toCylinder == nullptr) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
	}

	// check if we can move this item
	if (auto ret = game_.checkMoveItemToCylinder(player, fromCylinder, toCylinder, item, toPos); ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		return;
	}

	const auto &playerPos = player->getPosition();
	const auto &cylinderTile = fromCylinder->getTile();
	const auto &mapFromPos = cylinderTile ? cylinderTile->getPosition() : item->getPosition();
	if (playerPos.z != mapFromPos.z) {
		player->sendCancelMessage(playerPos.z > mapFromPos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!Position::areInRange<1, 1>(playerPos, mapFromPos)) {
		// need to walk to the item first before using it
		std::vector<Direction> listDir;
		if (player->getPathTo(item->getPosition(), listDir, 0, 1, true, true)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId = player->getID(), fromPos, itemId, fromStackPos, toPos, count] {
					playerMoveItemByPlayerID(playerId, fromPos, itemId, fromStackPos, toPos, count);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	const auto toCylinderTile = toCylinder->getTile();
	const Position &mapToPos = toCylinderTile ? toCylinderTile->getPosition() : toPos;

	// hangable item specific code
	if (item->isHangable() && toCylinderTile && toCylinderTile->hasFlag(TILESTATE_SUPPORTS_HANGABLE)) {
		// destination supports hangable objects so need to move there first
		bool vertical = toCylinderTile->hasProperty(CONST_PROP_ISVERTICAL);
		if (vertical) {
			if (playerPos.x + 1 == mapToPos.x) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		} else { // horizontal
			if (playerPos.y + 1 == mapToPos.y) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				return;
			}
		}

		if (!Position::areInRange<1, 1, 0>(playerPos, mapToPos)) {
			auto walkPos = mapToPos;
			if (vertical) {
				walkPos.x++;
			} else {
				walkPos.y++;
			}

			auto itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && Position::areInRange<1, 1>(mapFromPos, playerPos)
			    && !Position::areInRange<1, 1, 0>(mapFromPos, walkPos)) {
				// need to pickup the item first
				std::shared_ptr<Item> moveItem = nullptr;

				const auto ret = game_.internalMoveItem(fromCylinder, player, INDEX_WHEREEVER, item, count, &moveItem);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				// changing the position since its now in the inventory of the player
				Game::internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkPos, listDir, 0, 0, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					400,
					[this, playerId = player->getID(), itemPos, itemId, itemStackPos, toPos, count] {
						playerMoveItemByPlayerID(playerId, itemPos, itemId, itemStackPos, toPos, count);
					},
					__FUNCTION__
				);
				player->setNextWalkActionTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}
	}

	const auto throwRange = item->getThrowRange();
	if ((Position::getDistanceX(playerPos, mapToPos) > throwRange) || (Position::getDistanceY(playerPos, mapToPos) > throwRange) || (Position::getDistanceZ(mapFromPos, mapToPos) * 4 > throwRange)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		return;
	}

	if (!game_.canThrowObjectTo(mapFromPos, mapToPos)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTTHROW);
		return;
	}

	if (!g_callbacks().checkCallback(EventCallback_t::playerOnMoveItem, player, item, count, fromPos, toPos, fromCylinder, toCylinder)) {
		return;
	}

	if (!g_events().eventPlayerOnMoveItem(player, item, count, fromPos, toPos, fromCylinder, toCylinder)) {
		return;
	}

	uint8_t toIndex = 0;
	if (toPos.x == 0xFFFF) {
		if (toPos.y & 0x40) {
			toIndex = toPos.z;
		} else {
			toIndex = static_cast<uint8_t>(toPos.y);
		}
	}

	if (item->isWrapable() || item->isStoreItem() || (item->hasOwner() && !item->isOwner(player))) {
		const auto toHouseTile = toCylinderTile ? toCylinderTile->dynamic_self_cast<HouseTile>() : nullptr;
		const auto fromHouseTile = cylinderTile ? cylinderTile->dynamic_self_cast<HouseTile>() : nullptr;

		if (fromHouseTile) {
			const auto fromHouse = fromHouseTile->getHouse();
			const auto toHouse = toHouseTile ? toHouseTile->getHouse() : nullptr;
			if (!fromHouse || !toHouse || toHouse->getId() != fromHouse->getId()) {
				player->sendCancelMessage("You cannot move this item out of this house.");
				return;
			}
		}
	}

	if (game_.isTryingToStow(toPos, toCylinder)) {
		player->stowItem(item, count, false);
		return;
	}
	const auto fromContainer = fromCylinder ? fromCylinder->getContainer() : nullptr;
	const auto toContainer = toCylinder ? toCylinder->getContainer() : nullptr;
	const auto fromRoot = fromContainer ? fromContainer->getRootContainer() : nullptr;
	const auto toRoot = toContainer ? toContainer->getRootContainer() : nullptr;
	const bool fromIsStoreInbox = fromContainer && (fromContainer->isStoreInbox() || (fromRoot && fromRoot->isStoreInbox()));
	const bool toIsStoreInbox = toContainer && (toContainer->isStoreInbox() || (toRoot && toRoot->isStoreInbox()));
	const bool isLootPouchStoreInboxReorder = item->getID() == ITEM_GOLD_POUCH && fromIsStoreInbox && toIsStoreInbox;

	if ((!item->isPushable() && !isLootPouchStoreInboxReorder) || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTMOVABLE);
		return;
	}

	const uint32_t moveFlags = isLootPouchStoreInboxReorder ? FLAG_IGNORENOTMOVABLE : 0;
	const auto ret = game_.internalMoveItem(fromCylinder, toCylinder, toIndex, item, count, nullptr, moveFlags, player);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
	} else if (toCylinder->getContainer() && fromCylinder->getContainer() && fromCylinder->getContainer()->countsToLootAnalyzerBalance() && toCylinder->getContainer()->getTopParent() == player) {
		player->sendLootStats(item, count);
	}

	player->cancelPush();

	item->checkDecayMapItemOnMove();

	g_events().eventPlayerOnItemMoved(player, item, count, fromPos, toPos, fromCylinder, toCylinder);
	g_callbacks().executeCallback(EventCallback_t::playerOnItemMoved, player, item, count, fromPos, toPos, fromCylinder, toCylinder);
}
