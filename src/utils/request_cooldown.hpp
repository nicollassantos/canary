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
	#include <cstddef>
	#include <cstdint>
	#include <parallel_hashmap/phmap.h>
#endif

/// Minimum interval between two requests with the same key (usually a player id).
///
/// Keys come from the network, so stale entries are pruned once the map reaches
/// PruneThreshold and it cannot grow without bound. Dispatcher thread only.
class RequestCooldown {
public:
	static constexpr size_t PruneThreshold = 4096;

	explicit RequestCooldown(int64_t intervalMs) :
		intervalMs(intervalMs) { }

	/// True when `key` may proceed at `nowMs`; the request is then recorded.
	bool tryTake(uint32_t key, int64_t nowMs) {
		if (lastAt.size() >= PruneThreshold) {
			for (auto it = lastAt.begin(); it != lastAt.end();) {
				if (nowMs - it->second >= intervalMs) {
					it = lastAt.erase(it);
				} else {
					++it;
				}
			}
		}

		const auto [it, inserted] = lastAt.try_emplace(key, nowMs);
		if (inserted) {
			return true;
		}
		if (nowMs - it->second < intervalMs) {
			return false;
		}
		it->second = nowMs;
		return true;
	}

	[[nodiscard]] size_t size() const {
		return lastAt.size();
	}

private:
	int64_t intervalMs;
	phmap::flat_hash_map<uint32_t, int64_t> lastAt;
};
