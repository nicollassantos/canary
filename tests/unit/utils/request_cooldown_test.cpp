/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "utils/request_cooldown.hpp"

TEST(RequestCooldownTest, FirstRequestPassesAndARepeatInsideTheIntervalIsRefused) {
	RequestCooldown cooldown(500);

	EXPECT_TRUE(cooldown.tryTake(7, 1000));
	EXPECT_FALSE(cooldown.tryTake(7, 1499));
	EXPECT_TRUE(cooldown.tryTake(7, 1500));
}

TEST(RequestCooldownTest, KeysAreIndependent) {
	RequestCooldown cooldown(500);

	EXPECT_TRUE(cooldown.tryTake(1, 1000));
	EXPECT_TRUE(cooldown.tryTake(2, 1000));
	EXPECT_FALSE(cooldown.tryTake(1, 1100));
}

// Pairs of ids as one key (ChatService's invite notice: owner and invited player): two owners inviting the
// same player are independent, though the low 32 bits are equal.
TEST(RequestCooldownTest, PairKeysUseAllSixtyFourBits) {
	RequestCooldown cooldown(2000);
	const auto pair = [](uint32_t owner, uint32_t invited) {
		return (static_cast<uint64_t>(owner) << 32) | invited;
	};

	EXPECT_TRUE(cooldown.tryTake(pair(1, 50), 1000));
	EXPECT_TRUE(cooldown.tryTake(pair(2, 50), 1000));
	EXPECT_FALSE(cooldown.tryTake(pair(1, 50), 2999));
	EXPECT_TRUE(cooldown.tryTake(pair(1, 50), 3000));
}

TEST(RequestCooldownTest, RefusedRequestsDoNotExtendTheWait) {
	RequestCooldown cooldown(500);

	EXPECT_TRUE(cooldown.tryTake(7, 1000));
	EXPECT_FALSE(cooldown.tryTake(7, 1400));
	EXPECT_TRUE(cooldown.tryTake(7, 1500));
}

// Keys come from the network: stale entries are dropped once the map gets large.
TEST(RequestCooldownTest, StaleKeysArePrunedWhenTheMapGetsLarge) {
	RequestCooldown cooldown(500);
	for (uint32_t key = 0; key < RequestCooldown::PruneThreshold; ++key) {
		ASSERT_TRUE(cooldown.tryTake(key, 1000));
	}

	EXPECT_TRUE(cooldown.tryTake(RequestCooldown::PruneThreshold, 2000));
	EXPECT_EQ(1U, cooldown.size());
}
