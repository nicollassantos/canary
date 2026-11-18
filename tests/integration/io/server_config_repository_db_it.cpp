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

} // namespace it_server_config_repository_db
