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

class PlayerSessionComponentTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- isProtected / setProtection ---

TEST_F(PlayerSessionComponentTest, IsProtected_ReturnsFalse_Initially) {
	EXPECT_FALSE(player->session().isProtected());
}

TEST_F(PlayerSessionComponentTest, SetProtection_True_IsProtectedReturnsTrue) {
	player->session().setProtection(true);
	EXPECT_TRUE(player->session().isProtected());
}

TEST_F(PlayerSessionComponentTest, SetProtection_False_IsProtectedReturnsFalse) {
	player->session().setProtection(true);
	player->session().setProtection(false);
	EXPECT_FALSE(player->session().isProtected());
}

// --- isLoginProtected / resetLoginProtection / setLoginProtection ---

TEST_F(PlayerSessionComponentTest, IsLoginProtected_ReturnsFalse_Initially) {
	// loginProtectionTime = 0 → 0 > now → false
	EXPECT_FALSE(player->session().isLoginProtected());
}

TEST_F(PlayerSessionComponentTest, ResetLoginProtection_MakesNotProtected) {
	player->session().setLoginProtection(9999999);
	player->session().resetLoginProtection();
	EXPECT_FALSE(player->session().isLoginProtected());
}

TEST_F(PlayerSessionComponentTest, SetLoginProtection_LargeValue_IsProtected) {
	// time in ms; 1e12 ms ≈ 30 years in future
	player->session().setLoginProtection(static_cast<int64_t>(1e12));
	EXPECT_TRUE(player->session().isLoginProtected());
}

TEST_F(PlayerSessionComponentTest, SetLoginProtection_Zero_IsNotProtected) {
	// loginProtectionTime = now + 0 = now; now > now is false
	player->session().setLoginProtection(0);
	EXPECT_FALSE(player->session().isLoginProtected());
}

// --- canLogout ---

TEST_F(PlayerSessionComponentTest, CanLogout_ReturnsFalse_WhenNoTile) {
	// Default player has no tile → returns false before reaching pz/infight checks
	EXPECT_FALSE(player->session().canLogout());
}
