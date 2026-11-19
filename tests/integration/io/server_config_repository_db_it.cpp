/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/server_config_repository_db.hpp"
#include "test_env.hpp"

namespace it_server_config_repository_db {

	TEST(ServerConfigRepositoryDBTest, ValuesAreFoundInsertedAndUpdatedWithQuotesKept) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			ServerConfigRepositoryDB repository(db);
			const std::string key = "fork_it_key'quoted";
			EXPECT_FALSE(repository.find(key).has_value());
			repository.insert(key, "it's 1");
			EXPECT_EQ(std::optional<std::string>("it's 1"), repository.find(key));
			repository.update(key, "2");
			EXPECT_EQ(std::optional<std::string>("2"), repository.find(key));
		})();
	}

	TEST(ServerConfigRepositoryDBTest, TheBoostedCreatureIsReadAndReplaced) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			ASSERT_TRUE(db.executeQuery("DELETE FROM `boosted_creature`"));
			ServerConfigRepositoryDB repository(db);
			EXPECT_FALSE(repository.loadBoostedCreature().has_value());

			ASSERT_TRUE(db.executeQuery("INSERT INTO `boosted_creature` (`boostname`, `date`, `raceid`) VALUES ('Rat', '3', '21')"));
			auto stored = repository.loadBoostedCreature();
			ASSERT_TRUE(stored.has_value());
			EXPECT_EQ(3, stored->day);
			EXPECT_EQ("Rat", stored->name);
			EXPECT_EQ(21, stored->raceId);

			ASSERT_TRUE(repository.storeBoostedCreature({ 4, "Dragon Lord's Kin", 39 }, { 39, 1, 2, 3, 4, 0, 0 }));
			stored = repository.loadBoostedCreature();
			ASSERT_TRUE(stored.has_value());
			EXPECT_EQ(4, stored->day);
			EXPECT_EQ("Dragon Lord's Kin", stored->name);
			EXPECT_EQ(39, stored->raceId);
			const auto look = db.storeQuery("SELECT `looktype`, `lookhead` FROM `boosted_creature`");
			ASSERT_NE(nullptr, look);
			EXPECT_EQ(39, look->getNumber<int>("looktype"));
			EXPECT_EQ(3, look->getNumber<int>("lookhead"));
		})();
	}

	// storeBoostedBoss commits its own transaction, so it cannot run inside databaseTest's BEGIN/ROLLBACK:
	// the rows it touches are put back at the end instead.
	TEST(ServerConfigRepositoryDBTest, TheBoostedBossIsStoredAndLeavesThePlayersSlotsTogether) {
		auto &db = g_database();
		TestDatabase::init();
		const auto before = db.storeQuery("SELECT `date`, `boostname`, `raceid` FROM `boosted_boss`");
		ASSERT_NE(nullptr, before);
		const auto beforeDate = before->getString("date");
		const auto beforeName = before->getString("boostname");
		const auto beforeRace = before->getString("raceid");

		const auto seed = static_cast<uint32_t>((std::chrono::steady_clock::now().time_since_epoch().count() & 0x0FFFFFFF) + 50000000);
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({0}, 'boss_acc_{0}', 'boss_{0}@test.com', '', 1, 0, 0, 0)", seed)));
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('boss_player_{0}', {0}, '')", seed)));
		const auto guid = db.getLastInsertId();
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `player_bosstiary` (`player_id`, `bossIdSlotOne`, `bossIdSlotTwo`, `removeTimes`, `tracker`) VALUES ({}, 2367, 1234, 0, '')", guid)));

		ServerConfigRepositoryDB repository(db);
		ASSERT_TRUE(repository.storeBoostedBoss(BoostedCreatureRecord { 7, "O'Zarcorix", 2367 }, BoostedBossLook { 0, 1065, 1, 2, 3, 4, 0, 0 }));

		const auto loaded = repository.loadBoostedBoss();
		ASSERT_TRUE(loaded.has_value());
		EXPECT_EQ(7, loaded->day);
		EXPECT_EQ("O'Zarcorix", loaded->name);
		EXPECT_EQ(2367, loaded->raceId);
		const auto slots = db.storeQuery(fmt::format("SELECT `bossIdSlotOne`, `bossIdSlotTwo` FROM `player_bosstiary` WHERE `player_id` = {}", guid));
		ASSERT_NE(nullptr, slots);
		EXPECT_EQ(0U, slots->getNumber<uint32_t>("bossIdSlotOne")); // the boss of the day left the slot
		EXPECT_EQ(1234U, slots->getNumber<uint32_t>("bossIdSlotTwo")); // another boss stays

		EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `player_bosstiary` WHERE `player_id` = {}", guid)));
		EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `players` WHERE `id` = {}", guid)));
		EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `accounts` WHERE `id` = {}", seed)));
		EXPECT_TRUE(db.executeQuery(fmt::format("UPDATE `boosted_boss` SET `date` = {}, `boostname` = {}, `raceid` = {}", db.escapeString(beforeDate), db.escapeString(beforeName), db.escapeString(beforeRace))));
	}
} // namespace it_server_config_repository_db
