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
	#include <functional>
	#include <map>
	#include <mutex>
	#include <optional>
#endif

/// Leader (player id) of each highscore category, for the highscore titles checked at login.
///
/// PlayerTitle::checkHighscore ran one ranking query per highscore title on every login (an
/// unindexed ORDER BY over all players, ~10 per login in the Phase 25 load test). The leader is now
/// fetched at most once per MaxAge per category; a new leader gets the title on a login up to
/// MaxAge later. Thread-safe.
class TitleLeaderCache {
public:
	using Clock = std::chrono::steady_clock;
	static constexpr std::chrono::seconds MaxAge { 60 };

	/// The cached leader of `category`, or `fetch()` (stored) when missing or older than MaxAge.
	/// `fetch` returns nothing when the category has no leader; that answer is cached too.
	std::optional<uint32_t> leader(uint8_t category, Clock::time_point now, const std::function<std::optional<uint32_t>()> &fetch) {
		{
			std::scoped_lock lock(mutex);
			const auto it = leaders.find(category);
			if (it != leaders.end() && now - it->second.fetchedAt < MaxAge) {
				return it->second.leader;
			}
		}
		const auto fetched = fetch();
		std::scoped_lock lock(mutex);
		leaders.insert_or_assign(category, Entry { fetched, now });
		return fetched;
	}

	static TitleLeaderCache &global() {
		static TitleLeaderCache cache;
		return cache;
	}

private:
	struct Entry {
		std::optional<uint32_t> leader;
		Clock::time_point fetchedAt;
	};

	std::mutex mutex;
	std::map<uint8_t, Entry> leaders;
};
