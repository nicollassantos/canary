/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player_progress_repository_db.hpp"
#include "test_env.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <atomic>
	#include <chrono>
#endif

namespace it_player_progress_repository_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x00FFFFFF) + 80000000;
		return base + counter.fetch_add(1);
	}

	struct Stored {
		uint32_t guid;
		std::string name;
		uint32_t accountId;
	};

	Stored createPlayer(Database &db, const std::string &prefix, uint32_t accountId = 0, uint32_t level = 1, uint16_t vocation = 0) {
		const auto seed = uniqueSeed();
		if (accountId == 0) {
			accountId = seed;
			EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'prog_acc_{}', 'prog_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		}
		const auto name = fmt::format("{} {}", prefix, seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`, `level`, `vocation`) VALUES ({}, {}, '', {}, {})", db.escapeString(name), accountId, level, vocation)));
		return { static_cast<uint32_t>(db.getLastInsertId()), name, accountId };
	}

	TEST(PlayerProgressRepositoryDBTest, ForgeHistoryIsAddedReadAndRemoved) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto player = createPlayer(db, "Forger");
			PlayerProgressRepositoryDB repository(db);
			ForgeHistory fusion;
			fusion.actionType = ForgeAction_t::FUSION;
			fusion.description = "Fused it's sword";
			fusion.createdAt = 1'800'000'000'000;
			fusion.success = true;
			ASSERT_TRUE(repository.saveForgeHistory(player.guid, {}, { fusion }));

			auto history = repository.loadForgeHistory(player.guid);
			ASSERT_EQ(1U, history.size());
			EXPECT_EQ("Fused it's sword", history.front().description);
			EXPECT_EQ(1'800'000'000'000U, history.front().createdAt);
			EXPECT_TRUE(history.front().success);

			ASSERT_TRUE(repository.saveForgeHistory(player.guid, { history.front().id }, {}));
			EXPECT_TRUE(repository.loadForgeHistory(player.guid).empty());
		})();
	}

	TEST(PlayerProgressRepositoryDBTest, WheelSlotsAreABlobThatASaveReplaces) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto player = createPlayer(db, "Wheeler");
			PlayerProgressRepositoryDB repository(db);
			EXPECT_FALSE(repository.loadWheelSlots(player.guid).has_value());

			const std::string first("\x01\x05\x00\x02'\x00", 6);
			ASSERT_TRUE(repository.saveWheelSlots(player.guid, first));
			EXPECT_EQ(std::optional<std::string>(first), repository.loadWheelSlots(player.guid));
			const std::string second("\x01\x07\x00", 3);
			ASSERT_TRUE(repository.saveWheelSlots(player.guid, second));
			EXPECT_EQ(std::optional<std::string>(second), repository.loadWheelSlots(player.guid));
			EXPECT_TRUE(repository.saveWheelSlots(player.guid, "")); // nothing to write
		})();
	}

	TEST(PlayerProgressRepositoryDBTest, ASkillLeaderNeedsAPlainColumnName) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			createPlayer(db, "Leader", 0, 5000);
			PlayerProgressRepositoryDB repository(db);
			EXPECT_TRUE(repository.findSkillLeader("level").has_value());
			EXPECT_FALSE(repository.findSkillLeader("level` > 0 OR 1=1 -- ").has_value());
			EXPECT_FALSE(repository.findSkillLeader("").has_value());
		})();
	}

	TEST(PlayerProgressRepositoryDBTest, HirelingsAndTheCharactersOfAnAccountAreRead) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto main = createPlayer(db, "Main", 0, 120, 4);
			const auto alt = createPlayer(db, "O'Alt", main.accountId, 30, 1);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `player_hirelings` (`player_id`, `name`) VALUES ({}, 'Helper'), ({}, 'Helper Two')", main.guid, main.guid)));
			PlayerProgressRepositoryDB repository(db);

			EXPECT_EQ(std::optional<uint32_t>(2), repository.countHirelings(main.guid));
			EXPECT_EQ(std::optional<uint32_t>(0), repository.countHirelings(alt.guid));

			auto characters = repository.loadCharacterSummaries({ main.name, alt.name });
			ASSERT_EQ(2U, characters.size());
			std::ranges::sort(characters, {}, &CharacterSummary::level);
			EXPECT_EQ(alt.name, characters[0].name);
			EXPECT_EQ(30U, characters[0].level);
			EXPECT_EQ(1, characters[0].vocation);
			EXPECT_EQ(120U, characters[1].level);
			EXPECT_TRUE(repository.loadCharacterSummaries({}).empty());
		})();
	}

	TEST(PlayerProgressRepositoryDBTest, RecentDeathsAndKillsArePagedWithTheirTotal) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto victim = createPlayer(db, "Victim");
			const auto killer = createPlayer(db, "Killer");
			for (int i = 0; i < 3; ++i) {
				ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `player_deaths` (`player_id`, `time`, `level`, `killed_by`, `is_player`, `mostdamage_by`, `mostdamage_is_player`, `unjustified`, `participants`) VALUES ({}, UNIX_TIMESTAMP() - {}, 40, {}, 1, 'a dragon', 0, {}, '')", victim.guid, 100 + i, db.escapeString(killer.name), i == 0 ? 1 : 0)));
			}
			PlayerProgressRepositoryDB repository(db);

			const auto deaths = PlayerProgressRepositoryDB::readRecentDeaths(db.storeQuery(PlayerProgressRepositoryDB::recentDeathsQuery(victim.guid, 0, 2)));
			ASSERT_TRUE(deaths.has_value());
			EXPECT_EQ(3U, deaths->totalEntries);
			ASSERT_EQ(2U, deaths->rows.size());
			EXPECT_EQ(killer.name, deaths->rows.front().killedBy);
			EXPECT_EQ("a dragon", deaths->rows.front().mostDamageBy);
			EXPECT_FALSE(PlayerProgressRepositoryDB::readRecentDeaths(db.storeQuery(PlayerProgressRepositoryDB::recentDeathsQuery(killer.guid, 0, 2))).has_value());

			const auto kills = PlayerProgressRepositoryDB::readRecentPvpKills(db.storeQuery(repository.recentPvpKillsQuery(killer.name, 0, 10)));
			ASSERT_TRUE(kills.has_value());
			EXPECT_EQ(3U, kills->totalEntries);
			ASSERT_EQ(3U, kills->rows.size());
			EXPECT_EQ(victim.name, kills->rows.front().victim);
			EXPECT_TRUE(kills->rows.front().unjustified); // the most recent one
		})();
	}

} // namespace it_player_progress_repository_db
