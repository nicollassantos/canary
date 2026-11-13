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

/// Decides which "player is typing" updates (packet 0x38) reach the spectators.
///
/// Every update was sent to every player watching, on all floors, with no interval (the same
/// class as the outfit change, 0xD3). Now only changes are sent, "started typing" at most once per
/// StartIntervalMs; "stopped" always passes after a "started", so the indicator cannot get stuck.
/// That is at most two broadcasts per second per player. Dispatcher thread only.
class TypingThrottle {
public:
	static constexpr int64_t StartIntervalMs = 1000;
	static constexpr size_t PruneThreshold = 4096;
	static constexpr int64_t StaleAfterMs = 60'000;

	/// True when the change to `typing` must be broadcast; the state is then recorded.
	bool shouldBroadcast(uint32_t playerId, bool typing, int64_t nowMs) {
		if (states.size() >= PruneThreshold) {
			prune(nowMs);
		}

		auto &state = states[playerId];
		if (typing == state.typing) {
			return false;
		}
		if (typing) {
			if (state.started && nowMs - state.lastStartAt < StartIntervalMs) {
				return false;
			}
			state.started = true;
			state.lastStartAt = nowMs;
		}
		state.typing = typing;
		return true;
	}

	[[nodiscard]] size_t size() const {
		return states.size();
	}

private:
	struct State {
		bool typing = false;
		bool started = false;
		int64_t lastStartAt = 0;
	};

	void prune(int64_t nowMs) {
		for (auto it = states.begin(); it != states.end();) {
			if (!it->second.typing || nowMs - it->second.lastStartAt >= StaleAfterMs) {
				it = states.erase(it);
			} else {
				++it;
			}
		}
	}

	phmap::flat_hash_map<uint32_t, State> states;
};
