/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/movement/teleport_guard.hpp"

class TeleportGuardTest : public ::testing::Test {
protected:
	void SetUp() override {
		g_teleportGuard().reset();
	}

	TeleportGuard &guard = g_teleportGuard();

	// Dummy identities — never dereferenced by TeleportGuard.
	inline static char sentinel1 = 0;
	inline static char sentinel2 = 0;

	static const Thing* t1() { return reinterpret_cast<const Thing*>(&sentinel1); }
	static const Thing* t2() { return reinterpret_cast<const Thing*>(&sentinel2); }
};

// --- Recursion stack ---

TEST_F(TeleportGuardTest, TryEnterStack_ReturnsGuard_FirstTime) {
	auto g = guard.tryEnterStack(t1());
	EXPECT_NE(nullptr, g.get());
}

TEST_F(TeleportGuardTest, TryEnterStack_ReturnsNull_WhenAlreadyInStack) {
	auto g = guard.tryEnterStack(t1());
	ASSERT_NE(nullptr, g.get());
	auto g2 = guard.tryEnterStack(t1());
	EXPECT_EQ(nullptr, g2.get());
}

TEST_F(TeleportGuardTest, TryEnterStack_AllowsDifferentThings_Simultaneously) {
	auto g1 = guard.tryEnterStack(t1());
	auto g2 = guard.tryEnterStack(t2());
	EXPECT_NE(nullptr, g1.get());
	EXPECT_NE(nullptr, g2.get());
}

TEST_F(TeleportGuardTest, TryEnterStack_AllowsReentry_AfterGuardDestroyed) {
	{
		auto g = guard.tryEnterStack(t1());
		ASSERT_NE(nullptr, g.get());
	}
	// Destructor ran → t1 popped from stack
	auto g2 = guard.tryEnterStack(t1());
	EXPECT_NE(nullptr, g2.get());
}

// --- Rate limiting ---

TEST_F(TeleportGuardTest, ShouldBlockRate_ReturnsFalse_OnFirstCall) {
	EXPECT_FALSE(guard.shouldBlockRate(42, 1000));
}

TEST_F(TeleportGuardTest, ShouldBlockRate_ReturnsFalse_UnderBurstLimit) {
	// TELEPORT_SPAM_LIMIT = 10; calls 1–10 in same window are allowed
	const uint64_t now = 1000;
	for (int i = 0; i < 10; ++i) {
		EXPECT_FALSE(guard.shouldBlockRate(1, now)) << "call " << i + 1;
	}
}

TEST_F(TeleportGuardTest, ShouldBlockRate_ReturnsTrue_AfterBurstExceeded) {
	const uint64_t now = 1000;
	for (int i = 0; i < 10; ++i) {
		guard.shouldBlockRate(1, now);
	}
	// 11th call in same window → blocked
	EXPECT_TRUE(guard.shouldBlockRate(1, now));
}

TEST_F(TeleportGuardTest, ShouldBlockRate_ResetsAfterWindow) {
	const uint64_t t0 = 1000;
	for (int i = 0; i <= 10; ++i) {
		guard.shouldBlockRate(1, t0); // exhaust + block
	}
	// Advance past both SPAM_BLOCK_MS (1000ms) and SPAM_WINDOW_MS (1000ms)
	const uint64_t t1 = t0 + 2001;
	EXPECT_FALSE(guard.shouldBlockRate(1, t1));
}

TEST_F(TeleportGuardTest, ShouldBlockRate_IndependentPerKey) {
	const uint64_t now = 1000;
	for (int i = 0; i <= 10; ++i) {
		guard.shouldBlockRate(1, now);
	}
	// Key 2 has no history → still allowed
	EXPECT_FALSE(guard.shouldBlockRate(2, now));
}

// --- recordBlock / snapshot ---

TEST_F(TeleportGuardTest, RecordBlock_FirstCall_ShouldLog) {
	const auto snap = guard.recordBlock(99, 1000);
	EXPECT_TRUE(snap.shouldLog);
	EXPECT_EQ(1u, snap.blockedEvents);
	EXPECT_EQ(0u, snap.suppressedLogs);
}

TEST_F(TeleportGuardTest, RecordBlock_SecondCall_SuppressedWithinDelay) {
	guard.recordBlock(99, 1000);               // first: shouldLog=true, sets nextLogMs=2000
	const auto snap = guard.recordBlock(99, 1000); // same timestamp → suppressed
	EXPECT_FALSE(snap.shouldLog);
}

TEST_F(TeleportGuardTest, RecordBlock_LogsAgain_AfterDelay) {
	const auto snap1 = guard.recordBlock(99, 0);
	ASSERT_TRUE(snap1.shouldLog);  // nextLogMs = 0 + 1000
	guard.recordBlock(99, 500);    // suppressed
	const auto snap2 = guard.recordBlock(99, 1001); // past nextLogMs → logs again
	EXPECT_TRUE(snap2.shouldLog);
	EXPECT_EQ(3u, snap2.blockedEvents);
}
