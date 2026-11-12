/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player_repository_db.hpp"
#include "utils/tools.hpp"
#include "test_env.hpp"

namespace it_player_repo_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 50000000;
		return base + counter.fetch_add(1);
	}

	struct StoredPlayer {
		uint32_t guid;
		std::string name;
		uint32_t accountId;
	};

	StoredPlayer createPlayer(Database &db, const std::string &namePrefix) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'player_acc_{}', 'player_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		const auto name = fmt::format("{} {}", namePrefix, seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ({}, {}, '')", db.escapeString(name), seed)));
		return { static_cast<uint32_t>(db.getLastInsertId()), name, seed };
	}

	uint64_t storedBalance(Database &db, uint32_t guid) {
		const auto result = db.storeQuery(fmt::format("SELECT `balance` FROM `players` WHERE `id` = {}", guid));
		return result ? result->getNumber<uint64_t>("balance") : 0;
	}

	TEST(PlayerRepositoryDBTest, NameAndGuidResolveEachOtherWithQuotes) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto stored = createPlayer(db, "O'Brien");
			PlayerRepositoryDB repository(db);

			EXPECT_EQ(stored.guid, repository.getGuidByName(stored.name));
			EXPECT_EQ(stored.name, repository.getNameByGuid(stored.guid));
		})();
	}

	TEST(PlayerRepositoryDBTest, LookupWithOtherCaseReturnsStoredCasing) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto stored = createPlayer(db, "Mixed Case");
			PlayerRepositoryDB repository(db);

			std::string typed = asLowerCaseString(stored.name);
			ASSERT_TRUE(repository.formatPlayerName(typed));
			EXPECT_EQ(stored.name, typed);

			uint32_t guid = 0;
			bool specialVip = true;
			std::string lookup = asLowerCaseString(stored.name);
			ASSERT_TRUE(repository.getGuidByNameEx(guid, specialVip, lookup));
			EXPECT_EQ(stored.guid, guid);
			EXPECT_EQ(stored.name, lookup);
		})();
	}

	TEST(PlayerRepositoryDBTest, IncreaseBankBalanceAddsToStoredBalance) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto stored = createPlayer(db, "Banker");
			PlayerRepositoryDB repository(db);

			repository.increaseBankBalance(stored.guid, 700);
			repository.increaseBankBalance(stored.guid, 300);

			EXPECT_EQ(1000U, storedBalance(db, stored.guid));
		})();
	}

	TEST(PlayerRepositoryDBTest, UnknownPlayerLookupsFail) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			PlayerRepositoryDB repository(db);
			const std::string unknown = "No Such Player 0x7FFFFFF0";

			EXPECT_EQ(0U, repository.getGuidByName(unknown));
			EXPECT_TRUE(repository.getNameByGuid(0x7FFFFFF0).empty());

			std::string name = unknown;
			EXPECT_FALSE(repository.formatPlayerName(name));
			EXPECT_EQ(unknown, name);

			uint32_t guid = 0;
			bool specialVip = false;
			EXPECT_FALSE(repository.getGuidByNameEx(guid, specialVip, name));
		})();
	}

	std::vector<uint32_t> onlineGuids(Database &db) {
		std::vector<uint32_t> guids;
		if (const auto result = db.storeQuery("SELECT `player_id` FROM `players_online` ORDER BY `player_id`")) {
			do {
				guids.push_back(result->getNumber<uint32_t>("player_id"));
			} while (result->next());
		}
		return guids;
	}

	void removePlayers(Database &db, const std::vector<StoredPlayer> &players) {
		for (const auto &player : players) {
			EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `players` WHERE `id` = {}", player.guid)));
			EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `accounts` WHERE `id` = {}", player.accountId)));
		}
	}

	// updateOnlinePlayers opens its own transaction, and MySQL's BEGIN would commit the
	// databaseTest harness transaction. So this test runs outside it and removes its own rows.
	TEST(PlayerRepositoryDBTest, UpdateOnlinePlayersKeepsExactlyTheGivenPlayers) {
		TestDatabase::init();
		auto &db = g_database();
		const auto first = createPlayer(db, "Online A");
		const auto second = createPlayer(db, "Online B");
		PlayerRepositoryDB repository(db);

		ASSERT_TRUE(repository.updateOnlinePlayers({ second.guid, first.guid }));
		EXPECT_EQ((std::vector<uint32_t> { first.guid, second.guid }), onlineGuids(db));

		ASSERT_TRUE(repository.updateOnlinePlayers({ first.guid }));
		EXPECT_EQ((std::vector<uint32_t> { first.guid }), onlineGuids(db));

		ASSERT_TRUE(repository.updateOnlinePlayers({}));
		EXPECT_TRUE(onlineGuids(db).empty());

		// An empty table takes the count-only path.
		EXPECT_TRUE(repository.updateOnlinePlayers({}));

		removePlayers(db, { first, second });
	}

} // namespace it_player_repo_db
