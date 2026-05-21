/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/player_repository_db.hpp"

#include "io/iologindata.hpp"
#include "lib/di/container.hpp"

IPlayerRepository &IPlayerRepository::getInstance() {
	return inject<IPlayerRepository>();
}

bool PlayerRepositoryDB::loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id) {
	return IOLoginData::loadPlayerById(player, id);
}

bool PlayerRepositoryDB::loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) {
	return IOLoginData::loadPlayerByName(player, name);
}

bool PlayerRepositoryDB::savePlayer(const std::shared_ptr<Player> &player) {
	return IOLoginData::savePlayer(player);
}
