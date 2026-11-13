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

namespace log_budget {

	namespace {
		thread_local PacketScope::Context* current = nullptr;
	} // namespace

	bool Budget::admit(uint32_t playerId, uint32_t linesInPacket, Clock::time_point now) {
		if (linesInPacket >= LinesPerPacket) {
			return false;
		}

		std::scoped_lock lock(mutex);
		if (windows.size() >= PruneThreshold) {
			for (auto it = windows.begin(); it != windows.end();) {
				if (now - it->second.start >= PlayerWindow) {
					it = windows.erase(it);
				} else {
					++it;
				}
			}
		}

		auto &window = windows[playerId];
		if (window.lines == 0 || now - window.start >= PlayerWindow) {
			window.start = now;
			window.lines = 0;
		}
		if (window.lines >= LinesPerPlayerPerWindow) {
			return false;
		}
		++window.lines;
		return true;
	}

	std::optional<uint64_t> Budget::suppressed(uint64_t count, Clock::time_point now) {
		std::scoped_lock lock(mutex);
		pendingSuppressed += count;
		if (pendingSuppressed == 0 || (nextSummaryAt && now < *nextSummaryAt)) {
			return std::nullopt;
		}
		nextSummaryAt = now + SummaryInterval;
		return std::exchange(pendingSuppressed, 0);
	}

	Budget &Budget::global() {
		static Budget budget;
		return budget;
	}

	PacketScope::PacketScope(uint32_t playerId, uint8_t opcode, std::string_view playerName) :
		context { .playerId = playerId, .opcode = opcode },
		playerName(playerName),
		previous(current) {
		current = &context;
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
		if (const auto toReport = Budget::global().suppressed(context.suppressed, Clock::now())) {
			// Written after the scope is closed, so the budget does not apply to the summary.
			g_logger().warn(
				"[PacketWatch] suppressed {} log lines written while handling client packets (limit {} per packet, {} per player per {} s); last from packet 0x{:02X} of player '{}'",
				*toReport, LinesPerPacket, LinesPerPlayerPerWindow, PlayerWindow.count(), context.opcode, playerName
			);
		}
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
