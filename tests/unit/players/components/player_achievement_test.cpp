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
#include "injection_fixture.hpp"

class PlayerAchievementTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		player->setGUID(1234);
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- isUnlocked ---

TEST_F(PlayerAchievementTest, IsUnlocked_ReturnsFalse_WhenIdIsZero) {
	EXPECT_FALSE(player->achiev().isUnlocked(0));
}

TEST_F(PlayerAchievementTest, IsUnlocked_ReturnsFalse_WhenNothingUnlocked) {
	EXPECT_FALSE(player->achiev().isUnlocked(42));
}

TEST_F(PlayerAchievementTest, GetUnlockedAchievements_ReturnsEmpty_WhenNoneAdded) {
	EXPECT_TRUE(player->achiev().getUnlockedAchievements().empty());
}

// --- points ---

TEST_F(PlayerAchievementTest, GetPoints_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, AddPoints_IncreasesPoints) {
	player->achiev().addPoints(10);
	EXPECT_EQ(10u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, AddPoints_Accumulates) {
	player->achiev().addPoints(10);
	player->achiev().addPoints(5);
	EXPECT_EQ(15u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, RemovePoints_DecreasesPoints) {
	player->achiev().addPoints(10);
	player->achiev().removePoints(3);
	EXPECT_EQ(7u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, RemovePoints_ClampsToZero_WhenExceedingBalance) {
	player->achiev().addPoints(5);
	player->achiev().removePoints(20);
	EXPECT_EQ(0u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, RemovePoints_NoOp_WhenPointsAlreadyZero) {
	player->achiev().removePoints(10);
	EXPECT_EQ(0u, player->achiev().getPoints());
}

TEST_F(PlayerAchievementTest, PointsAreIndependentPerPlayer) {
	auto player2 = std::make_shared<Player>();
	player2->setGUID(9999);

	player->achiev().addPoints(100);
	player2->achiev().addPoints(25);

	EXPECT_EQ(100u, player->achiev().getPoints());
	EXPECT_EQ(25u, player2->achiev().getPoints());
}
