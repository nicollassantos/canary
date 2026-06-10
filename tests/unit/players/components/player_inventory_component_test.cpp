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
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

class PlayerInventoryComponentTest : public ::testing::Test {
public:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector);
		DI::setTestContainer(&injector);
		TestItems::init();
	}

	static void TearDownTestSuite() {
		DI::setTestContainer(nullptr);
	}

protected:
	void SetUp() override {
		player = std::make_shared<Player>();
	}

	void equip(Slots_t slot, std::shared_ptr<Item> item) const {
		// internalAddThing is overridden as private in Player but declared public in Cylinder;
		// C++ access check uses the base-class declaration, so this call is valid.
		static_cast<Cylinder &>(*player).internalAddThing(static_cast<uint32_t>(slot), item);
	}

	std::shared_ptr<Player> player;

	inline static di::extension::injector<> injector {};
};

// --- getInventoryItem ---

TEST_F(PlayerInventoryComponentTest, GetInventoryItem_ReturnsNullptr_ForEmptySlot) {
	EXPECT_EQ(nullptr, player->inventoryComponent().getInventoryItem(CONST_SLOT_HEAD));
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItem_ReturnsItem_WhenSlotOccupied) {
	auto item = Item::CreateItem(100);
	equip(CONST_SLOT_HEAD, item);
	EXPECT_EQ(item, player->inventoryComponent().getInventoryItem(CONST_SLOT_HEAD));
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItem_ReturnsNullptr_ForOutOfBoundsSlot) {
	EXPECT_EQ(nullptr, player->inventoryComponent().getInventoryItem(CONST_SLOT_WHEREEVER));
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItem_ReturnsCorrectItem_PerSlot) {
	auto head = Item::CreateItem(100);
	auto armor = Item::CreateItem(101);
	equip(CONST_SLOT_HEAD, head);
	equip(CONST_SLOT_ARMOR, armor);

	EXPECT_EQ(head, player->inventoryComponent().getInventoryItem(CONST_SLOT_HEAD));
	EXPECT_EQ(armor, player->inventoryComponent().getInventoryItem(CONST_SLOT_ARMOR));
	EXPECT_EQ(nullptr, player->inventoryComponent().getInventoryItem(CONST_SLOT_NECKLACE));
}

// --- getInventoryItemsId ---

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsId_ReturnsEmpty_ForNewPlayer) {
	EXPECT_TRUE(player->inventoryComponent().getInventoryItemsId().empty());
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsId_CountsItem_WhenSlotOccupied) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	const auto result = player->inventoryComponent().getInventoryItemsId();
	ASSERT_EQ(1u, result.size());
	EXPECT_EQ(1u, result.at({ 100, 0 }));
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsId_AccumulatesCounts_AcrossSlots) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	equip(CONST_SLOT_NECKLACE, Item::CreateItem(101));
	equip(CONST_SLOT_ARMOR, Item::CreateItem(100));

	const auto result = player->inventoryComponent().getInventoryItemsId();
	EXPECT_EQ(2u, result.at({ 100, 0 }));
	EXPECT_EQ(1u, result.at({ 101, 0 }));
}

// --- getInventoryItemsFromId ---

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsFromId_ReturnsEmpty_WhenNothingEquipped) {
	EXPECT_TRUE(player->inventoryComponent().getInventoryItemsFromId(100, false).empty());
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsFromId_FindsTopLevelItem_WhenIgnoreFalse) {
	auto item = Item::CreateItem(100);
	equip(CONST_SLOT_HEAD, item);

	const auto result = player->inventoryComponent().getInventoryItemsFromId(100, false);
	ASSERT_EQ(1u, result.size());
	EXPECT_EQ(item, result.front());
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsFromId_SkipsTopLevelItem_WhenIgnoreTrue) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));

	// ignore=true (default): top-level items are skipped, only container contents searched
	EXPECT_TRUE(player->inventoryComponent().getInventoryItemsFromId(100, true).empty());
}

TEST_F(PlayerInventoryComponentTest, GetInventoryItemsFromId_FindsAllMatchingTopLevelItems) {
	auto a = Item::CreateItem(100);
	auto b = Item::CreateItem(100);
	equip(CONST_SLOT_HEAD, a);
	equip(CONST_SLOT_ARMOR, b);

	const auto result = player->inventoryComponent().getInventoryItemsFromId(100, false);
	EXPECT_EQ(2u, result.size());
}

// --- getAllInventoryItems ---

TEST_F(PlayerInventoryComponentTest, GetAllInventoryItems_ReturnsEmpty_ForNewPlayer) {
	EXPECT_TRUE(player->inventoryComponent().getAllInventoryItems(false).empty());
	EXPECT_TRUE(player->inventoryComponent().getAllInventoryItems(true).empty());
}

TEST_F(PlayerInventoryComponentTest, GetAllInventoryItems_IncludesTopLevel_WhenIgnoreEquippedFalse) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	equip(CONST_SLOT_ARMOR, Item::CreateItem(101));

	const auto result = player->inventoryComponent().getAllInventoryItems(false);
	EXPECT_EQ(2u, result.size());
}

TEST_F(PlayerInventoryComponentTest, GetAllInventoryItems_SkipsTopLevel_WhenIgnoreEquippedTrue) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));

	// ignoreEquipped=true: top-level items not added; container contents still iterated
	EXPECT_TRUE(player->inventoryComponent().getAllInventoryItems(true).empty());
}

// --- getEquippedItems ---

TEST_F(PlayerInventoryComponentTest, GetEquippedItems_ReturnsEmpty_ForNewPlayer) {
	EXPECT_TRUE(player->inventoryComponent().getEquippedItems().empty());
}

TEST_F(PlayerInventoryComponentTest, GetEquippedItems_ReturnsItemsInEquipSlots) {
	equip(CONST_SLOT_HEAD, Item::CreateItem(100));
	equip(CONST_SLOT_ARMOR, Item::CreateItem(101));

	const auto result = player->inventoryComponent().getEquippedItems();
	EXPECT_EQ(2u, result.size());
}

TEST_F(PlayerInventoryComponentTest, GetEquippedItems_ExcludesAmmoSlot) {
	equip(CONST_SLOT_AMMO, Item::CreateItem(102));

	// AMMO is not in the hardcoded valid_slots list
	EXPECT_TRUE(player->inventoryComponent().getEquippedItems().empty());
}

TEST_F(PlayerInventoryComponentTest, GetEquippedItems_ExcludesStoreInboxSlot) {
	equip(CONST_SLOT_STORE_INBOX, Item::CreateItem(103));

	// STORE_INBOX is not in the hardcoded valid_slots list
	EXPECT_TRUE(player->inventoryComponent().getEquippedItems().empty());
}
