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

void PlayerSaveBarrier::begin(uint32_t guid, const std::string &name) {
	std::scoped_lock lock(mutex);
	saving.insert_or_assign(guid, asLowerCaseString(name));
}

void PlayerSaveBarrier::end(uint32_t guid) {
	{
		std::scoped_lock lock(mutex);
		saving.erase(guid);
	}
	saved.notify_all();
}

template <typename Pending>
std::chrono::milliseconds PlayerSaveBarrier::waitWhile(const Pending &isPending, const std::string &who) {
	const auto started = std::chrono::steady_clock::now();
	std::unique_lock lock(mutex);
	if (!isPending()) {
		return std::chrono::milliseconds(0);
	}
	// Consistency first, as the synchronous queries do when the database is away: keep waiting, and
	// say so every WarningInterval.
	while (!saved.wait_for(lock, WarningInterval, [&isPending] { return !isPending(); })) {
		g_logger().warn("[PlayerSaveBarrier] still waiting for the logout save of {} before loading it", who);
	}
	const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
	lock.unlock();
	g_metrics().addCounter("player_loads_waited_for_save", 1, {});
	return waited;
}

std::chrono::milliseconds PlayerSaveBarrier::waitForGuid(uint32_t guid) {
	return waitWhile([this, guid] { return saving.contains(guid); }, std::to_string(guid));
}

std::chrono::milliseconds PlayerSaveBarrier::waitForName(const std::string &name) {
	const auto lower = asLowerCaseString(name);
	return waitWhile([this, &lower] {
		for (const auto &[_, savingName] : saving) {
			if (savingName == lower) {
				return true;
			}
		}
		return false;
	}, name);
}

std::chrono::milliseconds PlayerSaveBarrier::waitForAll() {
	return waitWhile([this] { return !saving.empty(); }, "every character");
}

size_t PlayerSaveBarrier::pending() const {
	std::scoped_lock lock(mutex);
	return saving.size();
}

PlayerSaveBarrier &PlayerSaveBarrier::global() {
	static PlayerSaveBarrier barrier;
	return barrier;
}
