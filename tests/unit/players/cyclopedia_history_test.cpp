/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/player_cyclopedia.hpp"
#include "creatures/players/player_progress_repository.hpp"
#include "game/game_definitions.hpp"

// How the Cyclopedia shows a death and a PvP kill, from the stored rows (before, this sat inside the
// database callback and had no test).
namespace {
	RecentDeathRow death(std::string killedBy, std::string mostDamageBy, std::string participants = "") {
		return RecentDeathRow { 1'800'000'000, 50, std::move(killedBy), std::move(mostDamageBy), std::move(participants) };
	}
} // namespace

TEST(CyclopediaHistoryTest, ADeathNamesTheKillerAndWhoDidMostDamage) {
	EXPECT_EQ("Died at level 50 by a dragon", PlayerCyclopedia::describeDeath(death("a dragon", "")));
	EXPECT_EQ("Died at level 50 by a dragon", PlayerCyclopedia::describeDeath(death("", "a dragon")));
	EXPECT_EQ("Died at level 50 by Knight X and by a dragon", PlayerCyclopedia::describeDeath(death("Knight X", "a dragon")));
	EXPECT_EQ("Died at level 50 by Knight X <b>(soloed)</b>", PlayerCyclopedia::describeDeath(death("Knight X", "Knight X")));
}

TEST(CyclopediaHistoryTest, OtherParticipantsAreCountedPlayersFirstWithoutTheKiller) {
	const auto participants = "Name: Knight X\nType: player\n"
							  "Name: rat\nType: monster\n"
							  "Name: Druid Y\r\nType: player\r\n"
							  "Name: rat\nType: monster\n";
	EXPECT_EQ("Died at level 50 by Knight X <b>(soloed)</b>, Druid Y, 2x rat", PlayerCyclopedia::describeDeath(death("Knight X", "Knight X", participants)));
}

TEST(CyclopediaHistoryTest, AMalformedParticipantIsSkipped) {
	const auto participants = "Name: Lonely\nName: rat\nType: monster\n";
	EXPECT_EQ("Died at level 50 by a dragon, rat", PlayerCyclopedia::describeDeath(death("a dragon", "", participants)));
}

TEST(CyclopediaHistoryTest, AKillIsUnjustifiedOnlyForTheSideItWasUnjustifiedFor) {
	PvpKillRow kill { 1'800'000'000, "Knight X", "Druid Y", true, false, "Victim" };
	EXPECT_EQ(CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_UNJUSTIFIED, PlayerCyclopedia::pvpKillStatus(kill, "Knight X"));
	EXPECT_EQ(CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_JUSTIFIED, PlayerCyclopedia::pvpKillStatus(kill, "Druid Y"));

	kill = PvpKillRow { 1'800'000'000, "Knight X", "Druid Y", false, true, "Victim" };
	EXPECT_EQ(CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_JUSTIFIED, PlayerCyclopedia::pvpKillStatus(kill, "Knight X"));
	EXPECT_EQ(CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_UNJUSTIFIED, PlayerCyclopedia::pvpKillStatus(kill, "Druid Y"));
}
