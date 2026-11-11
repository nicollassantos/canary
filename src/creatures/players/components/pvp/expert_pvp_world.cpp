/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/pvp/expert_pvp_world.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "lib/di/container.hpp"
#include "utils/tools.hpp"

ExpertPvpWorld &ExpertPvpWorld::getInstance() {
	return inject<ExpertPvpWorld>();
}

std::shared_ptr<Player> ExpertPvpWorld::getPlayerByGUID(uint32_t guid) const {
	return g_game().getPlayerByGUID(guid);
}

WorldType_t ExpertPvpWorld::getWorldType() const {
	return g_game().getWorldType();
}

std::vector<std::shared_ptr<Player>> ExpertPvpWorld::getOnlinePlayers() const {
	std::vector<std::shared_ptr<Player>> players;
	const auto &onlinePlayers = g_game().getPlayers();
	players.reserve(onlinePlayers.size());
	for (const auto &[_, player] : onlinePlayers) {
		players.emplace_back(player);
	}
	return players;
}

std::string ExpertPvpWorld::getConfiguredWorldType() const {
	return asLowerCaseString(g_configManager().getString(WORLD_TYPE));
}
