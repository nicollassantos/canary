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

/// What the Bestiary window shows of a creature for the kill level the player reached (1 to 4). The
/// client may ask for any creature (ProtocolGame::parseBestiarysendMonsterData); this is all it gets.
namespace bestiary_disclosure {
	/// A loot entry's name and id: common drops (difficulty below 2) from level 2, below 3 from level 3,
	/// everything at level 4; all of them when the server shows loot in the Bestiary.
	constexpr bool showsLootItem(uint8_t level, int8_t difficulty, bool showAllLoot) {
		if (showAllLoot) {
			return true;
		}
		switch (level) {
			case 2:
				return difficulty < 2;
			case 3:
				return difficulty < 3;
			case 4:
				return true;
			default:
				return false;
		}
	}

	/// Charm points, attack mode, health, experience, speed, armour, mitigation: from level 2.
	constexpr bool showsStats(uint8_t level) {
		return level > 1;
	}

	/// Element resistances and where the creature lives: from level 3.
	constexpr bool showsElementsAndLocations(uint8_t level) {
		return level > 2;
	}
}
