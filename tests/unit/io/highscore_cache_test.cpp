/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/highscore/highscore_cache.hpp"
#include "io/highscore_repository_db.hpp"

#include <map>
#include <string>

namespace {

	using Clock = std::chrono::steady_clock;

	struct Entry {
		Clock::time_point timestamp;
	};

	constexpr auto Ttl = std::chrono::minutes(10);

} // namespace

TEST(HighscoreCachePruneTest, DropsOnlyExpiredEntries) {
	const auto now = Clock::now();
	std::map<std::string, Entry> cache {
		{ "fresh", { now - std::chrono::minutes(1) } },
		{ "expired", { now - std::chrono::minutes(11) } },
	};

	EXPECT_EQ(1U, highscore_cache::prune(cache, now, Ttl));
	EXPECT_TRUE(cache.contains("fresh"));
	EXPECT_FALSE(cache.contains("expired"));
}

TEST(HighscoreCachePruneTest, ClearsWhenStillAtTheLimit) {
	const auto now = Clock::now();
	std::map<std::string, Entry> cache;
	for (int i = 0; i < 3; ++i) {
		cache.emplace(std::to_string(i), Entry { now });
	}

	EXPECT_EQ(3U, highscore_cache::prune(cache, now, Ttl, 3));
	EXPECT_TRUE(cache.empty());
}

TEST(HighscoreCachePruneTest, KeepsFreshEntriesBelowTheLimit) {
	const auto now = Clock::now();
	std::map<std::string, Entry> cache { { "a", { now } }, { "b", { now } } };

	EXPECT_EQ(0U, highscore_cache::prune(cache, now, Ttl, 3));
	EXPECT_EQ(2U, cache.size());
}

// A client can ask for any page and page size; every distinct pair used to stay cached forever.
TEST(HighscoreQueryCacheTest, ClientChosenPagesCannotGrowTheCacheWithoutBound) {
	HighscoreRepositoryDB repository;

	for (uint32_t page = 1; page <= highscore_cache::MaxEntries * 2; ++page) {
		ASSERT_FALSE(repository.generateHighscoreOrGetCachedQueryForEntries("experience", page, 10, 0xFFFFFFFF).empty());
	}

	EXPECT_LE(repository.queryCacheSize(), highscore_cache::MaxEntries);
}
