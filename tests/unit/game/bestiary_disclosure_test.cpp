/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/bestiary/bestiary_disclosure.hpp"

// The Bestiary may be asked about any creature; what it reveals depends on the player's kill level.
TEST(BestiaryDisclosureTest, LootIsRevealedByKillLevelAndDifficulty) {
	using bestiary_disclosure::showsLootItem;
	for (int8_t difficulty = 0; difficulty <= 4; ++difficulty) {
		EXPECT_FALSE(showsLootItem(0, difficulty, false));
		EXPECT_FALSE(showsLootItem(1, difficulty, false)); // nothing before the first unlock
		EXPECT_EQ(difficulty < 2, showsLootItem(2, difficulty, false));
		EXPECT_EQ(difficulty < 3, showsLootItem(3, difficulty, false));
		EXPECT_TRUE(showsLootItem(4, difficulty, false));
		EXPECT_TRUE(showsLootItem(1, difficulty, true)); // the server shows all loot
	}
	EXPECT_FALSE(showsLootItem(200, 0, false)); // a level the client cannot have
}

TEST(BestiaryDisclosureTest, StatsFromLevelTwoElementsAndLocationsFromLevelThree) {
	EXPECT_FALSE(bestiary_disclosure::showsStats(1));
	EXPECT_TRUE(bestiary_disclosure::showsStats(2));
	EXPECT_FALSE(bestiary_disclosure::showsElementsAndLocations(2));
	EXPECT_TRUE(bestiary_disclosure::showsElementsAndLocations(3));
	EXPECT_TRUE(bestiary_disclosure::showsElementsAndLocations(4));
}
