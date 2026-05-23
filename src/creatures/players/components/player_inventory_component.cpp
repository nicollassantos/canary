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
