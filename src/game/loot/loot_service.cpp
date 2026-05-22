/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/loot/loot_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"
#include "map/map.hpp"
#include "utils/tools.hpp"

void LootService::playerQuickLootCorpse(const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &corpse, const Position &position) {
	if (!player || !corpse) {
		return;
	}

	std::vector<std::shared_ptr<Item>> itemList;
	bool ignoreListItems = (player->quickLootFilter == QUICKLOOTFILTER_SKIPPEDLOOT);

	bool missedAnyGold = false;
	bool missedAnyItem = false;

	for (ContainerIterator it = corpse->iterator(); it.hasNext(); it.advance()) {
		const auto &item = *it;
		bool listed = player->isQuickLootListedItem(item);
		if ((listed && ignoreListItems) || (!listed && !ignoreListItems)) {
			if (item->getWorth() != 0) {
				missedAnyGold = true;
			} else {
				missedAnyItem = true;
			}
			continue;
		}

		itemList.push_back(item);
	}

	bool shouldNotifyCapacity = false;
	ObjectCategory_t shouldNotifyNotEnoughRoom = OBJECTCATEGORY_NONE;

	uint32_t totalLootedGold = 0;
	uint32_t totalLootedItems = 0;
	for (const std::shared_ptr<Item> &item : itemList) {
		uint32_t worth = item->getWorth();
		uint16_t baseCount = item->getItemCount();
		ObjectCategory_t category = getObjectCategory(item);

		ReturnValue ret = internalCollectManagedItems(player, item, category);
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			shouldNotifyCapacity = true;
		} else if (ret == RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			shouldNotifyNotEnoughRoom = category;
		}

		bool success = ret == RETURNVALUE_NOERROR;
		if (worth != 0) {
			missedAnyGold = missedAnyGold || !success;
			if (success) {
				player->sendLootStats(item, baseCount);
				totalLootedGold += worth;
			} else {
				// item is not completely moved
				totalLootedGold += worth - item->getWorth();
			}
		} else {
			missedAnyItem = missedAnyItem || !success;
			if (success || item->getItemCount() != baseCount) {
				totalLootedItems++;
				player->sendLootStats(item, item->getItemCount());
			}
		}
	}

	bool hasLootavaible = false;
	for (ContainerIterator it = corpse->iterator(); it.hasNext(); it.advance()) {
		const auto &corpseItem = *it;
		if (!corpseItem) {
			continue;
		}

		const bool listed = player->isQuickLootListedItem(corpseItem);
		if ((listed && ignoreListItems) || (!listed && !ignoreListItems)) {
			continue;
		}

		hasLootavaible = true;
		break;
	}

	if (!hasLootavaible) {
		corpse->clearLootHighlight(player);
	}

	std::stringstream ss;
	if (totalLootedGold != 0 || missedAnyGold || totalLootedItems != 0 || missedAnyItem) {
		bool lootedAllGold = totalLootedGold != 0 && !missedAnyGold;
		bool lootedAllItems = totalLootedItems != 0 && !missedAnyItem;
		if (lootedAllGold) {
			if (totalLootedItems != 0 || missedAnyItem) {
				ss << "You looted the complete " << totalLootedGold << " gold";

				if (lootedAllItems) {
					ss << " and all dropped items";
				} else if (totalLootedItems != 0) {
					ss << ", but you only looted some of the items";
				} else if (missedAnyItem) {
					ss << " but none of the dropped items";
				}
			} else {
				ss << "You looted " << totalLootedGold << " gold";
			}
		} else if (lootedAllItems) {
			if (totalLootedItems == 1) {
				ss << "You looted 1 item";
			} else if (totalLootedGold != 0 || missedAnyGold) {
				ss << "You looted all of the dropped items";
			} else {
				ss << "You looted all items";
			}

			if (totalLootedGold != 0) {
				ss << ", but you only looted " << totalLootedGold << " of the dropped gold";
			} else if (missedAnyGold) {
				ss << " but none of the dropped gold";
			}
		} else if (totalLootedGold != 0) {
			ss << "You only looted " << totalLootedGold << " of the dropped gold";
			if (totalLootedItems != 0) {
				ss << " and some of the dropped items";
			} else if (missedAnyItem) {
				ss << " but none of the dropped items";
			}
		} else if (totalLootedItems != 0) {
			ss << "You looted some of the dropped items";
			if (missedAnyGold) {
				ss << " but none of the dropped gold";
			}
		} else if (missedAnyGold) {
			ss << "You looted none of the dropped gold";
			if (missedAnyItem) {
				ss << " and none of the items";
			}
		} else if (missedAnyItem) {
			ss << "You looted none of the dropped items";
		}
	} else {
		ss << "No loot";
	}
	ss << ".";
	player->sendTextMessage(MESSAGE_STATUS, ss.str());

	if (shouldNotifyCapacity) {
		ss.str(std::string());
		ss << "Attention! The loot you are trying to pick up is too heavy for you to carry.";
	} else if (shouldNotifyNotEnoughRoom != OBJECTCATEGORY_NONE) {
		ss.str(std::string());
		ss << "Attention! The container assigned to category " << getObjectCategoryName(shouldNotifyNotEnoughRoom) << " is full.";
	} else {
		return;
	}

	if (player->lastQuickLootNotification + 15000 < OTSYS_TIME()) {
		player->sendTextMessage(MESSAGE_GAME_HIGHLIGHT, ss.str());
	} else {
		player->sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
	}

	corpse->sendUpdateToClient(player);

	player->lastQuickLootNotification = OTSYS_TIME();
}

std::shared_ptr<Container> LootService::findManagedContainer(const std::shared_ptr<Player> &player, bool &fallbackConsumed, ObjectCategory_t category, bool isLootContainer) {
	auto lootContainer = player->getManagedContainer(category, isLootContainer);
	if (!lootContainer && player->quickLootFallbackToMainContainer && !fallbackConsumed) {
		auto fallbackItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
		auto mainBackpack = fallbackItem ? fallbackItem->getContainer() : nullptr;

		if (mainBackpack) {
			player->refreshManagedContainer(OBJECTCATEGORY_DEFAULT, mainBackpack, isLootContainer);
			player->sendInventoryItem(CONST_SLOT_BACKPACK, player->getInventoryItem(CONST_SLOT_BACKPACK));
			lootContainer = mainBackpack;
			fallbackConsumed = true;
		}
	}

	return lootContainer;
}

std::shared_ptr<Container> LootService::findNextAvailableContainer(ContainerIterator &containerIterator, std::shared_ptr<Container> &lootContainer, std::shared_ptr<Container> &lastSubContainer) {
	while (containerIterator.hasNext()) {
		std::shared_ptr<Item> cur = *containerIterator;
		std::shared_ptr<Container> subContainer = cur ? cur->getContainer() : nullptr;
		containerIterator.advance();

		if (subContainer) {
			lastSubContainer = subContainer;
			lootContainer = subContainer;
			return lootContainer;
		}
	}

	// Fix last empty sub-container
	if (lastSubContainer && !lastSubContainer->empty()) {
		auto cur = lastSubContainer->getItemByIndex(lastSubContainer->size() - 1);
		lootContainer = cur ? cur->getContainer() : nullptr;
		lastSubContainer = nullptr;
		return lootContainer;
	}

	return nullptr;
}

bool LootService::handleFallbackLogic(const std::shared_ptr<Player> &player, std::shared_ptr<Container> &lootContainer, ContainerIterator &containerIterator, const bool &fallbackConsumed) {
	if (fallbackConsumed || !player->quickLootFallbackToMainContainer) {
		return false;
	}

	std::shared_ptr<Item> fallbackItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
	if (!fallbackItem || !fallbackItem->getContainer()) {
		return false;
	}

	lootContainer = fallbackItem->getContainer();
	containerIterator = lootContainer->iterator();

	return true;
}

ReturnValue LootService::processMoveOrAddItemToLootContainer(const std::shared_ptr<Item> &item, const std::shared_ptr<Container> &lootContainer, uint32_t &remainderCount, const std::shared_ptr<Player> &player) {
	std::shared_ptr<Item> moveItem = nullptr;
	ReturnValue ret;
	if (item->getParent()) {
		ret = game_.internalMoveItem(item->getParent(), lootContainer, INDEX_WHEREEVER, item, item->getItemCount(), &moveItem, 0, player, nullptr, false);
	} else {
		ret = game_.internalAddItem(lootContainer, item, INDEX_WHEREEVER);
	}
	if (moveItem) {
		remainderCount -= moveItem->getItemCount();
	}
	return ret;
}

ReturnValue LootService::processLootItems(const std::shared_ptr<Player> &player, std::shared_ptr<Container> lootContainer, const std::shared_ptr<Item> &item, bool &fallbackConsumed) {
	std::shared_ptr<Container> lastSubContainer = nullptr;
	uint32_t remainderCount = item->getItemCount();
	ContainerIterator containerIterator = lootContainer->iterator();

	ReturnValue ret;
	do {
		ret = processMoveOrAddItemToLootContainer(item, lootContainer, remainderCount, player);
		if (ret != RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			return ret;
		}

		std::shared_ptr<Container> nextContainer = findNextAvailableContainer(containerIterator, lootContainer, lastSubContainer);
		if (!nextContainer && !handleFallbackLogic(player, lootContainer, containerIterator, fallbackConsumed)) {
			break;
		}
		fallbackConsumed = fallbackConsumed || (nextContainer == nullptr);
	} while (remainderCount != 0);

	return ret;
}

ReturnValue LootService::internalCollectManagedItems(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item, ObjectCategory_t category, bool isLootContainer /* = true*/) {
	if (!player || !item) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	// Send money to the bank
	if (config_.getBoolean(AUTOBANK)) {
		if (item->getID() == ITEM_GOLD_COIN || item->getID() == ITEM_PLATINUM_COIN || item->getID() == ITEM_CRYSTAL_COIN) {
			uint64_t money = 0;
			if (item->getID() == ITEM_PLATINUM_COIN) {
				money = item->getItemCount() * 100;
			} else if (item->getID() == ITEM_CRYSTAL_COIN) {
				money = item->getItemCount() * 10000;
			} else {
				money = item->getItemCount();
			}
			auto parent = item->getParent();
			if (parent) {
				parent->removeThing(item, item->getItemCount());
			} else {
				g_logger().debug("Item has no parent");
				return RETURNVALUE_NOTPOSSIBLE;
			}
			player->setBankBalance(player->getBankBalance() + money);
			g_metrics().addCounter("balance_increase", money, { { "player", player->getName() }, { "context", "loot" } });
			return RETURNVALUE_NOERROR;
		}
	}

	if (!player->quickLootListItemIds.empty()) {
		uint16_t itemId = item->getID();
		bool isInList = std::ranges::find(player->quickLootListItemIds, itemId) != player->quickLootListItemIds.end();
		if (player->quickLootFilter == QuickLootFilter_t::QUICKLOOTFILTER_ACCEPTEDLOOT && !isInList) {
			return RETURNVALUE_NOTPOSSIBLE;
		} else if (player->quickLootFilter == QuickLootFilter_t::QUICKLOOTFILTER_SKIPPEDLOOT && isInList) {
			return RETURNVALUE_NOTPOSSIBLE;
		}
	}

	bool fallbackConsumed = false;
	std::shared_ptr<Container> lootContainer = findManagedContainer(player, fallbackConsumed, category, isLootContainer);
	if (!lootContainer) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	return processLootItems(player, lootContainer, item, fallbackConsumed);
}

ReturnValue LootService::collectRewardChestItems(const std::shared_ptr<Player> &player, uint32_t maxMoveItems /* = 0*/) {
	// Check if have item on player reward chest
	const std::shared_ptr<RewardChest> &rewardChest = player->getRewardChest();
	if (rewardChest->empty()) {
		g_logger().debug("Reward chest is empty");
		return RETURNVALUE_REWARDCHESTISEMPTY;
	}

	const auto &container = rewardChest->getContainer();
	if (!container) {
		return RETURNVALUE_REWARDCHESTISEMPTY;
	}

	auto rewardItemsVector = player->getRewardsFromContainer(container);
	auto rewardCount = rewardItemsVector.size();
	uint32_t movedRewardItems = 0;
	std::string lootedItemsMessage;
	for (const auto &item : rewardItemsVector) {
		// Stop if player not have free capacity
		if (item && player->getCapacity() < item->getWeight()) {
			player->sendCancelMessage(RETURNVALUE_NOTENOUGHCAPACITY);
			break;
		}

		// Limit the collect count if the "maxMoveItems" is not "0"
		auto limitMove = maxMoveItems != 0 && movedRewardItems == maxMoveItems;
		if (limitMove) {
			lootedItemsMessage = fmt::format("You can only collect {} items at a time. {} of {} objects were picked up.", maxMoveItems, movedRewardItems, rewardCount);
			player->sendTextMessage(MESSAGE_EVENT_ADVANCE, lootedItemsMessage);
			return RETURNVALUE_NOERROR;
		}

		ObjectCategory_t category = getObjectCategory(item);
		if (internalCollectManagedItems(player, item, category) == RETURNVALUE_NOERROR) {
			movedRewardItems++;
		}
	}

	lootedItemsMessage = fmt::format("{} of {} objects were picked up.", movedRewardItems, rewardCount);
	player->sendTextMessage(MESSAGE_EVENT_ADVANCE, lootedItemsMessage);

	if (movedRewardItems == 0) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	return RETURNVALUE_NOERROR;
}

ObjectCategory_t LootService::getObjectCategory(const std::shared_ptr<Item> &item) const {
	ObjectCategory_t category = OBJECTCATEGORY_DEFAULT;
	if (!item) {
		return OBJECTCATEGORY_NONE;
	}

	const ItemType &it = Item::items[item->getID()];
	if (item->getWorth() != 0) {
		category = OBJECTCATEGORY_GOLD;
	} else {
		category = getObjectCategory(it);
	}

	return category;
}

ObjectCategory_t LootService::getObjectCategory(const ItemType &it) const {
	ObjectCategory_t category = OBJECTCATEGORY_DEFAULT;
	if (it.weaponType != WEAPON_NONE) {
		switch (it.weaponType) {
			case WEAPON_FIST:
				category = OBJECTCATEGORY_FISTWEAPONS;
				break;
			case WEAPON_SWORD:
				category = OBJECTCATEGORY_SWORDS;
				break;
			case WEAPON_CLUB:
				category = OBJECTCATEGORY_CLUBS;
				break;
			case WEAPON_AXE:
				category = OBJECTCATEGORY_AXES;
				break;
			case WEAPON_SHIELD:
				category = OBJECTCATEGORY_SHIELDS;
				break;
			case WEAPON_MISSILE:
			case WEAPON_DISTANCE:
				category = OBJECTCATEGORY_DISTANCEWEAPONS;
				break;
			case WEAPON_WAND:
				category = OBJECTCATEGORY_WANDS;
				break;
			case WEAPON_AMMO:
				category = OBJECTCATEGORY_AMMO;
				break;
			default:
				break;
		}
	} else if (it.slotPosition != SLOTP_HAND) { // if it's a weapon/shield should have been parsed earlier
		if ((it.slotPosition & SLOTP_HEAD) != 0) {
			category = OBJECTCATEGORY_HELMETS;
		} else if ((it.slotPosition & SLOTP_NECKLACE) != 0) {
			category = OBJECTCATEGORY_NECKLACES;
		} else if ((it.slotPosition & SLOTP_BACKPACK) != 0) {
			category = OBJECTCATEGORY_CONTAINERS;
		} else if ((it.slotPosition & SLOTP_ARMOR) != 0) {
			category = OBJECTCATEGORY_ARMORS;
		} else if ((it.slotPosition & SLOTP_LEGS) != 0) {
			category = OBJECTCATEGORY_LEGS;
		} else if ((it.slotPosition & SLOTP_FEET) != 0) {
			category = OBJECTCATEGORY_BOOTS;
		} else if ((it.slotPosition & SLOTP_RING) != 0) {
			category = OBJECTCATEGORY_RINGS;
		}
	} else if (it.type == ITEM_TYPE_RUNE) {
		category = OBJECTCATEGORY_RUNES;
	} else if (it.type == ITEM_TYPE_CREATUREPRODUCT) {
		category = OBJECTCATEGORY_CREATUREPRODUCTS;
	} else if (it.type == ITEM_TYPE_FOOD) {
		category = OBJECTCATEGORY_FOOD;
	} else if (it.type == ITEM_TYPE_VALUABLE) {
		category = OBJECTCATEGORY_VALUABLES;
	} else if (it.type == ITEM_TYPE_POTION) {
		category = OBJECTCATEGORY_POTIONS;
	} else {
		category = OBJECTCATEGORY_OTHERS;
	}

	return category;
}

void LootService::playerQuickLoot(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackPos, const std::shared_ptr<Item> &defaultItem, bool lootAllCorpses, bool autoLoot) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!autoLoot && !player->canDoAction()) {
		const uint32_t delay = player->getNextActionTime();
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId = player->getID(), pos, itemId, stackPos, defaultItem, lootAllCorpses, autoLoot] {
				playerQuickLoot(playerId, pos, itemId, stackPos, defaultItem, lootAllCorpses, autoLoot);
			},
			__FUNCTION__
		);
		player->setNextActionTask(task);
		return;
	}

	if (!autoLoot && pos.x != 0xffff) {
		if (!Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
			// need to walk to the corpse first before looting it
			std::vector<Direction> listDir;
			if (player->getPathTo(pos, listDir, 0, 1, true, true)) {
				g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
				const auto &task = game_.createPlayerTask(
					300,
					[this, playerId = player->getID(), pos, itemId, stackPos, defaultItem, lootAllCorpses, autoLoot] {
						playerQuickLoot(playerId, pos, itemId, stackPos, defaultItem, lootAllCorpses, autoLoot);
					},
					__FUNCTION__
				);
				player->setNextWalkActionTask(task);
			} else {
				player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
			}

			return;
		}
	} else if (!player->isPremium()) {
		player->sendCancelMessage("You must be premium.");
		return;
	}

	Player::PlayerLock lock(player);
	if (!autoLoot) {
		player->setNextActionTask(nullptr);
	}

	std::shared_ptr<Item> item = nullptr;
	if (!defaultItem) {
		const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_FIND_THING);
		if (!thing) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		item = thing->getItem();
	} else {
		item = defaultItem;
	}

	if (!item || !item->getParent()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	std::shared_ptr<Container> corpse = nullptr;
	if (pos.x == 0xffff) {
		corpse = item->getParent()->getContainer();
		if (corpse && corpse->getID() == ITEM_BROWSEFIELD) {
			corpse = item->getContainer();
			game_.browseField = true;
		}
	} else {
		corpse = item->getContainer();
	}

	if (!corpse || corpse->hasAttribute(ItemAttribute_t::UNIQUEID) || corpse->hasAttribute(ItemAttribute_t::ACTIONID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!corpse->isRewardCorpse()) {
		uint32_t corpseOwner = corpse->getCorpseOwner();
		if (corpseOwner != 0 && !player->canOpenCorpse(corpseOwner)) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}
	}

	if (pos.x == 0xffff && !game_.browseField && !corpse->isRewardCorpse()) {
		uint32_t worth = item->getWorth();
		ObjectCategory_t category = getObjectCategory(item);
		ReturnValue ret = internalCollectManagedItems(player, item, category);

		std::stringstream ss;
		if (ret == RETURNVALUE_NOTENOUGHCAPACITY) {
			ss << "Attention! The loot you are trying to pick up is too heavy for you to carry.";
		} else if (ret == RETURNVALUE_CONTAINERNOTENOUGHROOM) {
			ss << "Attention! The container for " << getObjectCategoryName(category) << " is full.";
		} else {
			if (ret == RETURNVALUE_NOERROR) {
				player->sendLootStats(item, item->getItemCount());
				ss << "You looted ";
			} else {
				ss << "You could not loot ";
			}

			if (worth != 0) {
				ss << worth << " gold.";
			} else {
				ss << "1 item.";
			}

			player->sendTextMessage(MESSAGE_LOOT, ss.str());
			return;
		}

		if (player->lastQuickLootNotification + 15000 < OTSYS_TIME()) {
			player->sendTextMessage(MESSAGE_GAME_HIGHLIGHT, ss.str());
		} else {
			player->sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		}

		player->lastQuickLootNotification = OTSYS_TIME();
	} else {
		if (corpse->isRewardCorpse()) {
			auto rewardId = corpse->getAttribute<time_t>(ItemAttribute_t::DATE);
			auto reward = player->getReward(rewardId, false);
			if (reward) {
				playerQuickLootCorpse(player, reward->getContainer(), corpse->getPosition());
			}
		} else {
			if (!lootAllCorpses) {
				playerQuickLootCorpse(player, corpse, corpse->getPosition());
			} else {
				playerLootAllCorpses(player, pos, lootAllCorpses);
			}
		}
	}

	corpse->sendUpdateToClient(player);
}

void LootService::playerLootAllCorpses(const std::shared_ptr<Player> &player, const Position &pos, bool lootAllCorpses) {
	if (lootAllCorpses) {
		const auto maxQuickLootCorpses = static_cast<uint16_t>(std::max<int32_t>(1, config_.getNumber(QUICK_LOOT_MAX_CORPSES)));
		std::shared_ptr<Tile> tile = game_.map.getTile(pos.x, pos.y, pos.z);
		if (!tile) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		const TileItemVector* itemVector = tile->getItemList();
		uint16_t corpses = 0;
		for (auto &tileItem : *itemVector) {
			if (!tileItem) {
				continue;
			}

			std::shared_ptr<Container> tileCorpse = tileItem->getContainer();
			if (!tileCorpse || !tileCorpse->isCorpse() || tileCorpse->hasAttribute(ItemAttribute_t::UNIQUEID) || tileCorpse->hasAttribute(ItemAttribute_t::ACTIONID)) {
				continue;
			}

			if (!tileCorpse->isRewardCorpse()
			    && tileCorpse->getCorpseOwner() != 0
			    && !player->canOpenCorpse(tileCorpse->getCorpseOwner())) {
				player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
				g_logger().debug("Player {} cannot loot corpse from id {} in position {}", player->getName(), tileItem->getID(), tileItem->getPosition().toString());
				continue;
			}

			corpses++;
			playerQuickLootCorpse(player, tileCorpse, tileCorpse->getPosition());
			if (corpses >= maxQuickLootCorpses) {
				break;
			}
		}

		if (corpses > 0) {
			if (corpses > 1) {
				std::stringstream string;
				string << "You looted " << corpses << " corpses.";
				player->sendTextMessage(MESSAGE_LOOT, string.str());
			}

			return;
		}
	}

	game_.browseField = false;
}

namespace {
	struct NearbyQuickLootSnapshot {
		uint64_t goldValue = 0;
		uint64_t stackableAmount = 0;
		uint32_t looseItems = 0;

		[[nodiscard]] bool hasLoot() const noexcept {
			return goldValue != 0 || stackableAmount != 0 || looseItems != 0;
		}

		bool operator!=(const NearbyQuickLootSnapshot &other) const {
			return goldValue != other.goldValue
				|| stackableAmount != other.stackableAmount
				|| looseItems != other.looseItems;
		}
	};

	NearbyQuickLootSnapshot captureNearbyQuickLootSnapshot(const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &container) {
		NearbyQuickLootSnapshot snapshot;
		if (!player || !container) {
			return snapshot;
		}

		const bool ignoreListItems = player->getQuickLootFilter() == QUICKLOOTFILTER_SKIPPEDLOOT;
		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			const auto &item = *it;
			if (!item) {
				continue;
			}

			const bool listed = player->isQuickLootListedItem(item);
			if ((listed && ignoreListItems) || (!listed && !ignoreListItems)) {
				continue;
			}

			const auto worth = item->getWorth();
			if (worth != 0) {
				snapshot.goldValue += worth;
			} else if (item->isStackable() || item->hasAttribute(ItemAttribute_t::CHARGES)) {
				snapshot.stackableAmount += item->getItemCount();
			} else {
				++snapshot.looseItems;
			}
		}

		return snapshot;
	}

	const TileItemVector* getNearbyLootItems(Game &game, const Position &playerPos, int32_t xOffset, int32_t yOffset) {
		const int32_t tileX = static_cast<int32_t>(playerPos.x) + xOffset;
		const int32_t tileY = static_cast<int32_t>(playerPos.y) + yOffset;
		if (tileX < 0 || tileX > 0xFFFF || tileY < 0 || tileY > 0xFFFF) {
			return nullptr;
		}

		const auto &tile = game.map.getTile(
			static_cast<uint16_t>(tileX),
			static_cast<uint16_t>(tileY),
			playerPos.z
		);
		if (!tile) {
			return nullptr;
		}

		return tile->getItemList();
	}

	bool isNearbyLootableCorpse(const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &corpse) {
		if (!player || !corpse || !corpse->isCorpse()
		    || corpse->hasAttribute(ItemAttribute_t::UNIQUEID)
		    || corpse->hasAttribute(ItemAttribute_t::ACTIONID)) {
			return false;
		}

		if (corpse->isRewardCorpse()) {
			return true;
		}

		return corpse->getCorpseOwner() == 0 || player->canOpenCorpse(corpse->getCorpseOwner());
	}

	std::shared_ptr<Container> resolveNearbyLootContainer(const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &corpse) {
		if (!player || !corpse || !corpse->isRewardCorpse()) {
			return corpse;
		}

		const auto rewardId = corpse->getAttribute<time_t>(ItemAttribute_t::DATE);
		const auto reward = player->getReward(rewardId, false);
		return reward ? reward->getContainer() : nullptr;
	}

	struct NearbyQuickLootOutcome {
		bool foundCorpse = false;
		bool looted = false;
	};

	NearbyQuickLootOutcome tryNearbyQuickLootCorpse(Game &game, const std::shared_ptr<Player> &player, const std::shared_ptr<Container> &corpse) {
		if (!isNearbyLootableCorpse(player, corpse)) {
			return {};
		}

		NearbyQuickLootOutcome outcome;
		outcome.foundCorpse = true;

		const auto lootContainer = resolveNearbyLootContainer(player, corpse);
		const auto lootSnapshotBefore = captureNearbyQuickLootSnapshot(player, lootContainer);
		if (!lootSnapshotBefore.hasLoot()) {
			return outcome;
		}

		game.playerQuickLootCorpse(player, lootContainer, corpse->getPosition());
		if (lootContainer != corpse) {
			corpse->sendUpdateToClient(player);
		}
		outcome.looted = captureNearbyQuickLootSnapshot(player, lootContainer) != lootSnapshotBefore;
		return outcome;
	}

	void sendNearbyQuickLootSummary(const std::shared_ptr<Player> &player, bool anyCorpseFound, uint32_t corpsesLooted) {
		if (!player) {
			return;
		}

		if (!anyCorpseFound) {
			player->sendCancelMessage("No lootable corpses nearby.");
			return;
		}

		if (corpsesLooted == 0) {
			player->sendCancelMessage("You could not loot any nearby corpses.");
			return;
		}

		if (corpsesLooted > 1) {
			std::stringstream ss;
			ss << "You looted " << corpsesLooted << " corpses.";
			player->sendTextMessage(MESSAGE_LOOT, ss.str());
		}
	}
} // namespace

void LootService::playerLootNearby(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->canDoAction()) {
		const uint32_t delay = player->getNextActionTime();
		const auto &task = game_.createPlayerTask(
			delay,
			[this, playerId] {
				playerLootNearby(playerId);
			},
			__FUNCTION__
		);
		player->setNextActionTask(task);
		return;
	}

	Player::PlayerLock lock(player);
	player->setNextActionTask(nullptr);

	const auto maxQuickLootCorpses = static_cast<uint32_t>(std::max<int32_t>(1, config_.getNumber(QUICK_LOOT_MAX_CORPSES)));
	const Position &playerPos = player->getPosition();
	uint32_t corpsesLooted = 0;
	bool anyCorpseFound = false;

	for (int32_t x = -1; x <= 1; ++x) {
		for (int32_t y = -1; y <= 1; ++y) {
			const TileItemVector* itemVector = getNearbyLootItems(game_, playerPos, x, y);
			if (!itemVector) {
				continue;
			}

			for (const auto &tileItem : *itemVector) {
				const auto outcome = tryNearbyQuickLootCorpse(game_, player, tileItem ? tileItem->getContainer() : nullptr);
				anyCorpseFound = anyCorpseFound || outcome.foundCorpse;
				corpsesLooted += outcome.looted ? 1U : 0U;
				if (corpsesLooted >= maxQuickLootCorpses) {
					break;
				}
			}

			if (corpsesLooted >= maxQuickLootCorpses) {
				break;
			}
		}

		if (corpsesLooted >= maxQuickLootCorpses) {
			break;
		}
	}

	sendNearbyQuickLootSummary(player, anyCorpseFound, corpsesLooted);
}

void LootService::playerSetManagedContainer(uint32_t playerId, ObjectCategory_t category, const Position &pos, uint16_t itemId, uint8_t stackPos, bool isLootContainer) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || pos.x != 0xffff) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_USEITEM);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const std::shared_ptr<Container> &container = thing->getContainer();
	auto allowConfig = config_.getBoolean(TOGGLE_GOLD_POUCH_ALLOW_ANYTHING) || config_.getBoolean(TOGGLE_GOLD_POUCH_QUICKLOOT_ONLY);
	if (!container || ((container->getID() == ITEM_GOLD_POUCH && category != OBJECTCATEGORY_GOLD) && !allowConfig)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (container->getID() == ITEM_GOLD_POUCH && !isLootContainer) {
		player->sendTextMessage(MESSAGE_FAILURE, "You can only set the gold pouch as a loot container.");
		return;
	}

	if (container->getHoldingPlayer() != player) {
		player->sendCancelMessage("You must be holding the container to set it as a loot container.");
		return;
	}

	std::shared_ptr<Container> previousContainer = player->refreshManagedContainer(category, container, isLootContainer);
	player->sendLootContainers();

	std::shared_ptr<Cylinder> parent = container->getParent();
	if (parent) {
		parent->updateThing(container, container->getID(), container->getItemCount());
	}

	if (previousContainer != nullptr) {
		parent = previousContainer->getParent();
		if (parent) {
			parent->updateThing(previousContainer, previousContainer->getID(), previousContainer->getItemCount());
		}
	}
}

void LootService::playerClearManagedContainer(uint32_t playerId, ObjectCategory_t category, bool isLootContainer) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Container> previousContainer = player->refreshManagedContainer(category, nullptr, isLootContainer);
	player->sendLootContainers();

	if (previousContainer != nullptr) {
		std::shared_ptr<Cylinder> parent = previousContainer->getParent();
		if (parent) {
			parent->updateThing(previousContainer, previousContainer->getID(), previousContainer->getItemCount());
		}
	}
}

void LootService::playerOpenManagedContainer(uint32_t playerId, ObjectCategory_t category, bool isLootContainer) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	std::shared_ptr<Container> container = player->getManagedContainer(category, isLootContainer);
	if (!container) {
		return;
	}

	player->sendContainer(static_cast<uint8_t>(container->getID()), container, container->hasParent(), 0);
}

void LootService::playerSetQuickLootFallback(uint32_t playerId, bool fallback) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->quickLootFallbackToMainContainer = fallback;
}

void LootService::playerQuickLootBlackWhitelist(uint32_t playerId, QuickLootFilter_t filter, const std::vector<uint16_t> &itemIds) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->quickLootFilter = filter;
	player->quickLootListItemIds = itemIds;
}

/*******************************************************************************
 * Depot search system
 ******************************************************************************/
void LootService::playerRequestDepotItems(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isDepotSearchAvailable()) {
		return;
	}

	if (player->isUIExhausted(500)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->requestDepotItems();
	player->updateUIExhausted();
}

void LootService::playerRequestCloseDepotSearch(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isDepotSearchOpen()) {
		return;
	}

	player->setDepotSearchIsOpen(0, 0);
	player->sendCloseDepotSearch();
}

void LootService::playerRequestDepotSearchItem(uint32_t playerId, uint16_t itemId, uint8_t tier) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isDepotSearchOpen()) {
		return;
	}

	if (player->isUIExhausted(500)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->requestDepotSearchItem(itemId, tier);
	player->updateUIExhausted();
}

void LootService::playerRequestDepotSearchRetrieve(uint32_t playerId, uint16_t itemId, uint8_t tier, uint8_t type) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isDepotSearchOpenOnItem(itemId)) {
		return;
	}

	if (player->isUIExhausted(500)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->retrieveAllItemsFromDepotSearch(itemId, tier, type == 1);
	player->updateUIExhausted();
}

void LootService::playerRequestOpenContainerFromDepotSearch(uint32_t playerId, const Position &pos) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isDepotSearchOpen()) {
		return;
	}

	if (player->isUIExhausted(500)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->openContainerFromDepotSearch(pos);
	player->updateUIExhausted();
}

void LootService::playerRewardChestCollect(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackPos, uint32_t maxMoveItems /* = 0*/) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_FIND_THING);
	if (!thing) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != ITEM_REWARD_CHEST || !item->getContainer()) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &function = [this, playerId = player->getID(), pos, itemId, stackPos, maxMoveItems] {
		playerRewardChestCollect(playerId, pos, itemId, stackPos, maxMoveItems);
	};

	if (player->canAutoWalk(item->getPosition(), function)) {
		return;
	}

	// Updates the parent of the reward chest and reward containers to avoid memory usage after cleaning
	const auto &playerRewardChest = player->getRewardChest();
	if (playerRewardChest && playerRewardChest->empty()) {
		player->sendCancelMessage(RETURNVALUE_REWARDCHESTISEMPTY);
		return;
	}

	playerRewardChest->setParent(item->getContainer()->getParent()->getTile());
	for (const auto &[mapRewardId, reward] : player->rewardMap) {
		reward->setParent(playerRewardChest);
	}

	std::scoped_lock<std::mutex> lock(player->quickLootMutex);

	ReturnValue returnValue = collectRewardChestItems(player, maxMoveItems);
	if (returnValue != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(returnValue);
	}
}

bool LootService::tryRetrieveStashItems(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item) {
	ObjectCategory_t category = getObjectCategory(item);
	return internalCollectManagedItems(player, item, category, false) == RETURNVALUE_NOERROR;
}

