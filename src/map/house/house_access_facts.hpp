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
	#include <cstdint>
#endif

/// What House::getHouseAccessLevel needs to know about a player who may be offline.
///
/// Using a bed someone sleeps in compares the sleeper's access level with the user's. That used
/// to load the whole sleeper (items, depot, inbox...) on the game thread for every use, which a
/// client can repeat several times per second; these facts come from one indexed query.
struct HouseAccessFacts {
	uint32_t guid = 0;
	uint32_t accountId = 0;
	/// The player's group has PlayerFlags_t::CanEditHouses.
	bool canEditHouses = false;
	/// 0 when the player is in no guild.
	uint32_t guildRankId = 0;
};
