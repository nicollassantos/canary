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
#include "creatures/players/components/player_stash_component.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PlayerStashComponentTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector);
		DI::setTestContainer(&injector);
	}

	void SetUp() override {
		player = std::make_shared<Player>();
	}

	std::shared_ptr<Player> player;

private:
	inline static di::extension::injector<> injector {};
};

TEST_F(PlayerStashComponentTest, GetStashItemCount_ReturnsZero_WhenEmpty) {
	EXPECT_EQ(0u, player->stashComponent().getStashItemCount(100));
}

TEST_F(PlayerStashComponentTest, AddItemOnStash_SetsInitialAmount) {
	player->stashComponent().addItemOnStash(100, 50);
	EXPECT_EQ(50u, player->stashComponent().getStashItemCount(100));
}

TEST_F(PlayerStashComponentTest, AddItemOnStash_AccumulatesAmount) {
	player->stashComponent().addItemOnStash(100, 30);
	player->stashComponent().addItemOnStash(100, 20);
	EXPECT_EQ(50u, player->stashComponent().getStashItemCount(100));
}

TEST_F(PlayerStashComponentTest, AddItemOnStash_MultipleItems_IndependentCounts) {
	player->stashComponent().addItemOnStash(100, 10);
	player->stashComponent().addItemOnStash(200, 25);

	EXPECT_EQ(10u, player->stashComponent().getStashItemCount(100));
	EXPECT_EQ(25u, player->stashComponent().getStashItemCount(200));
}

TEST_F(PlayerStashComponentTest, GetStashItems_ReturnsEmpty_WhenNothingAdded) {
	EXPECT_TRUE(player->stashComponent().getStashItems().empty());
}

TEST_F(PlayerStashComponentTest, GetStashItems_ReturnsAllAddedItems) {
	player->stashComponent().addItemOnStash(10, 5);
	player->stashComponent().addItemOnStash(20, 3);

	const auto items = player->stashComponent().getStashItems();
	ASSERT_EQ(2u, items.size());
	EXPECT_EQ(5u, items.at(10));
	EXPECT_EQ(3u, items.at(20));
}

TEST_F(PlayerStashComponentTest, GetStashItemCount_ReturnsZero_ForUnknownItem) {
	player->stashComponent().addItemOnStash(100, 1);
	EXPECT_EQ(0u, player->stashComponent().getStashItemCount(999));
}

TEST_F(PlayerStashComponentTest, AddItemOnStash_LargeAmount_StoresCorrectly) {
	const uint32_t bigAmount = 1000000u;
	player->stashComponent().addItemOnStash(42, bigAmount);
	EXPECT_EQ(bigAmount, player->stashComponent().getStashItemCount(42));
}
