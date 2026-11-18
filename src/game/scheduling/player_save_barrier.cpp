/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/scheduling/player_save_barrier.hpp"

#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
	#include <cctype>
	#include <optional>
	#include <utility>
#endif

void PlayerSaveBarrier::begin(uint32_t guid, const std::string &name) {
	std::scoped_lock lock(mutex);
	saving.insert_or_assign(guid, Pending { asLowerCaseString(name), std::chrono::steady_clock::now() });
}

void PlayerSaveBarrier::end(uint32_t guid) {
	{
		std::scoped_lock lock(mutex);
		saving.erase(guid);
	}
	saved.notify_all();
}

template <typename Blocks>
bool PlayerSaveBarrier::waitWhile(const Blocks &blocks, std::chrono::milliseconds limit) {
	std::unique_lock lock(mutex);
	bool waited = false;
	while (true) {
		const auto now = std::chrono::steady_clock::now();
		std::optional<std::chrono::steady_clock::time_point> earliest;
		for (auto &[guid, pending] : saving) {
			if (!blocks(guid, pending)) {
				continue;
			}
			const auto deadline = pending.started + limit;
			if (now >= deadline) {
				// Stuck: answer at once, so this save cannot hold the game thread past its deadline.
				const bool firstReport = !std::exchange(pending.reportedStuck, true);
				const auto name = pending.lowerName;
				lock.unlock();
				if (firstReport) {
					g_logger().error("[PlayerSaveBarrier] the logout save of {} (guid {}) is still pending after {} ms (is the database reachable?); loads of it are refused until it commits", name, guid, limit.count());
				}
				g_metrics().addCounter("player_save_barrier_timeouts", 1, {});
				return false;
			}
			earliest = earliest ? std::min(*earliest, deadline) : deadline;
		}
		if (!earliest) {
			lock.unlock();
			if (waited) {
				g_metrics().addCounter("player_loads_waited_for_save", 1, {});
			}
			return true;
		}
		waited = true;
		saved.wait_until(lock, *earliest);
	}
}

bool PlayerSaveBarrier::waitForGuid(uint32_t guid, std::chrono::milliseconds limit) {
	return waitWhile([guid](uint32_t pendingGuid, const Pending &) { return pendingGuid == guid; }, limit);
}

bool PlayerSaveBarrier::waitForName(const std::string &name, std::chrono::milliseconds limit) {
	const auto lower = asLowerCaseString(name);
	return waitWhile([&lower](uint32_t, const Pending &pending) { return pending.lowerName == lower; }, limit);
}

bool PlayerSaveBarrier::waitForAll(std::chrono::milliseconds limit) {
	return waitWhile([](uint32_t, const Pending &) { return true; }, limit);
}

bool PlayerSaveBarrier::writesSavedTables(std::string_view query) {
	const auto lower = asLowerCaseString(std::string(query));
	const auto start = lower.find_first_not_of(" \t\r\n(");
	if (start == std::string::npos || lower.compare(start, 6, "select") == 0) {
		return false;
	}
	const auto isNameChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
	for (const auto table : SavedTables) {
		for (auto at = lower.find(table); at != std::string::npos; at = lower.find(table, at + 1)) {
			const auto end = at + table.size();
			if ((at == 0 || !isNameChar(lower[at - 1])) && (end == lower.size() || !isNameChar(lower[end]))) {
				return true;
			}
		}
	}
	return false;
}

size_t PlayerSaveBarrier::pending() const {
	std::scoped_lock lock(mutex);
	return saving.size();
}

PlayerSaveBarrier &PlayerSaveBarrier::global() {
	static PlayerSaveBarrier barrier;
	return barrier;
}
