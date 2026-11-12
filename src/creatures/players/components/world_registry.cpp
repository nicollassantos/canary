/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/world_registry.hpp"

#include "game/game.hpp"
#include "lib/di/container.hpp"

WorldRegistry &WorldRegistry::getInstance() {
	return DI::port<WorldRegistry>();
}

std::shared_ptr<Player> WorldRegistry::getPlayerByID(uint32_t id, bool allowOffline) {
	return g_game().getPlayerByID(id, allowOffline);
}

std::shared_ptr<Creature> WorldRegistry::getCreatureByID(uint32_t id) {
	return g_game().getCreatureByID(id);
}

const WorldRegistry::PlayerMap &WorldRegistry::getPlayers() const {
	return g_game().getPlayers();
}

void WorldRegistry::addPlayer(const std::shared_ptr<Player> &player) {
	g_game().addPlayer(player);
}

void WorldRegistry::removePlayer(const std::shared_ptr<Player> &player) {
	g_game().removePlayer(player);
}

bool WorldRegistry::removeCreature(const std::shared_ptr<Creature> &creature, bool isLogout) {
	return g_game().removeCreature(creature, isLogout);
}

void WorldRegistry::checkPlayersRecord() {
	g_game().checkPlayersRecord();
}

WorldType_t WorldRegistry::getWorldType() const {
	return g_game().getWorldType();
}
