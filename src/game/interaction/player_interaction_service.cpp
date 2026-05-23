/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/interaction/player_interaction_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/creatures_definitions.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/iologindata.hpp"
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/actions.hpp"
#include "lua/creature/events.hpp"
#include "map/house/house.hpp"
#include "map/house/housetile.hpp"
#include "map/map.hpp"
#include "utils/tools.hpp"

namespace {
	bool playerCanUseItemOnHouseTile(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item) {
		if (!player || !item) {
			return false;
		}

		// Doors are checked separately (actions.cpp - Action::internalUseItem)
		const auto &itemDoor = item->getDoor();
		if (itemDoor) {
			return true;
		}

		auto itemTile = item->getTile();
		if (!itemTile) {
			return false;
		}

		if (std::shared_ptr<HouseTile> houseTile = std::dynamic_pointer_cast<HouseTile>(itemTile)) {
			const auto &house = houseTile->getHouse();
			if (!house || !house->isInvited(player)) {
				return false;
			}

			auto isGuest = house->getHouseAccessLevel(player) == HOUSE_GUEST;
			auto isOwner = house->getHouseAccessLevel(player) == HOUSE_OWNER;
			auto itemParentContainer = item->getParent() ? item->getParent()->getContainer() : nullptr;
			auto isItemParentContainerBrowseField = itemParentContainer && itemParentContainer->getID() == ITEM_BROWSEFIELD;
			if (isGuest && isItemParentContainerBrowseField) {
				return false;
			}

			auto realItemParent = item->getRealParent();
			auto isItemInGuestInventory = realItemParent && (realItemParent == player || realItemParent->getContainer());
			if (isGuest && !isItemInGuestInventory && !item->isLadder() && !item->canBeUsedByGuests()) {
				return false;
			}

			if (!isOwner && item->isDummy() && (isGuest || item->hasActor())) {
				return false;
			}
		}

		return true;
	}

	bool playerCanUseItemWithOnHouseTile(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item, const Position &toPos, int toStackPos, int toItemId) {
		if (!player || !item) {
			return false;
		}

		auto itemTile = item->getTile();
		if (!itemTile) {
			return false;
		}

		if (g_configManager().getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS)) {
			if (std::shared_ptr<HouseTile> houseTile = std::dynamic_pointer_cast<HouseTile>(itemTile)) {
				const auto &house = houseTile->getHouse();
				std::shared_ptr<Thing> targetThing = g_game().internalGetThing(player, toPos, toStackPos, toItemId, STACKPOS_FIND_THING);
				auto targetItem = targetThing ? targetThing->getItem() : nullptr;
				uint16_t targetId = targetItem ? targetItem->getID() : 0;
				auto invitedCheckUseWith = house && item->getRealParent() && item->getRealParent() != player && (!house->isInvited(player) || house->getHouseAccessLevel(player) == HOUSE_GUEST);
				if (targetId != 0 && targetItem && !targetItem->isDummy() && invitedCheckUseWith && !item->canBeUsedByGuests()) {
					player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
					return false;
				}
			}
		}

		return true;
	}

} // namespace

void PlayerInteractionService::playerUseItemEx(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint16_t fromItemId, const Position &toPos, uint8_t toStackPos, uint16_t toItemId) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (isHotkey && !config_.getBoolean(AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, fromPos, fromStackPos, fromItemId, STACKPOS_FIND_THING);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &item = thing->getItem();
	if (!item || !item->isMultiUse() || item->getID() != fromItemId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	bool canUseHouseItem = !config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) || InternalGame::playerCanUseItemOnHouseTile(player, item);
	if (!canUseHouseItem && item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	} else if (!canUseHouseItem) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	Position walkToPos = fromPos;
	ReturnValue ret = g_actions().canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions().canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	const ItemType &it = Item::items[item->getID()];
	bool canTriggerExhaustion = it.triggerExhaustion();
	if (canTriggerExhaustion) {
		if (player->walkExhausted()) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && toPos.x != 0xFFFF && Position::areInRange<1, 1, 0>(fromPos, player->getPosition()) && !Position::areInRange<1, 1, 0>(fromPos, toPos)) {
				std::shared_ptr<Item> moveItem = nullptr;

				ret = game_.internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(), &moveItem);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				// changing the position since its now in the inventory of the player
				Game::internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					400,
					[this, playerId, itemPos, itemStackPos, fromItemId, toPos, toStackPos, toItemId] {
						playerUseItemEx(playerId, itemPos, itemStackPos, fromItemId, toPos, toStackPos, toItemId);
					},
					__FUNCTION__
				);
				if (canTriggerExhaustion) {
					player->setNextPotionActionTask(task);
				} else {
					player->setNextWalkActionTask(task);
				}
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	bool canDoAction = player->canDoAction();
	if (canTriggerExhaustion) {
		canDoAction = player->canDoPotionAction();
	}

	if (!canDoAction) {
		uint32_t delay = player->getNextActionTime();
		if (canTriggerExhaustion) {
			delay = player->getNextPotionActionTime();
		}
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId, fromPos, fromStackPos, fromItemId, toPos, toStackPos, toItemId] {
				playerUseItemEx(playerId, fromPos, fromStackPos, fromItemId, toPos, toStackPos, toItemId);
			},
			__FUNCTION__
		);
		if (canTriggerExhaustion) {
			player->setNextPotionActionTask(task);
		} else {
			player->setNextActionTask(task);
		}
		return;
	}

	player->resetLoginProtection();
	player->resetIdleTime();
	if (canTriggerExhaustion) {
		player->setNextPotionActionTask(nullptr);
	} else {
		player->setNextActionTask(nullptr);
	}

	// Refresh depot search window if necessary
	bool mustReloadDepotSearch = false;
	if (player->isDepotSearchOpenOnItem(fromItemId)) {
		if (item->isInsideDepot(true)) {
			mustReloadDepotSearch = true;
		} else {
			if (auto targetThing = game_.internalGetThing(player, toPos, toStackPos, toItemId, STACKPOS_FIND_THING);
			    targetThing && targetThing->getItem() && targetThing->getItem()->isInsideDepot(true)) {
				mustReloadDepotSearch = true;
			}
		}
	}

	g_actions().useItemEx(player, fromPos, toPos, toStackPos, item, isHotkey);

	if (mustReloadDepotSearch) {
		player->requestDepotSearchItem(fromItemId, fromStackPos);
	}
}

void PlayerInteractionService::playerUseItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint8_t index, uint16_t itemId) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	bool isHotkey = (pos.x == 0xFFFF && pos.y == 0 && pos.z == 0);
	if (isHotkey && !config_.getBoolean(AIMBOT_HOTKEY_ENABLED)) {
		return;
	}

	const auto &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_FIND_THING);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->isMultiUse() || item->getID() != itemId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	bool canUseHouseItem = !config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) || InternalGame::playerCanUseItemOnHouseTile(player, item);
	if (!canUseHouseItem && item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	} else if (!canUseHouseItem) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	const ItemType &it = Item::items[item->getID()];
	bool canTriggerExhaustion = it.triggerExhaustion();
	if (canTriggerExhaustion) {
		if (player->walkExhausted()) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return;
		}
	}

	ReturnValue ret = g_actions().canUse(player, pos);
	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			std::vector<Direction> listDir;
			if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					400,
					[this, playerId, pos, stackPos, index, itemId] {
						playerUseItem(playerId, pos, stackPos, index, itemId);
					},
					__FUNCTION__
				);
				if (canTriggerExhaustion) {
					player->setNextPotionActionTask(task);
				} else {
					player->setNextWalkActionTask(task);
				}
				return;
			}

			ret = RETURNVALUE_THEREISNOWAY;
		}

		player->sendCancelMessage(ret);
		return;
	}

	bool canDoAction = player->canDoAction();
	if (canTriggerExhaustion) {
		canDoAction = player->canDoPotionAction();
	}

	if (!canDoAction) {
		uint32_t delay = player->getNextActionTime();
		if (canTriggerExhaustion) {
			delay = player->getNextPotionActionTime();
		}
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId, pos, stackPos, index, itemId] {
				playerUseItem(playerId, pos, stackPos, index, itemId);
			},
			__FUNCTION__
		);
		if (canTriggerExhaustion) {
			player->setNextPotionActionTask(task);
		} else {
			player->setNextActionTask(task);
		}
		return;
	}

	player->resetLoginProtection();
	player->resetIdleTime();
	player->setNextActionTask(nullptr);

	// Refresh depot search window if necessary
	bool refreshDepotSearch = false;
	if (player->isDepotSearchOpenOnItem(itemId) && item->isInsideDepot(true)) {
		refreshDepotSearch = true;
	}

	g_actions().useItem(player, pos, index, item, isHotkey);

	if (refreshDepotSearch) {
		player->requestDepotSearchItem(itemId, stackPos);
	}
}

void PlayerInteractionService::playerUseWithCreature(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint32_t creatureId, uint16_t itemId) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const std::shared_ptr<Creature> &creature = game_.getCreatureByID(creatureId);
	if (!creature) {
		return;
	}

	if (!Position::areInRange<7, 5, 0>(creature->getPosition(), player->getPosition())) {
		return;
	}

	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);
	if (!config_.getBoolean(AIMBOT_HOTKEY_ENABLED)) {
		if (creature->getPlayer() || isHotkey) {
			player->sendCancelMessage(RETURNVALUE_DIRECTPLAYERSHOOT);
			return;
		}
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, fromPos, fromStackPos, itemId, STACKPOS_FIND_THING);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &item = thing->getItem();
	if (!item || !item->isMultiUse() || item->getID() != itemId) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	bool canUseHouseItem = !config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) || InternalGame::playerCanUseItemOnHouseTile(player, item);
	if (!canUseHouseItem && item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	} else if (!canUseHouseItem) {
		player->sendCancelMessage(RETURNVALUE_ITEMCANNOTBEMOVEDTHERE);
		return;
	}

	const ItemType &it = Item::items[item->getID()];
	bool canTriggerExhaustion = it.triggerExhaustion();
	if (canTriggerExhaustion) {
		if (player->walkExhausted()) {
			player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
			return;
		}
	}

	const std::shared_ptr<Monster> monster = creature->getMonster();
	if (monster && monster->isFamiliar() && creature->getMaster() && creature->getMaster()->getPlayer() == player && (it.isRune() || it.type == ITEM_TYPE_POTION)) {
		player->setNextPotionAction(OTSYS_TIME() + config_.getNumber(EX_ACTIONS_DELAY_INTERVAL));

		if (it.isMultiUse()) {
			player->sendUseItemCooldown(config_.getNumber(EX_ACTIONS_DELAY_INTERVAL));
		}

		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	Position toPos = creature->getPosition();
	Position walkToPos = fromPos;
	ReturnValue ret = g_actions().canUse(player, fromPos);
	if (ret == RETURNVALUE_NOERROR) {
		ret = g_actions().canUse(player, toPos, item);
		if (ret == RETURNVALUE_TOOFARAWAY) {
			walkToPos = toPos;
		}
	}

	if (ret != RETURNVALUE_NOERROR) {
		if (ret == RETURNVALUE_TOOFARAWAY) {
			Position itemPos = fromPos;
			uint8_t itemStackPos = fromStackPos;

			if (fromPos.x != 0xFFFF && Position::areInRange<1, 1, 0>(fromPos, player->getPosition()) && !Position::areInRange<1, 1, 0>(fromPos, toPos)) {
				std::shared_ptr<Item> moveItem = nullptr;
				ret = game_.internalMoveItem(item->getParent(), player, INDEX_WHEREEVER, item, item->getItemCount(), &moveItem);
				if (ret != RETURNVALUE_NOERROR) {
					player->sendCancelMessage(ret);
					return;
				}

				// changing the position since its now in the inventory of the player
				Game::internalGetPosition(moveItem, itemPos, itemStackPos);
			}

			std::vector<Direction> listDir;
			if (player->getPathTo(walkToPos, listDir, 0, 1, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					400,
					[this, playerId, itemPos, itemStackPos, creatureId, itemId] {
						playerUseWithCreature(playerId, itemPos, itemStackPos, creatureId, itemId);
					},
					__FUNCTION__
				);
				if (canTriggerExhaustion) {
					player->setNextPotionActionTask(task);
				} else {
					player->setNextWalkActionTask(task);
				}
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}
			return;
		}

		player->sendCancelMessage(ret);
		return;
	}

	bool canDoAction = player->canDoAction();
	if (canTriggerExhaustion) {
		canDoAction = player->canDoPotionAction();
	}

	if (!canDoAction) {
		uint32_t delay = player->getNextActionTime();
		if (canTriggerExhaustion) {
			delay = player->getNextPotionActionTime();
		}
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId, fromPos, fromStackPos, creatureId, itemId] {
				playerUseWithCreature(playerId, fromPos, fromStackPos, creatureId, itemId);
			},
			__FUNCTION__
		);
		if (canTriggerExhaustion) {
			player->setNextPotionActionTask(task);
		} else {
			player->setNextActionTask(task);
		}
		return;
	}

	player->resetLoginProtection();
	player->resetIdleTime();
	if (canTriggerExhaustion) {
		player->setNextPotionActionTask(nullptr);
	} else {
		player->setNextActionTask(nullptr);
	}

	g_actions().useItemEx(player, fromPos, creature->getPosition(), creature->getParent()->getThingIndex(creature), item, isHotkey, creature);
}

void PlayerInteractionService::playerCloseContainer(uint32_t playerId, uint8_t cid) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->closeContainer(cid);
	player->sendCloseContainer(cid);
}

void PlayerInteractionService::playerMoveUpContainer(uint32_t playerId, uint8_t cid) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Container> container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	std::shared_ptr<Container> parentContainer = std::dynamic_pointer_cast<Container>(container->getRealParent());
	if (!parentContainer) {
		std::shared_ptr<Tile> tile = container->getTile();
		if (!tile) {
			return;
		}

		if (!g_events().eventPlayerOnBrowseField(player, tile->getPosition())) {
			return;
		}

		if (!g_callbacks().checkCallback(EventCallback_t::playerOnBrowseField, player, tile->getPosition())) {
			return;
		}

		auto it = game_.browseFields.find(tile);
		if (it == game_.browseFields.end() || it->second.expired()) {
			parentContainer = Container::createBrowseField(tile);
			game_.browseFields[tile] = parentContainer;
		} else {
			parentContainer = it->second.lock();
		}
	}

	if (parentContainer->hasOwner() && !parentContainer->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (parentContainer->hasPagination() && parentContainer->hasParent()) {
		uint16_t indexContainer = std::floor(parentContainer->getThingIndex(container) / parentContainer->capacity()) * parentContainer->capacity();
		player->addContainer(cid, parentContainer);

		player->setContainerIndex(cid, indexContainer);
		player->sendContainer(cid, parentContainer, parentContainer->hasParent(), indexContainer);
	} else {
		player->addContainer(cid, parentContainer);
		player->sendContainer(cid, parentContainer, parentContainer->hasParent(), player->getContainerIndex(cid));
	}
}

void PlayerInteractionService::playerUpdateContainer(uint32_t playerId, uint8_t cid) {
	const auto &player = game_.getPlayerByGUID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Container> container = player->getContainerByID(cid);
	if (!container) {
		return;
	}

	player->sendContainer(cid, container, container->hasParent(), player->getContainerIndex(cid));
}

void PlayerInteractionService::playerRotateItem(uint32_t playerId, const Position &pos, uint8_t stackPos, const uint16_t itemId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || !item->isRotatable() || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (pos.x != 0xFFFF && !Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos, stackPos, itemId] {
					playerRotateItem(playerId, pos, stackPos, itemId);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if (!g_callbacks().checkCallback(EventCallback_t::playerOnRotateItem, player, item, pos)) {
		return;
	}

	uint16_t newId = Item::items[item->getID()].rotateTo;
	if (newId != 0) {
		game_.transformItem(item, newId);
	}
}

void PlayerInteractionService::playerConfigureShowOffSocket(uint32_t playerId, const Position &pos, uint8_t stackPos, const uint16_t itemId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || pos.x == 0xFFFF) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || !item->isPodium() || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	bool isPodiumOfRenown = itemId == ITEM_PODIUM_OF_RENOWN1 || itemId == ITEM_PODIUM_OF_RENOWN2;
	if (!Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, false)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			if (isPodiumOfRenown) {
				const auto &task = game_.createPlayerTask(
					400,
					[player, item, pos, itemId, stackPos] {
						player->sendPodiumWindow(item, pos, itemId, stackPos);
					},
					__FUNCTION__
				);
				player->setNextWalkActionTask(task);
			} else {
				const auto &task = game_.createPlayerTask(
					400,
					[player, item, pos, itemId, stackPos] {
						player->sendMonsterPodiumWindow(item, pos, itemId, stackPos);
					},
					__FUNCTION__
				);
				player->setNextWalkActionTask(task);
			}
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if (isPodiumOfRenown) {
		player->sendPodiumWindow(item, pos, itemId, stackPos);
	} else {
		player->sendMonsterPodiumWindow(item, pos, itemId, stackPos);
	}
}

void PlayerInteractionService::playerSetShowOffSocket(uint32_t playerId, Outfit_t &outfit, const Position &pos, uint8_t stackPos, const uint16_t itemId, uint8_t podiumVisible, uint8_t direction) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || pos.x == 0xFFFF) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || !item->isPodium() || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	const auto &tile = item->getParent() ? item->getParent()->getTile() : nullptr;
	if (!tile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, false)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos] {
					playerBrowseField(playerId, pos);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (outfit.lookType != 0) {
		item->setCustomAttribute("PastLookType", static_cast<int64_t>(outfit.lookType));
	}

	if (outfit.lookMount != 0) {
		item->setCustomAttribute("PastLookMount", static_cast<int64_t>(outfit.lookMount));
	}

	if (!player->canWear(outfit.lookType, outfit.lookAddons)) {
		outfit.lookType = 0;
		outfit.lookAddons = 0;
	}

	const auto mount = game_.mounts->getMountByClientID(outfit.lookMount);
	if (!mount || !player->hasMount(mount) || player->isWearingSupportOutfit()) {
		outfit.lookMount = 0;
	}

	if (outfit.lookType != 0) {
		item->setCustomAttribute("LookType", static_cast<int64_t>(outfit.lookType));
		item->setCustomAttribute("LookHead", static_cast<int64_t>(outfit.lookHead));
		item->setCustomAttribute("LookBody", static_cast<int64_t>(outfit.lookBody));
		item->setCustomAttribute("LookLegs", static_cast<int64_t>(outfit.lookLegs));
		item->setCustomAttribute("LookFeet", static_cast<int64_t>(outfit.lookFeet));
		item->setCustomAttribute("LookAddons", static_cast<int64_t>(outfit.lookAddons));
	} else if (auto pastLookType = item->getCustomAttribute("PastLookType"); pastLookType && pastLookType->getInteger() > 0) {
		item->removeCustomAttribute("LookType");
		item->removeCustomAttribute("PastLookType");
	}

	if (outfit.lookMount != 0) {
		item->setCustomAttribute("LookMount", static_cast<int64_t>(outfit.lookMount));
		item->setCustomAttribute("LookMountHead", static_cast<int64_t>(outfit.lookMountHead));
		item->setCustomAttribute("LookMountBody", static_cast<int64_t>(outfit.lookMountBody));
		item->setCustomAttribute("LookMountLegs", static_cast<int64_t>(outfit.lookMountLegs));
		item->setCustomAttribute("LookMountFeet", static_cast<int64_t>(outfit.lookMountFeet));
	} else if (auto pastLookMount = item->getCustomAttribute("PastLookMount"); pastLookMount && pastLookMount->getInteger() > 0) {
		item->removeCustomAttribute("LookMount");
		item->removeCustomAttribute("PastLookMount");
	}

	item->setCustomAttribute("PodiumVisible", static_cast<int64_t>(podiumVisible));
	item->setCustomAttribute("LookDirection", static_cast<int64_t>(direction));

	// Change Podium name
	if (outfit.lookType != 0 || outfit.lookMount != 0) {
		std::ostringstream name;
		name << Item::items[item->getID()].name << " displaying the ";
		bool outfited = false;
		if (outfit.lookType != 0) {
			const auto &outfitInfo = Outfits::getInstance().getOutfitByLookType(player, outfit.lookType);
			if (!outfitInfo) {
				return;
			}

			name << outfitInfo->name << " outfit";
			outfited = true;
		}

		if (outfit.lookMount != 0) {
			if (outfited) {
				name << " on the ";
			}
			name << mount->name << " mount";
		}
		item->setAttribute(ItemAttribute_t::NAME, name.str());
	} else {
		item->removeAttribute(ItemAttribute_t::NAME);
	}

	// Send to client
	for (const auto &spectator : Spectators().find<Player>(pos, true)) {
		spectator->getPlayer()->sendUpdateTileItem(tile, pos, item);
	}
}

void PlayerInteractionService::playerWrapableItem(uint32_t playerId, const Position &pos, uint8_t stackPos, const uint16_t itemId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_FIND_THING);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item) {
		g_logger().error("Game::playerWrapableItem: Invalid item on position: {}", pos.toString());
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}
	const auto &tile = game_.map.getTile(item->getPosition());
	if (!tile) {
		g_logger().error("Game::playerWrapableItem: Invalid tile on position: {}", pos.toString());
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto houseTile = tile->dynamic_self_cast<HouseTile>();
	if (!tile->hasFlag(TILESTATE_PROTECTIONZONE) || !houseTile) {
		player->sendCancelMessage("You may construct this only inside a house.");
		return;
	}
	const auto &house = houseTile->getHouse();
	if (!house) {
		player->sendCancelMessage("You may construct this only inside a house.");
		return;
	}

	if (house->getHouseAccessLevel(player) < HOUSE_OWNER) {
		player->sendCancelMessage("You are not allowed to construct this here.");
		return;
	}

	if (!item || item->getID() != itemId || item->hasAttribute(ItemAttribute_t::UNIQUEID) || (!item->isWrapable() && item->getID() != ITEM_DECORATION_KIT)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_CANNOTUSETHISOBJECT);
		return;
	}

	if (pos.x != 0xFFFF && !Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos, stackPos, itemId] {
					playerWrapableItem(playerId, pos, stackPos, itemId);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	std::shared_ptr<Container> container = item->getContainer();
	if (container && container->getItemHoldingCount() > 0) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	auto topItem = tile->getTopTopItem();
	bool unwrappable = item->getHoldingPlayer() && item->getID() == ITEM_DECORATION_KIT;
	bool blockedUnwrap = topItem && topItem->canReceiveAutoCarpet() && !item->hasProperty(CONST_PROP_IMMOVABLEBLOCKSOLID);

	if (unwrappable || blockedUnwrap) {
		player->sendCancelMessage("You can only wrap/unwrap on the floor.");
		return;
	}

	std::string itemName = item->getName();
	auto unWrapAttribute = item->getCustomAttribute("unWrapId");
	uint16_t unWrapId = 0;
	if (unWrapAttribute != nullptr) {
		unWrapId = static_cast<uint16_t>(unWrapAttribute->getInteger());
	}

	// Prevent to wrap a filled bath tube
	if (item->getID() == ITEM_FILLED_BATH_TUBE) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->isWrapable() && item->getID() != ITEM_DECORATION_KIT) {
		wrapItem(item, houseTile->getHouse());
	} else if (item->getID() == ITEM_DECORATION_KIT && unWrapId != 0) {
		unwrapItem(item, unWrapId, houseTile->getHouse(), player);
	}
	game_.addMagicEffect(pos, CONST_ME_POFF);
}

std::shared_ptr<Item> PlayerInteractionService::wrapItem(const std::shared_ptr<Item> &item, const std::shared_ptr<House> &house) {
	uint16_t hiddenCharges = 0;
	uint16_t amount = item->getItemCount();
	if (isCaskItem(item->getID())) {
		hiddenCharges = item->getSubType();
	}
	if (house != nullptr && Item::items.getItemType(item->getID()).isBed()) {
		item->getBed()->wakeUp(nullptr);
		house->removeBed(item->getBed());
	}
	uint16_t oldItemID = item->getID();
	auto itemName = item->getName();
	auto attributeStore = item->getAttribute<int64_t>(ItemAttribute_t::STORE);
	std::shared_ptr<Item> newItem = game_.transformItem(item, ITEM_DECORATION_KIT);
	if (attributeStore != 0) {
		newItem->setAttribute(ItemAttribute_t::STORE, attributeStore);
	}
	newItem->setCustomAttribute("unWrapId", static_cast<int64_t>(oldItemID));
	newItem->setAttribute(ItemAttribute_t::DESCRIPTION, "You bought this item in the Store.\nUnwrap it in your own house to create a <" + itemName + ">.");
	if (hiddenCharges > 0) {
		newItem->setAttribute(ItemAttribute_t::DATE, hiddenCharges);
	}
	if (amount > 0) {
		newItem->setAttribute(ItemAttribute_t::AMOUNT, amount);
	}
	newItem->startDecaying();
	return newItem;
}

void PlayerInteractionService::unwrapItem(const std::shared_ptr<Item> &item, uint16_t unWrapId, const std::shared_ptr<House> &house, const std::shared_ptr<Player> &player) {
	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}
	auto hiddenCharges = item->getAttribute<uint16_t>(ItemAttribute_t::DATE);
	const ItemType &newiType = Item::items.getItemType(unWrapId);
	if (player != nullptr && house != nullptr && newiType.isBed() && house->getMaxBeds() > -1 && house->getBedCount() >= house->getMaxBeds()) {
		player->sendCancelMessage("You reached the maximum beds in this house");
		return;
	}
	auto amount = item->getAttribute<uint16_t>(ItemAttribute_t::AMOUNT);
	if (!amount) {
		amount = 1;
	}

	auto attributeStore = item->getAttribute<int64_t>(ItemAttribute_t::STORE);
	std::shared_ptr<Item> newItem = game_.transformItem(item, unWrapId, amount);
	if (attributeStore != 0) {
		newItem->setAttribute(ItemAttribute_t::STORE, attributeStore);
	}
	if (house && newiType.isBed()) {
		house->addBed(newItem->getBed());
	}
	if (newItem) {
		if (hiddenCharges > 0 && isCaskItem(unWrapId)) {
			newItem->setSubType(hiddenCharges);
		}
		newItem->removeCustomAttribute("unWrapId");
		newItem->removeAttribute(ItemAttribute_t::DESCRIPTION);
		newItem->startDecaying();
	}
}

void PlayerInteractionService::playerWriteItem(uint32_t playerId, uint32_t windowTextId, const std::string &text) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	uint16_t maxTextLength = 0;
	uint32_t internalWindowTextId = 0;

	std::shared_ptr<Item> writeItem = player->getWriteItem(internalWindowTextId, maxTextLength);
	if (text.length() > maxTextLength || windowTextId != internalWindowTextId) {
		return;
	}

	if (!writeItem || writeItem->isRemoved()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (writeItem->hasOwner() && !writeItem->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	std::shared_ptr<Cylinder> topParent = writeItem->getTopParent();

	std::shared_ptr<Player> owner = std::dynamic_pointer_cast<Player>(topParent);
	if (owner && owner != player) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!Position::areInRange<1, 1, 0>(writeItem->getPosition(), player->getPosition())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	for (const auto &creatureEvent : player->getCreatureEvents(CREATURE_EVENT_TEXTEDIT)) {
		if (!creatureEvent->executeTextEdit(player, writeItem, text)) {
			player->setWriteItem(nullptr);
			return;
		}
	}

	if (!text.empty()) {
		if (writeItem->getAttribute<std::string>(ItemAttribute_t::TEXT) != text) {
			writeItem->setAttribute(ItemAttribute_t::TEXT, text);
			writeItem->setAttribute(ItemAttribute_t::WRITER, player->getName());
			writeItem->setAttribute(ItemAttribute_t::DATE, getTimeNow());
		}
	} else {
		writeItem->removeAttribute(ItemAttribute_t::TEXT);
		writeItem->removeAttribute(ItemAttribute_t::WRITER);
		writeItem->removeAttribute(ItemAttribute_t::DATE);
	}

	uint16_t newId = Item::items[writeItem->getID()].writeOnceItemId;
	if (newId != 0) {
		game_.transformItem(writeItem, newId);
	}

	player->setWriteItem(nullptr);
}

void PlayerInteractionService::playerBrowseField(uint32_t playerId, const Position &pos) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const Position &playerPos = player->getPosition();
	if (playerPos.z != pos.z) {
		player->sendCancelMessage(playerPos.z > pos.z ? RETURNVALUE_FIRSTGOUPSTAIRS : RETURNVALUE_FIRSTGODOWNSTAIRS);
		return;
	}

	if (!Position::areInRange<1, 1>(playerPos, pos)) {
		std::vector<Direction> listDir;
		if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos] {
					playerBrowseField(playerId, pos);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	std::shared_ptr<Tile> tile = game_.map.getTile(pos);
	if (!tile) {
		return;
	}

	if (!g_events().eventPlayerOnBrowseField(player, pos)) {
		return;
	}

	if (!g_callbacks().checkCallback(EventCallback_t::playerOnBrowseField, player, tile->getPosition())) {
		return;
	}

	std::shared_ptr<Container> container;

	auto it = game_.browseFields.find(tile);
	if (it == game_.browseFields.end() || it->second.expired()) {
		container = Container::createBrowseField(tile);
		game_.browseFields[tile] = container;
	} else {
		container = it->second.lock();
	}

	uint8_t dummyContainerId = 0xF - ((pos.x % 3) * 3 + (pos.y % 3));
	std::shared_ptr<Container> openContainer = player->getContainerByID(dummyContainerId);
	if (openContainer) {
		player->onCloseContainer(openContainer);
		player->closeContainer(dummyContainerId);
	} else {
		player->addContainer(dummyContainerId, container);
		player->sendContainer(dummyContainerId, container, false, 0);
	}
}

void PlayerInteractionService::playerStowItem(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackpos, uint8_t count, bool allItems) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isPremium()) {
		player->sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackpos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || item->getItemCount() < count || item->isStoreItem()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (item->hasOwner() && !item->isOwner(player)) {
		player->sendCancelMessage(RETURNVALUE_ITEMISNOTYOURS);
		return;
	}

	if (pos.x != 0xFFFF && !Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	player->stowItem(item, count, allItems);

	// Refresh depot search window if necessary
	if (player->isDepotSearchOpenOnItem(itemId)) {
		// Tier for item stackable is 0
		player->requestDepotSearchItem(itemId, 0);
	}
}

void PlayerInteractionService::playerStashWithdraw(uint32_t playerId, uint16_t itemId, uint32_t count, uint8_t) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->hasFlag(PlayerFlags_t::CannotPickupItem)) {
		return;
	}

	const ItemType &it = Item::items[itemId];
	if (it.id == 0 || count == 0) {
		return;
	}

	if (player->getFreeCapacity() < 100) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHCAPACITY);
		return;
	}

	auto maxWithdrawLimit = static_cast<uint32_t>(config_.getNumber(STASH_MANAGE_AMOUNT));
	if (count > maxWithdrawLimit) {
		std::stringstream limitMessage;
		limitMessage << "You can only withdraw up to " << maxWithdrawLimit << " items at a time from the stash.";
		player->sendTextMessage(MESSAGE_EVENT_ADVANCE, limitMessage.str());
		count = maxWithdrawLimit;
	}

	const uint32_t previousStashCount = player->getStashItemCount(itemId);
	auto ret = player->addItemFromStash(itemId, count);
	if (ret != RETURNVALUE_NOERROR) {
		g_logger().warn("[{}] failed to retrieve item: {}, count {}, to player: {}, from the stash", __FUNCTION__, itemId, count, player->getName());
		const uint32_t currentStashCount = player->getStashItemCount(itemId);
		if (currentStashCount < previousStashCount) {
			const uint32_t diff = previousStashCount - currentStashCount;
			player->addItemOnStash(itemId, diff);
			g_logger().warn("[{}] corrected stash count for item: {}, count {}, to player: {}, from the stash", __FUNCTION__, itemId, diff, player->getName());
		}
		player->sendCancelMessage(ret);
	}

	// Refresh depot search window if necessary
	if (player->isDepotSearchOpenOnItem(itemId)) {
		player->requestDepotSearchItem(itemId, 0);
	}

	player->sendOpenStash(true);
}

void PlayerInteractionService::playerSeekInContainer(uint32_t playerId, uint8_t containerId, uint16_t index, uint8_t containerCategory) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Container> container = player->getContainerByID(containerId);
	if (!container || !container->hasPagination()) {
		return;
	}

	if (container->isStoreInbox()) {
		auto enumName = magic_enum::enum_name(static_cast<ContainerCategory_t>(containerCategory)).data();
		container->setAttribute(ItemAttribute_t::STORE_INBOX_CATEGORY, enumName);
		g_logger().debug("Setting new container with store inbox category name {}", enumName);
	}

	if ((index % container->capacity()) != 0 || index >= container->size()) {
		return;
	}

	player->setContainerIndex(containerId, index);
	player->sendContainer(containerId, container, container->hasParent(), index);
}

void PlayerInteractionService::playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, const std::string &text) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	uint32_t internalWindowTextId;
	uint32_t internalListId;

	const auto &house = player->getEditHouse(internalWindowTextId, internalListId);
	if (house && house->canEditAccessList(internalListId, player) && internalWindowTextId == windowTextId && listId == 0) {
		house->setAccessList(internalListId, text);
	}

	player->setEditHouse(nullptr);
}

void PlayerInteractionService::playerApplyImbuement(uint32_t playerId, uint16_t imbuementid, uint8_t slot) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();

	Imbuement* imbuement = g_imbuements().getImbuement(imbuementid);
	if (!imbuement) {
		return;
	}

	if (!player->hasImbuingItem()) {
		player->createScrollImbuement(imbuement);
		return;
	}

	const auto &item = player->imbuingItem;
	if (!item) {
		return;
	}

	if (item->getTopParent() != player) {
		g_logger().error("[PlayerInteractionService::playerApplyImbuement] - An error occurred while player with name {} try to apply imbuement", player->getName());
		player->sendImbuementResult("An error has occurred, reopen the imbuement window. If the problem persists, contact your administrator.");
		return;
	}

	player->onApplyImbuement(imbuement, item, slot);
}

void PlayerInteractionService::playerClearImbuement(uint32_t playerid, uint8_t slot) {
	const auto &player = game_.getPlayerByID(playerid);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();

	if (!player->hasImbuingItem()) {
		return;
	}

	const auto &item = player->imbuingItem;
	if (!item) {
		return;
	}

	player->onClearImbuement(item, slot);
}

void PlayerInteractionService::playerCloseImbuementWindow(uint32_t playerid) {
	const auto &player = game_.getPlayerByID(playerid);
	if (!player) {
		return;
	}

	player->setImbuingItem(nullptr);
}

void PlayerInteractionService::playerRequestInventoryImbuements(uint32_t playerId, bool isTrackerOpen) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || player->isRemoved()) {
		return;
	}

	player->imbuementTrackerWindowOpen = isTrackerOpen;
	if (!player->imbuementTrackerWindowOpen) {
		return;
	}

	std::map<Slots_t, std::shared_ptr<Item>> itemsWithImbueSlotMap;
	for (uint8_t inventorySlot = CONST_SLOT_FIRST; inventorySlot <= CONST_SLOT_LAST; ++inventorySlot) {
		const auto &item = player->getInventoryItem(static_cast<Slots_t>(inventorySlot));
		if (!item) {
			continue;
		}

		uint8_t imbuementSlot = item->getImbuementSlot();
		for (uint8_t slot = 0; slot < imbuementSlot; slot++) {
			ImbuementInfo imbuementInfo;
			if (!item->getImbuementInfo(slot, &imbuementInfo)) {
				continue;
			}
		}

		itemsWithImbueSlotMap[static_cast<Slots_t>(inventorySlot)] = item;
	}

	player->sendInventoryImbuements(itemsWithImbueSlotMap);
}

void PlayerInteractionService::playerRequestAddVip(uint32_t playerId, const std::string &name) {
	if (name.length() > 25) {
		return;
	}

	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Player> vipPlayer = game_.getPlayerByName(name);
	if (!vipPlayer) {
		uint32_t guid;
		bool specialVip;
		std::string formattedName = name;
		if (!IOLoginData::getGuidByNameEx(guid, specialVip, formattedName)) {
			player->sendTextMessage(MESSAGE_FAILURE, "A player with this name does not exist.");
			return;
		}

		if (specialVip && !player->hasFlag(PlayerFlags_t::SpecialVIP)) {
			player->sendTextMessage(MESSAGE_FAILURE, "You can not add this player");
			return;
		}

		player->vip().add(guid, formattedName, VipStatus_t::Offline);
	} else {
		if (vipPlayer->hasFlag(PlayerFlags_t::SpecialVIP) && !player->hasFlag(PlayerFlags_t::SpecialVIP)) {
			player->sendTextMessage(MESSAGE_FAILURE, "You can not add this player");
			return;
		}

		if (!vipPlayer->isInGhostMode() || player->isAccessPlayer()) {
			player->vip().add(vipPlayer->getGUID(), vipPlayer->getName(), vipPlayer->vip().getStatus());
		} else {
			player->vip().add(vipPlayer->getGUID(), vipPlayer->getName(), VipStatus_t::Offline);
		}
	}
}

void PlayerInteractionService::playerRequestRemoveVip(uint32_t playerId, uint32_t guid) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->vip().remove(guid);
}

void PlayerInteractionService::playerRequestEditVip(uint32_t playerId, uint32_t guid, const std::string &description, uint32_t icon, bool notify, std::vector<uint8_t> vipGroupsId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->vip().edit(guid, description, icon, notify, vipGroupsId);
}

