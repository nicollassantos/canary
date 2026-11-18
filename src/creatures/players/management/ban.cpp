/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/management/ban.hpp"

#include "utils/tools.hpp"

namespace {
	// An entry can no longer influence acceptConnection() once its block has
	// expired and its last attempt fell out of the 5s burst window, so keeping
	// it around only costs memory and lookup time.
	constexpr uint64_t CONNECT_BLOCK_BURST_WINDOW = 5000;
} // namespace

void Ban::pruneStaleEntries(uint64_t currentTime) {
	// Incremental: examine at most PRUNE_BUDGET entries, resuming where the
	// previous call stopped and wrapping at the end, so the time spent under the
	// lock is bounded no matter how large the map has grown.
	auto it = ipConnectMap.lower_bound(pruneCursor);
	// At most one lap: a small map is not re-examined within the same call.
	const size_t steps = std::min(PRUNE_BUDGET, ipConnectMap.size());
	for (size_t examined = 0; examined < steps; ++examined) {
		if (it == ipConnectMap.end()) {
			it = ipConnectMap.begin();
		}
		const ConnectBlock &connectBlock = it->second;
		// OTSYS_TIME() follows the system clock, which can step backward. An
		// attempt recorded "in the future" (before a backward step) is recent:
		// acceptConnection() still counts it inside the burst window, so it stays.
		const bool stale = connectBlock.blockTime <= currentTime
			&& connectBlock.lastAttempt <= currentTime
			&& currentTime - connectBlock.lastAttempt > CONNECT_BLOCK_BURST_WINDOW;
		it = stale ? ipConnectMap.erase(it) : std::next(it);
	}
	pruneCursor = it == ipConnectMap.end() ? 0 : it->first;
}

bool Ban::acceptConnection(uint32_t clientIP) {
	std::scoped_lock<std::recursive_mutex> lockClass(lock);

	const uint64_t currentTime = OTSYS_TIME();
	pruneStaleEntries(currentTime);

	auto it = ipConnectMap.find(clientIP);
	if (it == ipConnectMap.end()) {
		ipConnectMap.emplace(clientIP, ConnectBlock(currentTime, 0, 1));
		return true;
	}

	ConnectBlock &connectBlock = it->second;
	if (connectBlock.blockTime > currentTime) {
		connectBlock.blockTime += 250;
		return false;
	}

	const int64_t timeDiff = currentTime - connectBlock.lastAttempt;
	connectBlock.lastAttempt = currentTime;
	if (timeDiff <= 5000) {
		if (++connectBlock.count > 5) {
			connectBlock.count = 0;
			if (timeDiff <= 500) {
				connectBlock.blockTime = currentTime + 3000;
				return false;
			}
		}
	} else {
		connectBlock.count = 1;
	}
	return true;
}
