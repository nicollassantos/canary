/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/vocations/vocation.hpp"
#include "io/highscore_repository_db.hpp"
#include "test_env.hpp"

namespace it_highscore_repo_db {

	constexpr uint32_t AllVocations = 0xFFFFFFFF;
	constexpr uint16_t GamemasterGroup = 4;

	// The table may already hold players; seeded experience sits far above any real value so the
	// seeded players always own the first ranks.
	constexpr uint64_t Top = 9'000'000'000'000'000;

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 60000000;
		return base + counter.fetch_add(1);
	}

	std::string createPlayer(Database &db, uint64_t experience, uint16_t vocation = 0, uint16_t group = 1) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'hs_acc_{}', 'hs_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		const auto name = fmt::format("Ranked {}", seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`, `experience`, `vocation`, `group_id`) VALUES ('{}', {}, '', {}, {}, {})", name, seed, experience, vocation, group)));
		return name;
	}

	uint32_t rankedPlayers(Database &db) {
		const auto result = db.storeQuery(fmt::format("SELECT COUNT(*) AS `count` FROM `players` WHERE `group_id` < {}", GamemasterGroup));
		return result ? result->getNumber<uint32_t>("count") : 0;
	}

	std::optional<HighscorePage> runQuery(Database &db, const std::string &query) {
		return HighscoreRepositoryDB::readHighscorePage(db.storeQuery(query));
	}

	TEST(HighscoreRepositoryDBTest, TopPageRanksTiesTogetherAndSkipsGamemasters) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			createPlayer(db, Top, 0, GamemasterGroup);
			const auto first = createPlayer(db, Top - 1);
			const auto tied = createPlayer(db, Top - 1);
			const auto third = createPlayer(db, Top - 2);
			HighscoreRepositoryDB repository;

			const auto page = runQuery(db, repository.generateHighscoreOrGetCachedQueryForEntries("experience", 1, 3, AllVocations));

			ASSERT_TRUE(page.has_value());
			EXPECT_EQ(1, page->page);
			EXPECT_EQ(rankedPlayers(db), page->total);
			ASSERT_EQ(3U, page->characters.size());
			const std::set<std::string> leaders { page->characters[0].name, page->characters[1].name };
			EXPECT_EQ((std::set<std::string> { first, tied }), leaders);
			EXPECT_EQ(1U, page->characters[0].rank);
			EXPECT_EQ(1U, page->characters[1].rank);
			EXPECT_EQ(third, page->characters[2].name);
			EXPECT_EQ(2U, page->characters[2].rank);
		})();
	}

	TEST(HighscoreRepositoryDBTest, OurRankLoadsThePageHoldingThePlayer) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			createPlayer(db, Top);
			createPlayer(db, Top - 1);
			createPlayer(db, Top - 2);
			const auto fourth = createPlayer(db, Top - 3);
			const auto guid = db.storeQuery(fmt::format("SELECT `id` FROM `players` WHERE `name` = '{}'", fourth))->getNumber<uint32_t>("id");
			HighscoreRepositoryDB repository;

			const auto page = runQuery(db, repository.generateHighscoreOrGetCachedQueryForOurRank("experience", 3, guid, AllVocations));

			ASSERT_TRUE(page.has_value());
			EXPECT_EQ(2, page->page);
			ASSERT_FALSE(page->characters.empty());
			EXPECT_EQ(fourth, page->characters.front().name);
			EXPECT_EQ(4U, page->characters.front().rank);
		})();
	}

	TEST(HighscoreRepositoryDBTest, VocationFilterKeepsPromotionsOfTheBaseVocation) {
		ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));
		auto &db = g_database();
		databaseTest(db, [&db] {
			createPlayer(db, Top, VOCATION_KNIGHT);
			const auto sorcerer = createPlayer(db, Top - 1, VOCATION_SORCERER);
			const auto masterSorcerer = createPlayer(db, Top - 2, VOCATION_MASTER_SORCERER);
			HighscoreRepositoryDB repository;

			const auto page = runQuery(db, repository.generateHighscoreOrGetCachedQueryForEntries("experience", 1, 2, VOCATION_SORCERER));

			ASSERT_TRUE(page.has_value());
			ASSERT_EQ(2U, page->characters.size());
			EXPECT_EQ(sorcerer, page->characters[0].name);
			EXPECT_EQ(masterSorcerer, page->characters[1].name);
		})();
	}

	// (page - 1) wraps for page 0, so the window starts past every row. Pinned as current behaviour.
	TEST(HighscoreRepositoryDBTest, PageZeroFindsNoRows) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			createPlayer(db, Top);
			HighscoreRepositoryDB repository;

			EXPECT_FALSE(runQuery(db, repository.generateHighscoreQuery("experience", 0, 3, AllVocations)).has_value());
		})();
	}

	TEST(HighscoreRepositoryDBTest, EmptyCategoryBuildsNoQuery) {
		HighscoreRepositoryDB repository;
		EXPECT_TRUE(repository.generateHighscoreQuery("", 1, 3, AllVocations).empty());
		EXPECT_FALSE(repository.loadEntries("", 1, 3, AllVocations, [](std::optional<HighscorePage>) { FAIL() << "no query, no callback"; }));
	}

} // namespace it_highscore_repo_db
