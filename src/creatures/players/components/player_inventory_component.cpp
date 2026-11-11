/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_inventory_component.hpp"

#include "creatures/combat/spells.hpp"
#include "creatures/players/player.hpp"
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "game/game.hpp"
#include "utils/batch_update.hpp"
#include "utils/utils_definitions.hpp"

std::shared_ptr<Item> PlayerInventoryComponent::getInventoryItem(Slots_t slot) const {
	if (slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return nullptr;
	}
	return m_player.inventory[slot];
}

ItemsTierCountList PlayerInventoryComponent::getInventoryItemsId(bool ignoreStoreInbox) const {
	ItemsTierCountList inventoryCache;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		const auto &item = m_player.inventory[i];
		if (!item) {
			continue;
		}

		const bool isStoreInbox = item->getID() == ITEM_STORE_INBOX;

		if (!isStoreInbox) {
			inventoryCache[{ item->getID(), item->getTier() }] += item->getItemAmount();
		}

		const auto &container = item->getContainer();
		if (container && (!isStoreInbox || !ignoreStoreInbox)) {
			for (const auto &containerItem : container->getItems(true)) {
				if (!containerItem) {
					continue;
				}
				inventoryCache[{ containerItem->getID(), containerItem->getTier() }] += containerItem->getItemAmount();
			}
		}
	}

	return inventoryCache;
}

std::vector<std::shared_ptr<Item>> PlayerInventoryComponent::getInventoryItemsFromId(uint16_t itemId, bool ignore) const {
	std::vector<std::shared_ptr<Item>> itemVector;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const auto &item = m_player.inventory[i];
		if (!item) {
			continue;
		}

		if (!ignore && item->getID() == itemId) {
			itemVector.emplace_back(item);
		}

		if (const auto &container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				const auto &containerItem = *it;
				if (containerItem->getID() == itemId) {
					itemVector.emplace_back(containerItem);
				}
			}
		}
	}

	return itemVector;
}

std::vector<std::shared_ptr<Item>> PlayerInventoryComponent::getAllInventoryItems(bool ignoreEquipped, bool ignoreItemWithTier) const {
	std::vector<std::shared_ptr<Item>> itemVector;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const auto &item = m_player.inventory[i];
		if (!item) {
			continue;
		}

		if (!ignoreEquipped) {
			itemVector.emplace_back(item);
		}
		if (const auto &container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				const auto &containedItem = *it;
				if (!containedItem) {
					continue;
				}
				if (ignoreItemWithTier && containedItem->getTier() > 0) {
					continue;
				}
				itemVector.emplace_back(containedItem);
			}
		}
	}

	return itemVector;
}

std::vector<std::shared_ptr<Item>> PlayerInventoryComponent::getEquippedItems() const {
	static const std::vector valid_slots {
		CONST_SLOT_HEAD,
		CONST_SLOT_NECKLACE,
		CONST_SLOT_BACKPACK,
		CONST_SLOT_ARMOR,
		CONST_SLOT_RIGHT,
		CONST_SLOT_LEFT,
		CONST_SLOT_LEGS,
		CONST_SLOT_FEET,
		CONST_SLOT_RING,
	};

	std::vector<std::shared_ptr<Item>> valid_items;
	for (const auto &slot : valid_slots) {
		const auto &item = m_player.inventory[slot];
		if (!item) {
			continue;
		}
		valid_items.emplace_back(item);
	}

	return valid_items;
}

std::vector<std::shared_ptr<Item>> PlayerInventoryComponent::getEquippedAugmentItemsByType(Augment_t augmentType) const {
	std::vector<std::shared_ptr<Item>> result;
	for (const auto &item : getEquippedItems()) {
		for (const auto &augment : item->getAugments()) {
			if (augment->type == augmentType) {
				result.emplace_back(item);
			}
		}
	}
	return result;
}

std::vector<std::shared_ptr<Item>> PlayerInventoryComponent::getEquippedAugmentItems() const {
	std::vector<std::shared_ptr<Item>> result;
	for (const auto &item : getEquippedItems()) {
		if (!item->getAugments().empty()) {
			result.emplace_back(item);
		}
	}
	return result;
}

std::unordered_map<std::pair<uint16_t, uint8_t>, double, PairHash, PairEqual> PlayerInventoryComponent::getEquippedAugments() const {
	std::unordered_map<std::pair<uint16_t, uint8_t>, double, PairHash, PairEqual> equippedAugments;

	for (const auto &item : getEquippedAugmentItems()) {
		for (const auto &augment : item->getAugments()) {
			if (augment->type == Augment_t::None) {
				continue;
			}

			const auto &spell = g_spells().getInstantSpellByName(augment->spellName);
			const auto spellId = spell ? spell->getSpellId() : 0;

			if (spellId == 0) {
				continue;
			}

			const auto key = std::make_pair(spellId, static_cast<uint8_t>(augment->type));
			double divisor = augment->type == Augment_t::Cooldown ? -1000.0 : 100.0;
			equippedAugments[key] += augment->value / divisor;
		}
	}

	return equippedAugments;
}

namespace {
	[[nodiscard]] uint32_t appendMatchingItem(
		const std::shared_ptr<Item> &item,
		uint16_t itemId,
		int32_t subType,
		std::vector<std::shared_ptr<Item>> &itemList
	) {
		if (!item || item->getID() != itemId) {
			return 0;
		}

		const auto itemCount = Item::countByType(item, subType);
		if (itemCount > 0) {
			itemList.push_back(item);
		}
		return itemCount;
	}
}

bool PlayerInventoryComponent::removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped, bool ignoreStoreInbox) const {
	if (amount == 0) {
		return true;
	}

	std::vector<std::shared_ptr<Item>> itemList;

	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		if (ignoreStoreInbox && i == CONST_SLOT_STORE_INBOX) {
			continue;
		}
		const auto &item = m_player.inventory[i];
		if (!item) {
			continue;
		}

		if (!ignoreEquipped && item->getID() == itemId) {
			count += appendMatchingItem(item, itemId, subType, itemList);
			if (count >= amount) {
				g_game().internalRemoveItems(itemList, amount, Item::items[itemId].stackable);
				return true;
			}
			continue;
		}

		const auto &container = item->getContainer();
		if (!container) {
			continue;
		}

		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			count += appendMatchingItem(*it, itemId, subType, itemList);
			if (count >= amount) {
				g_game().internalRemoveItems(itemList, amount, Item::items[itemId].stackable);
				return true;
			}
		}
	}

	return false;
}

bool PlayerInventoryComponent::removeItemCountById(uint16_t itemId, uint32_t itemAmount, bool removeFromStash) {
	// Here we guarantee that the player has at least the necessary amount of items he needs, if not, we return
	if (!m_player.hasItemCountById(itemId, itemAmount, removeFromStash)) {
		return false;
	}

	BatchUpdate batchUpdate(m_player.getPlayer());
	std::unordered_set<Container*> batchedContainers;
	uint32_t amountToRemove = itemAmount;
	// Check items from inventory
	for (const auto &item : getAllInventoryItems()) {
		if (!item || item->getID() != itemId) {
			continue;
		}

		if (const auto &parent = item->getParent()) {
			if (const auto &container = parent->getContainer()) {
				if (batchedContainers.emplace(container.get()).second) {
					batchUpdate.add(container);
				}
			}
		}

		// If the item quantity is already needed, remove the quantity and stop the loop
		if (item->getItemAmount() >= amountToRemove) {
			g_game().internalRemoveItem(item, amountToRemove);
			return true;
		}

		// If not, we continue removing items and checking the next slot.
		const auto removedAmount = item->getItemAmount();
		g_game().internalRemoveItem(item);
		amountToRemove -= removedAmount;
	}

	// If there are not enough items in the inventory, we will remove the remaining from stash
	if (removeFromStash && amountToRemove > 0 && m_player.withdrawItem(itemId, amountToRemove)) {
		return true;
	}

	return false;
}

std::vector<std::shared_ptr<Container>> PlayerInventoryComponent::getAllContainers(bool onlyFromMainBackpack) const {
	std::vector<std::shared_ptr<Container>> containersCache;

	// Add main backpack to the cache
	if (onlyFromMainBackpack) {
		const auto &mainBp = m_player.getBackpack();
		if (mainBp) {
			containersCache.emplace_back(mainBp);
		}
	}

	// Gather all containers from player inventory
	for (uint32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; ++slot) {
		// Skip slots check if onlyFromMainBackpack is true
		if (onlyFromMainBackpack) {
			break;
		}

		const auto &slotItem = getInventoryItem(static_cast<Slots_t>(slot));
		if (!slotItem) {
			continue;
		}

		if (auto container = slotItem->getContainer()) {
			containersCache.emplace_back(container);
		}
	}

	// Add all nested containers to the cache
	for (size_t i = 0; i < containersCache.size(); i++) {
		const auto &container = containersCache[i];
		if (!container) {
			continue;
		}

		for (const auto &item : container->getItemList()) {
			if (auto subContainer = item->getContainer()) {
				containersCache.emplace_back(subContainer);
			}
		}
	}

	return containersCache;
}
