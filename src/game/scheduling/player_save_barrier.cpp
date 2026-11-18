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
bool PlayerSaveBarrier::waitWhile(const Pending &isPending, const std::string &who, std::chrono::milliseconds limit) {
	std::unique_lock lock(mutex);
	if (!isPending()) {
		return true;
	}
	const bool done = saved.wait_for(lock, limit, [&isPending] { return !isPending(); });
	lock.unlock();
	if (done) {
		g_metrics().addCounter("player_loads_waited_for_save", 1, {});
		return true;
	}
	g_logger().error("[PlayerSaveBarrier] the logout save of {} is still pending after {} ms (is the database reachable?)", who, limit.count());
	g_metrics().addCounter("player_save_barrier_timeouts", 1, {});
	return false;
}

bool PlayerSaveBarrier::waitForGuid(uint32_t guid, std::chrono::milliseconds limit) {
	return waitWhile([this, guid] { return saving.contains(guid); }, std::to_string(guid), limit);
}

bool PlayerSaveBarrier::waitForName(const std::string &name, std::chrono::milliseconds limit) {
	const auto lower = asLowerCaseString(name);
	return waitWhile([this, &lower] {
		for (const auto &[_, savingName] : saving) {
			if (savingName == lower) {
				return true;
			}
		}
		return false;
	}, name, limit);
}

bool PlayerSaveBarrier::waitForAll(std::chrono::milliseconds limit) {
	return waitWhile([this] { return !saving.empty(); }, "every character", limit);
}

size_t PlayerSaveBarrier::pending() const {
	std::scoped_lock lock(mutex);
	return saving.size();
}

PlayerSaveBarrier &PlayerSaveBarrier::global() {
	static PlayerSaveBarrier barrier;
	return barrier;
}
