/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/player_race_icon_overlays.hpp"

using enum CreatureIconModifications_t;

class PlayerRaceIconOverlaysTest : public ::testing::Test {
protected:
	PlayerRaceIconOverlays overlays;
};

// --- set ---

TEST_F(PlayerRaceIconOverlaysTest, Set_RejectsRaceIdZero) {
	EXPECT_FALSE(overlays.set(0, Influenced));
	EXPECT_TRUE(overlays.get(0).empty());
}

TEST_F(PlayerRaceIconOverlaysTest, Set_RejectsNoneIcon) {
	EXPECT_FALSE(overlays.set(10, None));
	EXPECT_TRUE(overlays.get(10).empty());
}

TEST_F(PlayerRaceIconOverlaysTest, Set_RejectsOutOfRangeIcon) {
	EXPECT_FALSE(overlays.set(10, static_cast<CreatureIconModifications_t>(250)));
	EXPECT_TRUE(overlays.get(10).empty());
}

TEST_F(PlayerRaceIconOverlaysTest, Set_AddsIconOnce) {
	EXPECT_TRUE(overlays.set(10, Influenced));
	EXPECT_FALSE(overlays.set(10, Influenced));

	const auto icons = overlays.get(10);
	ASSERT_EQ(icons.size(), 1U);
	EXPECT_EQ(icons.front(), CreatureIcon { Influenced });
}

TEST_F(PlayerRaceIconOverlaysTest, Set_KeepsRacesIsolated) {
	EXPECT_TRUE(overlays.set(10, Influenced));
	EXPECT_TRUE(overlays.set(11, Fiendish));

	ASSERT_EQ(overlays.get(10).size(), 1U);
	EXPECT_EQ(overlays.get(10).front(), CreatureIcon { Influenced });
	ASSERT_EQ(overlays.get(11).size(), 1U);
	EXPECT_EQ(overlays.get(11).front(), CreatureIcon { Fiendish });
}

TEST_F(PlayerRaceIconOverlaysTest, Set_DisableRemovesIcon) {
	ASSERT_TRUE(overlays.set(10, Influenced));
	ASSERT_TRUE(overlays.set(10, Fiendish));

	EXPECT_TRUE(overlays.set(10, Influenced, false));

	const auto icons = overlays.get(10);
	ASSERT_EQ(icons.size(), 1U);
	EXPECT_EQ(icons.front(), CreatureIcon { Fiendish });
}

TEST_F(PlayerRaceIconOverlaysTest, Set_DisableUnknownIconReturnsFalse) {
	EXPECT_FALSE(overlays.set(10, Influenced, false));
	ASSERT_TRUE(overlays.set(10, Fiendish));
	EXPECT_FALSE(overlays.set(10, Influenced, false));
}

// --- clear ---

TEST_F(PlayerRaceIconOverlaysTest, Clear_RejectsNoneIcon) {
	ASSERT_TRUE(overlays.set(10, Influenced));
	EXPECT_FALSE(overlays.clear(None));
	EXPECT_EQ(overlays.get(10).size(), 1U);
}

TEST_F(PlayerRaceIconOverlaysTest, Clear_RemovesIconFromEveryRace) {
	ASSERT_TRUE(overlays.set(10, Influenced));
	ASSERT_TRUE(overlays.set(11, Influenced));
	ASSERT_TRUE(overlays.set(11, Fiendish));

	EXPECT_TRUE(overlays.clear(Influenced));

	EXPECT_TRUE(overlays.get(10).empty());
	const auto icons = overlays.get(11);
	ASSERT_EQ(icons.size(), 1U);
	EXPECT_EQ(icons.front(), CreatureIcon { Fiendish });
}

TEST_F(PlayerRaceIconOverlaysTest, Clear_ReturnsFalseWhenNothingChanged) {
	ASSERT_TRUE(overlays.set(10, Fiendish));
	EXPECT_FALSE(overlays.clear(Influenced));
}

// --- get ---

TEST_F(PlayerRaceIconOverlaysTest, Get_UnknownRaceIsEmpty) {
	EXPECT_TRUE(overlays.get(999).empty());
}
