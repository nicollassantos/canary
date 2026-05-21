/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// Characterization tests for player.cpp inventory subsystem.
// Documents current getInventoryItem / getWeapon / getAllInventoryItems behavior
// to guard against regressions during Phase 5.3 (InventoryService extraction).

#include <gtest/gtest.h>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/player.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

class PlayerInventoryTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector_);
		DI::setTestContainer(&injector_);
		TestItems::init();
	}

	static void TearDownTestSuite() {
		if (DI::getTestContainer() == &injector_) {
			DI::setTestContainer(nullptr);
		}
	}

private:
	inline static di::extension::injector<> injector_ {};
};

// Empty player has no item in any inventory slot
TEST_F(PlayerInventoryTest, AllInventorySlotsEmptyOnNewPlayer) {
	auto player = std::make_shared<Player>();
	for (int slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		EXPECT_EQ(nullptr, player->getInventoryItem(static_cast<Slots_t>(slot)))
			<< "slot " << slot << " should be empty";
	}
}

// getAllInventoryItems returns empty vector for player with no items
TEST_F(PlayerInventoryTest, GetAllInventoryItemsEmptyForNewPlayer) {
	auto player = std::make_shared<Player>();
	EXPECT_TRUE(player->getAllInventoryItems(false).empty());
}

// getWeapon returns nullptr when player has no weapon equipped
TEST_F(PlayerInventoryTest, GetWeaponNullWhenNothingEquipped) {
	auto player = std::make_shared<Player>();
	EXPECT_EQ(nullptr, player->getWeapon(false));
}

// getInventoryItemsFromId returns empty for non-existent item id
TEST_F(PlayerInventoryTest, GetInventoryItemsFromIdEmptyWhenAbsent) {
	auto player = std::make_shared<Player>();
	constexpr uint16_t nonExistentId = 9999;
	EXPECT_TRUE(player->getInventoryItemsFromId(nonExistentId).empty());
}

// getAllInventoryItems(true) and (false) both return empty for new player
TEST_F(PlayerInventoryTest, GetAllInventoryItemsBothOverloadsEmptyForNewPlayer) {
	auto player = std::make_shared<Player>();
	EXPECT_TRUE(player->getAllInventoryItems(false).empty());
	EXPECT_TRUE(player->getAllInventoryItems(true).empty());
}

// getInventoryItemsFromId with count=0 returns empty
TEST_F(PlayerInventoryTest, GetInventoryItemsFromIdWithZeroCountEmpty) {
	auto player = std::make_shared<Player>();
	// Non-existent and zero-count: both must return empty
	EXPECT_TRUE(player->getInventoryItemsFromId(0).empty());
}
