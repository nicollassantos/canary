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

class PlayerTitleTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		player->setGUID(42);
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- isTitleUnlocked ---

TEST_F(PlayerTitleTest, IsTitleUnlocked_ReturnsFalse_WhenIdIsZero) {
	EXPECT_FALSE(player->title().isTitleUnlocked(0));
}

TEST_F(PlayerTitleTest, IsTitleUnlocked_ReturnsFalse_WhenNothingUnlocked) {
	EXPECT_FALSE(player->title().isTitleUnlocked(5));
}

// --- getUnlockedTitles ---

TEST_F(PlayerTitleTest, GetUnlockedTitles_ReturnsEmpty_Initially) {
	EXPECT_TRUE(player->title().getUnlockedTitles().empty());
}

// --- getCurrentTitle / setCurrentTitle (via KV, no g_game) ---

TEST_F(PlayerTitleTest, GetCurrentTitle_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->title().getCurrentTitle());
}

TEST_F(PlayerTitleTest, SetCurrentTitle_StoresZero_WhenIdIsZero) {
	player->title().setCurrentTitle(0);
	EXPECT_EQ(0u, player->title().getCurrentTitle());
}

TEST_F(PlayerTitleTest, SetCurrentTitle_StoresZero_WhenTitleNotUnlocked) {
	// Title 7 is not unlocked — setCurrentTitle should store 0 (not 7)
	player->title().setCurrentTitle(7);
	EXPECT_EQ(0u, player->title().getCurrentTitle());
}

// --- getNameBySex ---

TEST_F(PlayerTitleTest, GetNameBySex_ReturnsMaleName_WhenSexIsMale) {
	EXPECT_EQ("King", player->title().getNameBySex(PLAYERSEX_MALE, "King", "Queen"));
}

TEST_F(PlayerTitleTest, GetNameBySex_ReturnsFemaleName_WhenSexIsFemale) {
	EXPECT_EQ("Queen", player->title().getNameBySex(PLAYERSEX_FEMALE, "King", "Queen"));
}

TEST_F(PlayerTitleTest, GetNameBySex_ReturnsMaleName_WhenFemaleIsEmpty) {
	EXPECT_EQ("King", player->title().getNameBySex(PLAYERSEX_FEMALE, "King", ""));
}
