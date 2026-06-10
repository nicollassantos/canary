/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player.hpp"
#include "items/item.hpp"
#include "injection_fixture.hpp"
#include "test_items.hpp"

// Smoke tests for PlayerImbuementComponent.
// Most methods guard on m_player.client (null in tests), null pointers, or
// player state (default = no imbuement tracker window, zero imbuements).

class PlayerImbuementComponentTest : public ::testing::Test {
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

// --- client-null guards ---

TEST_F(PlayerImbuementComponentTest, SendImbuementResult_NullClient_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().sendImbuementResult("test message"));
}

TEST_F(PlayerImbuementComponentTest, CloseImbuementWindow_NullClient_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().closeImbuementWindow());
}

TEST_F(PlayerImbuementComponentTest, SendInventoryImbuements_NullClient_DoesNotCrash) {
	const std::map<Slots_t, std::shared_ptr<Item>> emptyMap;
	EXPECT_NO_THROW(player->imbuementComponent().sendInventoryImbuements(emptyMap));
}

TEST_F(PlayerImbuementComponentTest, OpenImbuementWindow_NullClient_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().openImbuementWindow(ImbuementAction::PickItem, nullptr));
}

// --- clearAllImbuements ---

TEST_F(PlayerImbuementComponentTest, ClearAllImbuements_NullItem_ReturnsFalse) {
	EXPECT_FALSE(player->imbuementComponent().clearAllImbuements(nullptr));
}

TEST_F(PlayerImbuementComponentTest, ClearAllImbuements_ItemWithNoSlots_ReturnsFalse) {
	auto item = Item::CreateItem(100); // regular item, 0 imbuement slots
	EXPECT_FALSE(player->imbuementComponent().clearAllImbuements(item));
}

// --- addItemImbuementStats / removeItemImbuementStats ---

TEST_F(PlayerImbuementComponentTest, AddItemImbuementStats_NullImbuement_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().addItemImbuementStats(nullptr));
}

TEST_F(PlayerImbuementComponentTest, RemoveItemImbuementStats_NullImbuement_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().removeItemImbuementStats(nullptr));
}

TEST_F(PlayerImbuementComponentTest, AddItemImbuementStats_ZeroImbuement_DoesNotCrash) {
	// All fields zero: no g_game() call (no speed), no sendStats (requestUpdate=false)
	Imbuement imb(1, 1);
	EXPECT_NO_THROW(player->imbuementComponent().addItemImbuementStats(&imb));
}

TEST_F(PlayerImbuementComponentTest, RemoveItemImbuementStats_ZeroImbuement_DoesNotCrash) {
	Imbuement imb(1, 1);
	EXPECT_NO_THROW(player->imbuementComponent().removeItemImbuementStats(&imb));
}

// --- updateImbuementTrackerStats ---

TEST_F(PlayerImbuementComponentTest, UpdateImbuementTrackerStats_WindowClosed_DoesNotCrash) {
	// imbuementTrackerWindowOpen defaults to false → early return path, no g_dispatcher() call
	// (m_pendingImbuementTrackerEventId==0 → no stopEvent)
	EXPECT_NO_THROW(player->imbuementComponent().updateImbuementTrackerStats());
}

// --- onApplyImbuement ---

TEST_F(PlayerImbuementComponentTest, OnApplyImbuement_NullImbuement_DoesNotCrash) {
	auto item = Item::CreateItem(100);
	EXPECT_NO_THROW(player->imbuementComponent().onApplyImbuement(nullptr, item, 0));
}

TEST_F(PlayerImbuementComponentTest, OnApplyImbuement_NullItem_DoesNotCrash) {
	Imbuement imb(1, 1);
	EXPECT_NO_THROW(player->imbuementComponent().onApplyImbuement(&imb, nullptr, 0));
}

// --- onClearImbuement ---

TEST_F(PlayerImbuementComponentTest, OnClearImbuement_NullItem_DoesNotCrash) {
	EXPECT_NO_THROW(player->imbuementComponent().onClearImbuement(nullptr, 0));
}

// --- updateDamageReductionFromItemImbuement ---

TEST_F(PlayerImbuementComponentTest, UpdateDamageReduction_ItemWithNoImbuementSlots_DoesNotCrash) {
	auto item = Item::CreateItem(100); // 0 imbuement slots → loop body never executes
	std::array<double_t, COMBAT_COUNT> reductions {};
	EXPECT_NO_THROW(player->imbuementComponent().updateDamageReductionFromItemImbuement(reductions, item, 0));
}
