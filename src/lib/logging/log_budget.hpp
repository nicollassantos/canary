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
	#include <chrono>
	#include <cstdint>
	#include <mutex>
	#include <optional>
	#include <string_view>
	#include <parallel_hashmap/phmap.h>
#endif

/// Caps the log lines a client packet can produce.
///
/// Rejections of hostile input are logged, and some handlers log once per value in the packet
/// (weapon proficiency logged up to 255 errors per packet). The logger writes synchronously on the
/// dispatcher, so a client could fill the disk and slow the game with log lines. While a packet is
/// handled (packet_watch::Scope), info/warn/error/critical lines are limited per packet and per
/// player; the rest are counted in `packet_log_lines_suppressed{opcode}` and summarised in one
/// warning at most every SummaryInterval. Lines written outside a client packet are not limited.
namespace log_budget {

	using Clock = std::chrono::steady_clock;

	inline constexpr uint32_t LinesPerPacket = 3;
	inline constexpr uint32_t LinesPerPlayerPerWindow = 20;
	inline constexpr std::chrono::seconds PlayerWindow { 60 };
	inline constexpr std::chrono::seconds SummaryInterval { 5 };

	/// Shared budget state. One global instance (packets run on several dispatcher threads, so it is
	/// locked); tests build their own.
	class Budget {
	public:
		static constexpr size_t PruneThreshold = 4096;

		/// True when one more line fits: `linesInPacket` lines were already written for this packet,
		/// and the player's lines in the current window are below the window limit.
		bool admit(uint32_t playerId, uint32_t linesInPacket, Clock::time_point now);

		/// Records `count` suppressed lines. Returns how many to report when a summary is due now
		/// (then the pending count restarts), or nothing while the previous summary is recent.
		std::optional<uint64_t> suppressed(uint64_t count, Clock::time_point now);

		static Budget &global();

	private:
		struct Window {
			Clock::time_point start;
			uint32_t lines = 0;
		};

		std::mutex mutex;
		phmap::flat_hash_map<uint32_t, Window> windows;
		std::optional<Clock::time_point> nextSummaryAt;
		uint64_t pendingSuppressed = 0;
	};

	/// Marks the current thread as handling a client packet until close() or destruction.
	class PacketScope {
	public:
		PacketScope(uint32_t playerId, uint8_t opcode, std::string_view playerName);
		~PacketScope();

		PacketScope(const PacketScope &) = delete;
		PacketScope &operator=(const PacketScope &) = delete;

		/// Ends the scope early (idempotent) and reports what it suppressed.
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
		Context* previous = nullptr;
		bool open = true;
	};

	/// Called by the logger before writing a line. True outside a client packet, or while the
	/// packet and its player are within budget.
	bool admit();

} // namespace log_budget
