/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/world_type_config.hpp"

#include "utils/tools.hpp"

namespace world_type_config {
	std::optional<WorldType_t> parse(std::string_view configured) {
		const auto worldType = asLowerCaseString(std::string(configured));
		if (worldType == "open-pvp" || worldType == "expert-pvp" || worldType == "retro-pvp" || worldType == "pvp") {
			return WORLD_TYPE_PVP;
		}
		if (worldType == "no-pvp") {
			return WORLD_TYPE_NO_PVP;
		}
		if (worldType == "pvp-enforced") {
			return WORLD_TYPE_PVP_ENFORCED;
		}
		return std::nullopt;
	}

	std::string displayName(std::string_view configured) {
		const auto worldType = asLowerCaseString(std::string(configured));
		return asUpperCaseString(worldType == "pvp" ? std::string("retro-pvp") : worldType);
	}
}
