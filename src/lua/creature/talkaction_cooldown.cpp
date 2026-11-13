/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "lua/creature/talkaction_cooldown.hpp"

#include "creatures/players/player.hpp"
#include "lib/metrics/metrics.hpp"

bool TalkActionCooldown::admit(const Player &player, int64_t nowMs) {
	if (player.isAccessPlayer()) {
		return true;
	}

	if (cooldown.tryTake(player.getID(), nowMs)) {
		return true;
	}

	player.sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
	g_metrics().addCounter("talkaction_requests_throttled", 1, {});
	return false;
}

TalkActionCooldown &TalkActionCooldown::global() {
	static TalkActionCooldown instance;
	return instance;
}
