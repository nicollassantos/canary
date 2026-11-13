/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "utils/request_cooldown.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
#endif

class Player;

/// Minimum interval between two talkactions said by the same player.
///
/// Talkactions run before the mute check and had no interval, so a client could run one per
/// packet; several reach the database or load characters (!transfer resolved any typed name,
/// !livestream writes a row). Staff (access players) are not limited. Dispatcher thread only.
class TalkActionCooldown {
public:
	static constexpr int64_t PlayerIntervalMs = 1000;

	/// True when `player` may run a talkaction at `nowMs`. A refused player is told they are
	/// exhausted, and the refusal is counted in `talkaction_requests_throttled`.
	bool admit(const Player &player, int64_t nowMs);

	static TalkActionCooldown &global();

private:
	RequestCooldown cooldown { PlayerIntervalMs };
};
