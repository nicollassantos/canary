/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <bitset>
	#include <chrono>
	#include <cstdint>
	#include <mutex>
	#include <optional>
	#include <string_view>
	#include <vector>
	#include <parallel_hashmap/phmap.h>
#endif

/// Caps the log lines a client packet can produce.
///
/// Rejections of hostile input are logged, and some handlers log once per value in the packet
/// (weapon proficiency logged up to 255 errors per packet). The logger writes synchronously on the
/// dispatcher, so a client could fill the disk and slow the game with log lines. While a player's
/// packet is handled (packet_watch::Scope), info/warn/error/critical lines are limited per packet
/// and per player. Staff are not limited, and lines outside a client packet are not limited.
///
/// What is cut is never silent: `packet_log_lines_suppressed{opcode}` counts every line, and each
/// player whose lines were cut is named (with IP, opcodes and count) in a warning at most once per
/// PlayerWindow, so filling the budget with noise does not hide who did it.
namespace log_budget {

	using Clock = std::chrono::steady_clock;

	inline constexpr uint32_t LinesPerPacket = 3;
	inline constexpr uint32_t LinesPerPlayerPerWindow = 20;
	inline constexpr std::chrono::seconds PlayerWindow { 60 };

	/// What to report for one player: lines cut since the last report, and from which opcodes.
	struct SuppressedReport {
		uint64_t lines = 0;
		std::vector<uint8_t> opcodes;
	};

	/// Shared budget state. One global instance (packets run on several dispatcher threads, so it is
	/// locked); tests build their own.
	class Budget {
	public:
		static constexpr size_t PruneThreshold = 4096;

		/// True when one more line fits: `linesInPacket` lines were already written for this packet,
		/// and the player's lines in the current window are below the window limit.
		bool admit(uint32_t playerId, uint32_t linesInPacket, Clock::time_point now);

		/// Records `count` lines of `opcode` cut for `playerId`. Returns the report to write now: the
		/// first time for the player, then at most once per PlayerWindow, carrying everything cut since
		/// the previous report.
		std::optional<SuppressedReport> suppressed(uint32_t playerId, uint8_t opcode, uint32_t count, Clock::time_point now);

		[[nodiscard]] size_t size();

		static Budget &global();

	private:
		struct PlayerState {
			Clock::time_point windowStart;
			uint32_t lines = 0;
			uint64_t pendingLines = 0;
			std::bitset<256> pendingOpcodes;
			std::optional<Clock::time_point> lastReportAt;
		};

		void prune(Clock::time_point now);

		std::mutex mutex;
		phmap::flat_hash_map<uint32_t, PlayerState> players;
		size_t pruneAt = PruneThreshold;
	};

	/// Marks the current thread as handling a player's packet until close() or destruction. An
	/// unlimited scope (staff) leaves logging untouched.
	class PacketScope {
	public:
		PacketScope(uint32_t playerId, uint8_t opcode, std::string_view playerName, uint32_t ip, bool limited);
		~PacketScope();

		PacketScope(const PacketScope &) = delete;
		PacketScope &operator=(const PacketScope &) = delete;

		/// Ends the scope early (idempotent) and reports what it cut.
		void close();

		struct Context {
			uint32_t playerId = 0;
			uint8_t opcode = 0;
			uint32_t lines = 0;
			uint32_t suppressed = 0;
		};

	private:
		Context context;
		std::string_view playerName;
		uint32_t ip = 0;
		Context* previous = nullptr;
		bool open = false;
	};

	/// Called by the logger before writing a line. True outside a limited packet scope, or while the
	/// packet and its player are within budget.
	bool admit();

} // namespace log_budget
