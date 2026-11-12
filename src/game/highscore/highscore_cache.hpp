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
	#include <cstddef>
#endif

namespace highscore_cache {

	/// Highscore cache keys come from client input (page, entries per page, vocation), so the caches
	/// must not grow without bound. Call before inserting.
	inline constexpr size_t MaxEntries = 4096;

	/// Drops entries whose `timestamp` is older than `ttl`. If the cache still holds `maxEntries` or
	/// more, clears it. Returns how many entries were dropped.
	template <typename Cache, typename TimePoint, typename Duration>
	size_t prune(Cache &cache, TimePoint now, Duration ttl, size_t maxEntries = MaxEntries) {
		const size_t before = cache.size();
		for (auto it = cache.begin(); it != cache.end();) {
			if (now - it->second.timestamp >= ttl) {
				it = cache.erase(it);
			} else {
				++it;
			}
		}
		if (cache.size() >= maxEntries) {
			cache.clear();
		}
		return before - cache.size();
	}

} // namespace highscore_cache
