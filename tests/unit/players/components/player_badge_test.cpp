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

class PlayerBadgeTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		player->setGUID(55);
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- hasBadge ---

TEST_F(PlayerBadgeTest, HasBadge_ReturnsFalse_WhenIdIsZero) {
	EXPECT_FALSE(player->badge().hasBadge(0));
}

TEST_F(PlayerBadgeTest, HasBadge_ReturnsFalse_WhenNothingUnlocked) {
	EXPECT_FALSE(player->badge().hasBadge(10));
}

// --- loyalty (uses loyaltyPoints which defaults to 0) ---

TEST_F(PlayerBadgeTest, Loyalty_ReturnsTrue_WhenPointsMeetThreshold) {
	// loyaltyPoints defaults to 0; loyalty(0) → 0 >= 0 → true
	EXPECT_TRUE(player->badge().loyalty(0));
}

TEST_F(PlayerBadgeTest, Loyalty_ReturnsFalse_WhenPointsBelowThreshold) {
	// loyaltyPoints = 0; loyalty(1) → 0 >= 1 → false
	EXPECT_FALSE(player->badge().loyalty(1));
}

// --- accountAge (loyaltyPoints=0 means 0 years) ---

TEST_F(PlayerBadgeTest, AccountAge_ReturnsTrue_WhenRequirementIsZero) {
	// floor(0 / 365) = 0, 0 >= 0 → true
	EXPECT_TRUE(player->badge().accountAge(0));
}

TEST_F(PlayerBadgeTest, AccountAge_ReturnsFalse_WhenRequirementExceedsPoints) {
	// floor(0 / 365) = 0, 0 >= 1 → false
	EXPECT_FALSE(player->badge().accountAge(1));
}

// --- tournament stubs always return false ---

TEST_F(PlayerBadgeTest, TournamentParticipation_AlwaysReturnsFalse) {
	EXPECT_FALSE(player->badge().tournamentParticipation(0));
	EXPECT_FALSE(player->badge().tournamentParticipation(255));
}

TEST_F(PlayerBadgeTest, TournamentPoints_AlwaysReturnsFalse) {
	EXPECT_FALSE(player->badge().tournamentPoints(0));
	EXPECT_FALSE(player->badge().tournamentPoints(255));
}
