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
	return DI::port<IPlayerRepository>();
}

bool PlayerRepositoryDB::loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id, bool disableIrrelevantInfo) {
	return IOLoginData::loadPlayerById(player, id, disableIrrelevantInfo);
}

bool PlayerRepositoryDB::loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) {
	return IOLoginData::loadPlayerByName(player, name);
}

bool PlayerRepositoryDB::savePlayer(const std::shared_ptr<Player> &player) {
	return IOLoginData::savePlayer(player);
}

uint32_t PlayerRepositoryDB::getGuidByName(const std::string &name) {
	return IOLoginData::getGuidByName(name);
}

std::string PlayerRepositoryDB::getNameByGuid(uint32_t guid) {
	return IOLoginData::getNameByGuid(guid);
}

void PlayerRepositoryDB::increaseBankBalance(uint32_t guid, uint64_t amount) {
	IOLoginData::increaseBankBalance(guid, amount);
}

bool PlayerRepositoryDB::formatPlayerName(std::string &name) {
	return IOLoginData::formatPlayerName(name);
}

bool PlayerRepositoryDB::getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) {
	return IOLoginData::getGuidByNameEx(guid, specialVip, name);
}

bool PlayerRepositoryDB::gameWorldAuthentication(const std::string &accountDescriptor, const std::string &sessionOrPassword, std::string &characterName, uint32_t &accountId, bool oldProtocol, uint32_t ip) {
	return IOLoginData::gameWorldAuthentication(accountDescriptor, sessionOrPassword, characterName, accountId, oldProtocol, ip);
}
