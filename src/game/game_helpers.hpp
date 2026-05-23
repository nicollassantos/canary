/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <limits>
#include <memory>
#include <string>

#include "creatures/players/player.hpp"
#include "items/item.hpp"
#include "lib/logging/logger.hpp"
#include "map/house/house.hpp"
#include "map/house/housetile.hpp"
#include "utils/utils_definitions.hpp"

namespace GameHelpers {
	inline bool playerCanUseItemOnHouseTile(const std::shared_ptr<Player> &player, const std::shared_ptr<Item> &item) {
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

	template <typename T>
	T getCustomAttributeValue(const auto item, const std::string &attributeName) {
		static_assert(std::is_integral<T>::value, "T must be an integral type");

		auto attribute = item->getCustomAttribute(attributeName);
		if (!attribute) {
			return 0;
		}

		int64_t value = attribute->getInteger();
		if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max()) {
			g_logger().error("[{}] value is out of range for the specified type", __FUNCTION__);
			return 0;
		}

		return static_cast<T>(value);
	}
} // namespace GameHelpers
