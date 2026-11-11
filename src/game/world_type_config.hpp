/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "game/game_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <optional>
	#include <string>
	#include <string_view>
#endif

/**
 * Pure mapping of the `worldType` config value to the game world type.
 *
 * PvP worlds share WORLD_TYPE_PVP; their rule set is selected by the name:
 * - "open-pvp": Open PvP without retro or expert rules (party protection,
 *   advanced secure mode and Twist of Fate enabled). Branch addition: upstream
 *   removed `toggleServerIsRetroPVP`, which used to select this mode for "pvp".
 * - "retro-pvp" (alias "pvp"): Retro Open PvP.
 * - "expert-pvp": Expert Open PvP (see ExpertPvp).
 */
namespace world_type_config {
	[[nodiscard]] std::optional<WorldType_t> parse(std::string_view configured);

	/// Upper-case name for logs, with the legacy "pvp" alias shown as RETRO-PVP.
	[[nodiscard]] std::string displayName(std::string_view configured);

	inline constexpr std::string_view validValues = "open-pvp, expert-pvp, retro-pvp, pvp, no-pvp and pvp-enforced";
}
