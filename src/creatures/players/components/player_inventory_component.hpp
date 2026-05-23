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
#include <unordered_map>
#include <utility>
#include <vector>

#include "creatures/players/stash_definitions.hpp"
#include "utils/hash.hpp"

class Player;
class Item;
enum Slots_t : uint8_t;
enum class Augment_t : uint8_t;

class PlayerInventoryComponent {
public:
	PlayerInventoryComponent() = delete;
	explicit PlayerInventoryComponent(Player &player) :
		m_player(player) { }

	[[nodiscard]] std::shared_ptr<Item> getInventoryItem(Slots_t slot) const;
	[[nodiscard]] ItemsTierCountList getInventoryItemsId(bool ignoreStoreInbox = false) const;
	[[nodiscard]] std::vector<std::shared_ptr<Item>> getInventoryItemsFromId(uint16_t itemId, bool ignore = true) const;
	[[nodiscard]] std::vector<std::shared_ptr<Item>> getAllInventoryItems(bool ignoreEquipped = false, bool ignoreItemWithTier = false) const;
	[[nodiscard]] std::vector<std::shared_ptr<Item>> getEquippedItems() const;
	[[nodiscard]] std::vector<std::shared_ptr<Item>> getEquippedAugmentItemsByType(Augment_t augmentType) const;
	[[nodiscard]] std::vector<std::shared_ptr<Item>> getEquippedAugmentItems() const;
	[[nodiscard]] std::unordered_map<std::pair<uint16_t, uint8_t>, double, PairHash, PairEqual> getEquippedAugments() const;

private:
	Player &m_player;
};
