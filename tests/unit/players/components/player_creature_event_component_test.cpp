/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player.hpp"
#include "items/item.hpp"
#include "injection_fixture.hpp"
#include "test_items.hpp"

// Smoke tests for PlayerCreatureEventComponent.
// All methods guard on m_player.client (null in tests) or on trade/loot state
// (default = no trade, no loot containers). These tests verify the guard paths
// execute without crashing and exercise the main branch points.

class PlayerCreatureEventComponentTest : public ::testing::Test {
public:
	static void SetUpTestSuite() {
		TestItems::init();
	}

protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- onAttackedCreatureDisappear ---

TEST_F(PlayerCreatureEventComponentTest, OnAttackedCreatureDisappear_Logout_DoesNotCrash) {
	// isLogout=true: sendCancelTarget only (no sendTextMessage), client=null guards
	EXPECT_NO_THROW(player->creatureEvents().onAttackedCreatureDisappear(true));
}

TEST_F(PlayerCreatureEventComponentTest, OnAttackedCreatureDisappear_NonLogout_DoesNotCrash) {
	// isLogout=false: both sendCancelTarget + sendTextMessage, both client=null guards
	EXPECT_NO_THROW(player->creatureEvents().onAttackedCreatureDisappear(false));
}

// --- onFollowCreatureDisappear ---

TEST_F(PlayerCreatureEventComponentTest, OnFollowCreatureDisappear_Logout_DoesNotCrash) {
	EXPECT_NO_THROW(player->creatureEvents().onFollowCreatureDisappear(true));
}

TEST_F(PlayerCreatureEventComponentTest, OnFollowCreatureDisappear_NonLogout_DoesNotCrash) {
	EXPECT_NO_THROW(player->creatureEvents().onFollowCreatureDisappear(false));
}

// --- onCloseContainer ---

TEST_F(PlayerCreatureEventComponentTest, OnCloseContainer_NullClient_ReturnsEarly) {
	// client=null → early return before iterating openContainers
	EXPECT_NO_THROW(player->creatureEvents().onCloseContainer(nullptr));
}

// --- onSendContainer ---

TEST_F(PlayerCreatureEventComponentTest, OnSendContainer_NullContainer_ReturnsEarly) {
	// null container → early return
	EXPECT_NO_THROW(player->creatureEvents().onSendContainer(nullptr));
}

TEST_F(PlayerCreatureEventComponentTest, OnSendContainer_NullClient_ReturnsEarly) {
	// client=null → early return
	auto item = Item::CreateItem(100);
	const auto container = item->getContainer();
	// item 100 is not a container, so container==nullptr → same early return via null client check
	EXPECT_NO_THROW(player->creatureEvents().onSendContainer(nullptr));
}

// --- autoCloseContainers ---

TEST_F(PlayerCreatureEventComponentTest, AutoCloseContainers_EmptyOpenContainers_IsNoOp) {
	// No open containers → closeList stays empty, no work done
	EXPECT_NO_THROW(player->creatureEvents().autoCloseContainers(nullptr));
}

// --- onAddContainerItem ---

TEST_F(PlayerCreatureEventComponentTest, OnAddContainerItem_NoTradeItem_DoesNotCrash) {
	// tradeItem=null → checkTradeState early return
	auto item = Item::CreateItem(100);
	EXPECT_NO_THROW(player->creatureEvents().onAddContainerItem(item));
}

// --- onUpdateInventoryItem ---

TEST_F(PlayerCreatureEventComponentTest, OnUpdateInventoryItem_SameItem_SkipsRemovePath) {
	auto item = Item::CreateItem(101);
	// oldItem == newItem → onRemoveInventoryItem not called
	EXPECT_NO_THROW(player->creatureEvents().onUpdateInventoryItem(item, item));
}

TEST_F(PlayerCreatureEventComponentTest, OnUpdateInventoryItem_DifferentItems_ExecutesBothPaths) {
	auto a = Item::CreateItem(100);
	auto b = Item::CreateItem(101);
	// oldItem != newItem → calls onRemoveInventoryItem then checkTradeState (early return)
	EXPECT_NO_THROW(player->creatureEvents().onUpdateInventoryItem(a, b));
}

// --- onRemoveInventoryItem ---

TEST_F(PlayerCreatureEventComponentTest, OnRemoveInventoryItem_NonContainerItem_DoesNotCrash) {
	auto item = Item::CreateItem(100); // regular item, no container → checkLootContainers(null)
	EXPECT_NO_THROW(player->creatureEvents().onRemoveInventoryItem(item));
}
