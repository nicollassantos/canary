/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_race_icon_overlays.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <magic_enum/magic_enum.hpp>
#endif

namespace {
	[[nodiscard]] bool isValidOverlayIcon(CreatureIconModifications_t icon) {
		return icon != CreatureIconModifications_t::None && magic_enum::enum_contains(icon);
	}
}

bool PlayerRaceIconOverlays::set(uint16_t raceId, CreatureIconModifications_t icon, bool enabled) {
	if (raceId == 0 || !isValidOverlayIcon(icon)) {
		return false;
	}

	if (enabled) {
		return m_overlays[raceId].insert(icon).second;
	}

	const auto raceIt = m_overlays.find(raceId);
	if (raceIt == m_overlays.end() || raceIt->second.erase(icon) == 0) {
		return false;
	}
	if (raceIt->second.empty()) {
		static_cast<void>(m_overlays.erase(raceIt));
	}
	return true;
}

bool PlayerRaceIconOverlays::clear(CreatureIconModifications_t icon) {
	if (!isValidOverlayIcon(icon)) {
		return false;
	}

	bool changed = false;
	for (auto &[_, overlays] : m_overlays) {
		changed = overlays.erase(icon) > 0 || changed;
	}
	static_cast<void>(std::erase_if(m_overlays, [](const auto &entry) {
		return entry.second.empty();
	}));
	return changed;
}

std::vector<CreatureIcon> PlayerRaceIconOverlays::get(uint16_t raceId) const {
	std::vector<CreatureIcon> icons;
	const auto raceIt = m_overlays.find(raceId);
	if (raceIt == m_overlays.end()) {
		return icons;
	}

	icons.reserve(raceIt->second.size());
	for (const auto icon : raceIt->second) {
		icons.emplace_back(icon);
	}
	return icons;
}
