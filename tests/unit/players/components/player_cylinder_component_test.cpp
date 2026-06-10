/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/player.hpp"
#include "items/cylinder.hpp"
#include "items/item.hpp"
#include "injection_fixture.hpp"
#include "test_items.hpp"

class PlayerCylinderComponentTest : public ::testing::Test {
public:
	static void SetUpTestSuite() {
		TestItems::init();
	}

protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	void equip(Slots_t slot, std::shared_ptr<Item> item) const {
		static_cast<Cylinder &>(*player).internalAddThing(static_cast<uint32_t>(slot), item);
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- getFirstIndex / getLastIndex ---

TEST_F(PlayerCylinderComponentTest, GetFirstIndex_ReturnsConstSlotFirst) {
	EXPECT_EQ(static_cast<size_t>(CONST_SLOT_FIRST), player->cylinderComponent().getFirstIndex());
}

TEST_F(PlayerCylinderComponentTest, GetLastIndex_ReturnsConstSlotLastPlusOne) {
	EXPECT_EQ(static_cast<size_t>(CONST_SLOT_LAST) + 1u, player->cylinderComponent().getLastIndex());
}

// --- getThingIndex ---

TEST_F(PlayerCylinderComponentTest, GetThingIndex_ReturnsMinusOne_WhenItemNotInInventory) {
	auto item = Item::CreateItem(100);
	EXPECT_EQ(-1, player->cylinderComponent().getThingIndex(item));
}

TEST_F(PlayerCylinderComponentTest, GetThingIndex_ReturnsSlot_WhenItemEquipped) {
	auto item = Item::CreateItem(100);
	equip(CONST_SLOT_HEAD, item);
	EXPECT_EQ(static_cast<int32_t>(CONST_SLOT_HEAD), player->cylinderComponent().getThingIndex(item));
}

TEST_F(PlayerCylinderComponentTest, GetThingIndex_ReturnsCorrectSlot_ForDifferentSlots) {
	auto head = Item::CreateItem(100);
	auto armor = Item::CreateItem(101);
	equip(CONST_SLOT_HEAD, head);
	equip(CONST_SLOT_ARMOR, armor);

	EXPECT_EQ(static_cast<int32_t>(CONST_SLOT_HEAD), player->cylinderComponent().getThingIndex(head));
	EXPECT_EQ(static_cast<int32_t>(CONST_SLOT_ARMOR), player->cylinderComponent().getThingIndex(armor));
}

// --- getItemTypeCount ---

TEST_F(PlayerCylinderComponentTest, GetItemTypeCount_ReturnsZero_WhenEmpty) {
	EXPECT_EQ(0u, player->cylinderComponent().getItemTypeCount(100));
}

TEST_F(PlayerCylinderComponentTest, GetItemTypeCount_ReturnsOne_WhenOneItemEquipped) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	EXPECT_EQ(1u, player->cylinderComponent().getItemTypeCount(100));
}

TEST_F(PlayerCylinderComponentTest, GetItemTypeCount_CountsAcrossSlots) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	equip(CONST_SLOT_NECKLACE, Item::CreateItem(100));
	EXPECT_EQ(2u, player->cylinderComponent().getItemTypeCount(100));
}

TEST_F(PlayerCylinderComponentTest, GetItemTypeCount_ReturnsZero_ForWrongItemId) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	EXPECT_EQ(0u, player->cylinderComponent().getItemTypeCount(101));
}
