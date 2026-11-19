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

// Tibia's manual: the boss is boosted "with every server save [...] until the next server save". Its day is the
// one of the last server save, whenever the server was started.
namespace {
	std::tm localTime(int year, int month, int day, int hour, int minute, int second) {
		std::tm time {};
		time.tm_year = year - 1900;
		time.tm_mon = month - 1;
		time.tm_mday = day;
		time.tm_hour = hour;
		time.tm_min = minute;
		time.tm_sec = second;
		return time;
	}
}

TEST(BoostedBossChoiceTest, TheBossDayStartsAtTheServerSave) {
	const IOBosstiary::ServerSaveTime six { 6, 0, 0 };
	EXPECT_EQ(19, IOBosstiary::bossDay(localTime(2026, 11, 20, 5, 59, 59), six)); // a restart before it keeps the boss
	EXPECT_EQ(20, IOBosstiary::bossDay(localTime(2026, 11, 20, 6, 0, 0), six));
	EXPECT_EQ(20, IOBosstiary::bossDay(localTime(2026, 11, 20, 23, 59, 59), six));
	EXPECT_EQ(31, IOBosstiary::bossDay(localTime(2026, 11, 1, 5, 0, 0), six)); // October 31st
	EXPECT_EQ(29, IOBosstiary::bossDay(localTime(2028, 3, 1, 0, 30, 0), six)); // leap year
	EXPECT_EQ(28, IOBosstiary::bossDay(localTime(2027, 3, 1, 0, 30, 0), six));
	EXPECT_EQ(20, IOBosstiary::bossDay(localTime(2026, 11, 20, 0, 0, 0), IOBosstiary::ServerSaveTime { 0, 0, 0 }));
}

TEST(BoostedBossChoiceTest, TheServerSaveTimeIsHoursMinutesAndOptionalSeconds) {
	const auto withSeconds = IOBosstiary::parseServerSaveTime("06:30:15");
	ASSERT_TRUE(withSeconds.has_value());
	EXPECT_EQ(6, withSeconds->hour);
	EXPECT_EQ(30, withSeconds->minute);
	EXPECT_EQ(15, withSeconds->second);
	const auto withoutSeconds = IOBosstiary::parseServerSaveTime("6:05");
	ASSERT_TRUE(withoutSeconds.has_value());
	EXPECT_EQ(6, withoutSeconds->hour);
	EXPECT_EQ(5, withoutSeconds->minute);
	EXPECT_EQ(0, withoutSeconds->second);
	for (const auto *bad : { "", "6", "24:00", "06:60", "06:00:60", "06:00:00:00", "06::00", ":30", "06:", "ab:cd", "006:00", " 06:00" }) {
		EXPECT_FALSE(IOBosstiary::parseServerSaveTime(bad).has_value()) << bad;
	}
}
