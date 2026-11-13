/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "lib/logging/log_budget.hpp"

#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
#endif

namespace log_budget {

	namespace {
		thread_local PacketScope::Context* current = nullptr;
	} // namespace

	void Budget::prune(Clock::time_point now) {
		// A player gone quiet for a whole window is dropped even with lines still waiting for a
		// report (the counter already has them): keeping those entries let the map grow for good,
		// and a full map was scanned again on every line.
		for (auto it = players.begin(); it != players.end();) {
			const auto &state = it->second;
			const bool windowOver = now - state.windowStart >= PlayerWindow;
			const bool reportOver = !state.lastReportAt || now - *state.lastReportAt >= PlayerWindow;
			if (windowOver && reportOver) {
				it = players.erase(it);
			} else {
				++it;
			}
		}
		// Scan again only after the map doubles, so a map of active players is not rescanned per line.
		pruneAt = std::max(PruneThreshold, players.size() * 2);
	}

	bool Budget::admit(uint32_t playerId, uint32_t linesInPacket, Clock::time_point now) {
		if (linesInPacket >= LinesPerPacket) {
			return false;
		}

		std::scoped_lock lock(mutex);
		if (players.size() >= pruneAt) {
			prune(now);
		}

		auto &state = players[playerId];
		if (state.lines == 0 || now - state.windowStart >= PlayerWindow) {
			state.windowStart = now;
			state.lines = 0;
		}
		if (state.lines >= LinesPerPlayerPerWindow) {
			return false;
		}
		++state.lines;
		return true;
	}

	std::optional<SuppressedReport> Budget::suppressed(uint32_t playerId, uint8_t opcode, uint32_t count, Clock::time_point now) {
		if (count == 0) {
			return std::nullopt;
		}

		std::scoped_lock lock(mutex);
		if (players.size() >= pruneAt) {
			prune(now);
		}
		auto &state = players[playerId];
		state.pendingLines += count;
		state.pendingOpcodes.set(opcode);
		if (state.lastReportAt && now - *state.lastReportAt < PlayerWindow) {
			return std::nullopt;
		}

		state.lastReportAt = now;
		SuppressedReport report { .lines = std::exchange(state.pendingLines, 0) };
		for (size_t code = 0; code < state.pendingOpcodes.size(); ++code) {
			if (state.pendingOpcodes.test(code)) {
				report.opcodes.push_back(static_cast<uint8_t>(code));
			}
		}
		state.pendingOpcodes.reset();
		return report;
	}

	size_t Budget::size() {
		std::scoped_lock lock(mutex);
		return players.size();
	}

	Budget &Budget::global() {
		static Budget budget;
		return budget;
	}

	PacketScope::PacketScope(uint32_t playerId, uint8_t opcode, std::string_view playerName, uint32_t ip, bool limited) :
		context { .playerId = playerId, .opcode = opcode },
		playerName(playerName),
		ip(ip),
		previous(current),
		open(limited) {
		if (open) {
			current = &context;
		}
	}

	PacketScope::~PacketScope() {
		close();
	}

	void PacketScope::close() {
		if (!open) {
			return;
		}
		open = false;
		current = previous;

		if (context.suppressed == 0) {
			return;
		}

		g_metrics().addCounter("packet_log_lines_suppressed", context.suppressed, { { "opcode", fmt::format("0x{:02X}", context.opcode) } });
		const auto report = Budget::global().suppressed(context.playerId, context.opcode, context.suppressed, Clock::now());
		if (!report) {
			return;
		}

		std::string opcodes;
		for (const auto code : report->opcodes) {
			opcodes += fmt::format("{}0x{:02X}", opcodes.empty() ? "" : " ", code);
		}
		// Written after the scope is closed, so the budget does not apply to the report.
		g_logger().warn(
			"[PacketWatch] cut {} log lines from player '{}' (IP {}), packets {} (limit {} per packet, {} per player per {} s)",
			report->lines, playerName, convertIPToString(ip), opcodes, LinesPerPacket, LinesPerPlayerPerWindow, PlayerWindow.count()
		);
	}

	bool admit() {
		auto* const context = current;
		if (!context) {
			return true;
		}

		if (Budget::global().admit(context->playerId, context->lines, Clock::now())) {
			++context->lines;
			return true;
		}
		++context->suppressed;
		return false;
	}

} // namespace log_budget
