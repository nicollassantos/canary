/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

class Player;

class IPlayerRepository {
public:
	IPlayerRepository() = default;
	virtual ~IPlayerRepository() = default;

	IPlayerRepository(const IPlayerRepository &) = delete;
	void operator=(const IPlayerRepository &) = delete;

	static IPlayerRepository &getInstance();

	[[nodiscard]] virtual bool loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id) = 0;
	[[nodiscard]] virtual bool loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) = 0;
	[[nodiscard]] virtual bool savePlayer(const std::shared_ptr<Player> &player) = 0;
};

constexpr auto g_playerRepository = IPlayerRepository::getInstance;
