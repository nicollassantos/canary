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
	#include <cstdint>
	#include <memory>
	#include <string>
	#include <vector>
#endif

class Player;

/**
 * World state that ExpertPvp reads (driven port).
 *
 * Concrete on purpose: the default implementation adapts Game and the
 * configuration, so DI containers that do not bind it (most unit tests) still
 * resolve a working instance. Tests override it with
 * `di::bind<ExpertPvpWorld>.to<FakeWorld>()`.
 */
class ExpertPvpWorld {
public:
	virtual ~ExpertPvpWorld() = default;

	static ExpertPvpWorld &getInstance();

	[[nodiscard]] virtual std::shared_ptr<Player> getPlayerByGUID(uint32_t guid) const;
	[[nodiscard]] virtual WorldType_t getWorldType() const;
	[[nodiscard]] virtual std::vector<std::shared_ptr<Player>> getOnlinePlayers() const;
	/// Lower-case `worldType` config value.
	[[nodiscard]] virtual std::string getConfiguredWorldType() const;
};
