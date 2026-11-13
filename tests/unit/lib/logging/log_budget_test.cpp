/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "lib/logging/log_budget.hpp"

using namespace log_budget;

namespace {
	// The global budget keeps per-player windows across tests: give each test its own players.
	constexpr uint32_t FirstPlayerOfPacketTest = 900'001;
	constexpr uint32_t FirstPlayerOfNestingTest = 900'101;

	const Clock::time_point T0 = Clock::time_point {} + std::chrono::hours(1);
} // namespace

TEST(LogBudgetTest, LinesOutsideAClientPacketAreNotLimited) {
	for (int i = 0; i < 100; ++i) {
		EXPECT_TRUE(admit());
	}
}

// Regression: one weapon proficiency packet logged up to 255 errors (one per invalid perk).
TEST(LogBudgetTest, APacketWritesAtMostLinesPerPacket) {
	PacketScope scope(FirstPlayerOfPacketTest, 0xB3, "Attacker");

	for (uint32_t i = 0; i < LinesPerPacket; ++i) {
		EXPECT_TRUE(admit()) << i;
	}
	for (int i = 0; i < 252; ++i) {
		EXPECT_FALSE(admit());
	}
}

TEST(LogBudgetTest, ClosingThePacketLiftsTheLimit) {
	{
		PacketScope scope(FirstPlayerOfNestingTest, 0xB3, "Attacker");
		for (uint32_t i = 0; i < LinesPerPacket; ++i) {
			EXPECT_TRUE(admit());
		}
		EXPECT_FALSE(admit());
		scope.close();
		EXPECT_TRUE(admit());
	}
	EXPECT_TRUE(admit());
}

TEST(LogBudgetTest, APlayerWritesAtMostLinesPerPlayerPerWindowAcrossPackets) {
	Budget budget;
	uint32_t written = 0;
	for (int packet = 0; packet < 30; ++packet) {
		if (budget.admit(7, 0, T0 + std::chrono::milliseconds(packet))) {
			++written;
		}
	}
	EXPECT_EQ(LinesPerPlayerPerWindow, written);

	// Another player is not affected, and the window reopens after PlayerWindow.
	EXPECT_TRUE(budget.admit(8, 0, T0));
	EXPECT_FALSE(budget.admit(7, 0, T0 + PlayerWindow - std::chrono::milliseconds(1)));
	EXPECT_TRUE(budget.admit(7, 0, T0 + PlayerWindow));
}

TEST(LogBudgetTest, PacketLimitAppliesBeforeThePlayerWindow) {
	Budget budget;
	EXPECT_FALSE(budget.admit(7, LinesPerPacket, T0));
	// The refusal by packet did not consume the player's window.
	for (uint32_t i = 0; i < LinesPerPlayerPerWindow; ++i) {
		EXPECT_TRUE(budget.admit(7, 0, T0)) << i;
	}
}

TEST(LogBudgetTest, SuppressedLinesAreSummarisedAtMostOncePerInterval) {
	Budget budget;
	EXPECT_EQ(std::optional<uint64_t> { 5 }, budget.suppressed(5, T0));
	EXPECT_EQ(std::nullopt, budget.suppressed(3, T0 + std::chrono::seconds(1)));
	EXPECT_EQ(std::nullopt, budget.suppressed(4, T0 + SummaryInterval - std::chrono::milliseconds(1)));
	EXPECT_EQ(std::optional<uint64_t> { 8 }, budget.suppressed(1, T0 + SummaryInterval));
}
