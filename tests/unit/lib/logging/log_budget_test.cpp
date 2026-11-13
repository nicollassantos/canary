/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "lib/logging/log_budget.hpp"
#include "lib/logging/log_with_spd_log.hpp"

using namespace log_budget;

namespace {
	// The global budget keeps per-player state across tests: give each test its own players.
	constexpr uint32_t PlayerOfPacketTest = 900'001;
	constexpr uint32_t PlayerOfCloseTest = 900'101;
	constexpr uint32_t PlayerOfStaffTest = 900'201;
	constexpr uint32_t PlayerOfLoggerTest = 900'301;
	constexpr uint32_t LocalIp = 0x0100007F;

	const Clock::time_point T0 = Clock::time_point {} + std::chrono::hours(1);
} // namespace

TEST(LogBudgetTest, LinesOutsideAClientPacketAreNotLimited) {
	for (int i = 0; i < 100; ++i) {
		EXPECT_TRUE(admit());
	}
}

// Regression: one weapon proficiency packet logged up to 255 errors (one per invalid perk).
TEST(LogBudgetTest, APacketWritesAtMostLinesPerPacket) {
	PacketScope scope(PlayerOfPacketTest, 0xB3, "Attacker", LocalIp, true);

	for (uint32_t i = 0; i < LinesPerPacket; ++i) {
		EXPECT_TRUE(admit()) << i;
	}
	for (int i = 0; i < 252; ++i) {
		EXPECT_FALSE(admit());
	}
}

TEST(LogBudgetTest, ClosingThePacketLiftsTheLimit) {
	PacketScope scope(PlayerOfCloseTest, 0xB3, "Attacker", LocalIp, true);
	for (uint32_t i = 0; i < LinesPerPacket; ++i) {
		EXPECT_TRUE(admit());
	}
	EXPECT_FALSE(admit());
	scope.close();
	EXPECT_TRUE(admit());
}

TEST(LogBudgetTest, StaffPacketsAreNotLimited) {
	PacketScope scope(PlayerOfStaffTest, 0x96, "Gamemaster", LocalIp, false);
	for (int i = 0; i < 100; ++i) {
		EXPECT_TRUE(admit());
	}
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

// Filling the budget with noise must not hide who did it: every player whose lines were cut is
// reported (first time at once, then once per window, carrying what was cut in between).
TEST(LogBudgetTest, EveryPlayerWithCutLinesIsReportedOncePerWindow) {
	Budget budget;
	const auto first = budget.suppressed(7, 0xB3, 5, T0);
	ASSERT_TRUE(first.has_value());
	EXPECT_EQ(5U, first->lines);
	EXPECT_EQ((std::vector<uint8_t> { 0xB3 }), first->opcodes);

	EXPECT_EQ(std::nullopt, budget.suppressed(7, 0x96, 3, T0 + std::chrono::seconds(1)));

	const auto otherPlayer = budget.suppressed(8, 0x96, 1, T0 + std::chrono::seconds(2));
	ASSERT_TRUE(otherPlayer.has_value());
	EXPECT_EQ(1U, otherPlayer->lines);

	const auto later = budget.suppressed(7, 0xB3, 1, T0 + PlayerWindow);
	ASSERT_TRUE(later.has_value());
	EXPECT_EQ(4U, later->lines);
	EXPECT_EQ((std::vector<uint8_t> { 0x96, 0xB3 }), later->opcodes);
}

// The production logger applies the budget (InMemoryLogger, used by other tests, does not).
TEST(LogBudgetTest, TheSpdLogLoggerWritesOnlyTheBudgetedLines) {
	std::ostringstream out;
	const auto previousLogger = spdlog::default_logger();
	const auto previousLevel = spdlog::get_level();
	spdlog::set_default_logger(std::make_shared<spdlog::logger>("log-budget-test", std::make_shared<spdlog::sinks::ostream_sink_mt>(out)));
	{
		const LogWithSpdLog logger;
		PacketScope scope(PlayerOfLoggerTest, 0xB3, "Attacker", LocalIp, true);
		for (int i = 0; i < 10; ++i) {
			logger.error("invalid perk");
		}
	}
	spdlog::set_default_logger(previousLogger);
	spdlog::set_level(previousLevel);

	const auto text = out.str();
	size_t written = 0;
	for (auto pos = text.find("invalid perk"); pos != std::string::npos; pos = text.find("invalid perk", pos + 1)) {
		++written;
	}
	EXPECT_EQ(LinesPerPacket, written);
}
