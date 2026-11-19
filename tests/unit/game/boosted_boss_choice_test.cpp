/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "io/io_bosstiary.hpp"

// The boss of the day: today's stored one stays; on another day one of the archfoe bosses is picked.
namespace {
	const std::vector<std::pair<uint16_t, std::string>> kArchfoes { { 2367, "Zarcorix" }, { 1984, "Gorzindel" }, { 1985, "Lokathmor" } };
	const auto kFirst = [](size_t) { return size_t { 0 }; };
}

TEST(BoostedBossChoiceTest, TodaysStoredBossStays) {
	const auto choice = IOBosstiary::chooseBoostedBoss(BoostedCreatureRecord { 19, "Gorzindel", 1984 }, 19, kArchfoes, kFirst);
	ASSERT_TRUE(choice.has_value());
	EXPECT_FALSE(choice->isNew);
	EXPECT_EQ(1984, choice->boss.raceId);
	// Even with no candidate left: nothing has to be picked.
	EXPECT_TRUE(IOBosstiary::chooseBoostedBoss(BoostedCreatureRecord { 19, "Gorzindel", 1984 }, 19, {}, kFirst).has_value());
}

TEST(BoostedBossChoiceTest, AnotherDayPicksACandidateForToday) {
	const auto choice = IOBosstiary::chooseBoostedBoss(BoostedCreatureRecord { 18, "Gorzindel", 1984 }, 19, kArchfoes, [](size_t count) { return count - 1; });
	ASSERT_TRUE(choice.has_value());
	EXPECT_TRUE(choice->isNew);
	EXPECT_EQ(19, choice->boss.day);
	EXPECT_EQ(1985, choice->boss.raceId);
	EXPECT_EQ("Lokathmor", choice->boss.name);
	// A picker out of range cannot index past the list.
	EXPECT_EQ(1985, IOBosstiary::chooseBoostedBoss(BoostedCreatureRecord { 18, "x", 1 }, 19, kArchfoes, [](size_t) { return size_t { 99 }; })->boss.raceId);
}

TEST(BoostedBossChoiceTest, NothingStoredOrNoCandidateGivesNoBoss) {
	EXPECT_FALSE(IOBosstiary::chooseBoostedBoss(std::nullopt, 19, kArchfoes, kFirst).has_value());
	EXPECT_FALSE(IOBosstiary::chooseBoostedBoss(BoostedCreatureRecord { 18, "x", 1 }, 19, {}, kFirst).has_value());
}
