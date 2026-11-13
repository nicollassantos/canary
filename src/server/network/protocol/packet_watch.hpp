/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "lib/logging/log_budget.hpp"
#include "lib/metrics/metrics.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <chrono>
	#include <cstdint>
	#include <memory>
	#include <optional>
	#include <string>
	#include <string_view>
#endif

class Player;

/// Measures every client packet handled on the dispatcher.
///
/// Metrics: latency per opcode in `method_latency{method="packet_0xNN"}` (its count is the
/// packet count) and `slow_packets_total{opcode}`. Logs: a packet that blocks the dispatcher for
/// SlowThreshold or more is logged with opcode, player, IP and duration, at most once per
/// WarningInterval (later ones are counted and reported with the next warning), so a flood
/// cannot flood the log too. While the packet is handled, its log lines are limited by
/// log_budget (a few per packet and per player).
namespace packet_watch {

	inline constexpr std::chrono::milliseconds SlowThreshold { 50 };
	inline constexpr std::chrono::seconds WarningInterval { 5 };

	/// Rate-limited slow-packet reporting. One global instance; tests build their own.
	class SlowPacketReporter {
	public:
		/// `elapsed` is how long the packet held the dispatcher. Returns true when it logged.
		bool report(uint8_t opcode, std::string_view playerName, uint32_t ip, std::chrono::steady_clock::duration elapsed, std::chrono::steady_clock::time_point now);

		static SlowPacketReporter &global();

	private:
		std::optional<std::chrono::steady_clock::time_point> nextWarningAt;
		uint64_t suppressed = 0;
	};

	/// Put at the top of ProtocolGame::parsePacket, after reading the opcode.
	class Scope {
	public:
		Scope(uint8_t opcode, const std::shared_ptr<Player> &player, uint32_t ip);
		~Scope();

		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		uint8_t opcode;
		std::string playerName;
		uint32_t ip;
		std::chrono::steady_clock::time_point start;
		metrics::method_latency latency;
		log_budget::PacketScope logBudget;
	};

} // namespace packet_watch
