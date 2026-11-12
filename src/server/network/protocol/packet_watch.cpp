/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "server/network/protocol/packet_watch.hpp"

#include "creatures/players/player.hpp"
#include "lib/logging/logger.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <array>
#endif

namespace packet_watch {

	namespace {

		// Built once: one label per opcode, so measuring a packet allocates nothing.
		const std::array<std::string, 256> &opcodeLabels() {
			static const auto labels = [] {
				std::array<std::string, 256> built;
				for (size_t opcode = 0; opcode < built.size(); ++opcode) {
					built[opcode] = fmt::format("packet_0x{:02X}", opcode);
				}
				return built;
			}();
			return labels;
		}

	} // namespace

	bool SlowPacketReporter::report(uint8_t opcode, std::string_view playerName, uint32_t ip, std::chrono::steady_clock::duration elapsed, std::chrono::steady_clock::time_point now) {
		if (elapsed < SlowThreshold) {
			return false;
		}

		g_metrics().addCounter("slow_packets_total", 1, { { "opcode", fmt::format("0x{:02X}", opcode) } });

		if (nextWarningAt && now < *nextWarningAt) {
			++suppressed;
			return false;
		}
		nextWarningAt = now + WarningInterval;

		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
		g_logger().warn(
			"[PacketWatch] packet 0x{:02X} held the dispatcher for {} ms (player '{}', IP {}); {} more slow packets since the last warning",
			opcode, milliseconds, playerName, convertIPToString(ip), std::exchange(suppressed, 0)
		);
		return true;
	}

	SlowPacketReporter &SlowPacketReporter::global() {
		// Dispatcher thread only.
		static SlowPacketReporter reporter;
		return reporter;
	}

	Scope::Scope(uint8_t opcode, const std::shared_ptr<Player> &player, uint32_t ip) :
		opcode(opcode),
		playerName(player ? player->getName() : "<none>"),
		ip(ip),
		start(std::chrono::steady_clock::now()),
		latency(opcodeLabels()[opcode]) { }

	Scope::~Scope() {
		const auto now = std::chrono::steady_clock::now();
		SlowPacketReporter::global().report(opcode, playerName, ip, now - start, now);
	}

} // namespace packet_watch
