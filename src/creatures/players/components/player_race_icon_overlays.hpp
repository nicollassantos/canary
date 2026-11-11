/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/creatures_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <map>
	#include <set>
	#include <vector>
#endif

/**
 * Per-player creature icon overlays keyed by monster race.
 *
 * Pure domain state: it does not touch the client. Callers decide when to
 * refresh visible creatures after a change.
 */
class PlayerRaceIconOverlays {
public:
	/**
	 * @brief Enables or disables an overlay icon for a race.
	 * @return True when the stored overlays changed.
	 */
	bool set(uint16_t raceId, CreatureIconModifications_t icon, bool enabled = true);

	/**
	 * @brief Removes an overlay icon from every race.
	 * @return True when at least one race lost the icon.
	 */
	bool clear(CreatureIconModifications_t icon);

	[[nodiscard]] std::vector<CreatureIcon> get(uint16_t raceId) const;

private:
	std::map<uint16_t, std::set<CreatureIconModifications_t>> m_overlays;
};
