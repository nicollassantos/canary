/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "server/network/connection/ip_connection_limiter.hpp"

namespace {
	constexpr uint32_t Attacker = 0x0A000001;
	constexpr uint32_t Neighbour = 0x0A000002;

	const void* connection(int id) {
		static int slots[64];
		return &slots[id];
	}
}

TEST(IpConnectionLimiterTest, RefusesTheConnectionAboveTheLimit) {
	IpConnectionLimiter limiter;
	EXPECT_TRUE(limiter.admit(connection(0), Attacker, 2));
	EXPECT_TRUE(limiter.admit(connection(1), Attacker, 2));
	EXPECT_FALSE(limiter.admit(connection(2), Attacker, 2));
	EXPECT_EQ(2U, limiter.openFrom(Attacker));
}

TEST(IpConnectionLimiterTest, OtherIpsAreNotAffected) {
	IpConnectionLimiter limiter;
	EXPECT_TRUE(limiter.admit(connection(0), Attacker, 1));
	EXPECT_FALSE(limiter.admit(connection(1), Attacker, 1));
	EXPECT_TRUE(limiter.admit(connection(2), Neighbour, 1));
}

TEST(IpConnectionLimiterTest, ClosingAConnectionFreesItsSlot) {
	IpConnectionLimiter limiter;
	EXPECT_TRUE(limiter.admit(connection(0), Attacker, 1));
	limiter.release(connection(0));

	EXPECT_EQ(0U, limiter.openFrom(Attacker));
	EXPECT_TRUE(limiter.admit(connection(1), Attacker, 1));
}

TEST(IpConnectionLimiterTest, RefusedOrUnknownConnectionsReleaseNothing) {
	IpConnectionLimiter limiter;
	EXPECT_TRUE(limiter.admit(connection(0), Attacker, 1));
	EXPECT_FALSE(limiter.admit(connection(1), Attacker, 1));

	limiter.release(connection(1));
	limiter.release(connection(9));

	EXPECT_EQ(1U, limiter.openFrom(Attacker));
}

TEST(IpConnectionLimiterTest, ZeroDisablesTheLimitAndAdmittingTwiceCountsOnce) {
	IpConnectionLimiter limiter;
	for (int id = 0; id < 20; ++id) {
		EXPECT_TRUE(limiter.admit(connection(id), Attacker, 0));
	}
	EXPECT_TRUE(limiter.admit(connection(0), Attacker, 0));
	EXPECT_EQ(20U, limiter.openFrom(Attacker));
}

// Carrier-grade NAT: many real players share one IP. Once logged in they are released, so only
// connections still waiting to log in count.
TEST(IpConnectionLimiterTest, ManyLoggedInPlayersBehindOneIpDoNotBlockTheNext) {
	IpConnectionLimiter limiter;
	for (int id = 0; id < 40; ++id) {
		ASSERT_TRUE(limiter.admit(connection(id), Attacker, 2));
		limiter.release(connection(id)); // logged in
	}

	EXPECT_TRUE(limiter.admit(connection(40), Attacker, 2));
	EXPECT_TRUE(limiter.admit(connection(41), Attacker, 2));
	EXPECT_FALSE(limiter.admit(connection(42), Attacker, 2));
}
