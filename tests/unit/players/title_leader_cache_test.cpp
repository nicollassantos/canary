/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/title_leader_cache.hpp"

namespace {
	const TitleLeaderCache::Clock::time_point T0 = TitleLeaderCache::Clock::time_point {} + std::chrono::hours(1);
} // namespace

// Regression (Phase 25 load test): every login ran one ranking query per highscore title.
TEST(TitleLeaderCacheTest, OneQueryPerCategoryPerMaxAge) {
	TitleLeaderCache cache;
	int queries = 0;
	const auto fetch = [&queries]() -> std::optional<uint32_t> {
		++queries;
		return 42;
	};

	for (int login = 0; login < 50; ++login) {
		EXPECT_EQ(std::optional<uint32_t> { 42 }, cache.leader(3, T0 + std::chrono::seconds(login), fetch));
	}
	EXPECT_EQ(1, queries);

	EXPECT_EQ(std::optional<uint32_t> { 42 }, cache.leader(4, T0, fetch)); // another category
	EXPECT_EQ(2, queries);

	EXPECT_EQ(std::optional<uint32_t> { 42 }, cache.leader(3, T0 + TitleLeaderCache::MaxAge, fetch)); // expired
	EXPECT_EQ(3, queries);
}

TEST(TitleLeaderCacheTest, NoLeaderIsCachedToo) {
	TitleLeaderCache cache;
	int queries = 0;
	const auto none = [&queries]() -> std::optional<uint32_t> {
		++queries;
		return std::nullopt;
	};
	EXPECT_EQ(std::nullopt, cache.leader(7, T0, none));
	EXPECT_EQ(std::nullopt, cache.leader(7, T0 + std::chrono::seconds(1), none));
	EXPECT_EQ(1, queries);
}
