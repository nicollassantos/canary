/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/vip_repository_db.hpp"

#include "creatures/creatures_definitions.hpp"
#include "io/iologindata.hpp"
#include "lib/di/container.hpp"

IVipRepository &IVipRepository::getInstance() {
	return DI::port<IVipRepository>();
}

std::vector<VIPEntry> VipRepositoryDB::getVIPEntries(uint32_t accountId) {
	return IOLoginData::getVIPEntries(accountId);
}

void VipRepositoryDB::addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	IOLoginData::addVIPEntry(accountId, guid, description, icon, notify);
}

void VipRepositoryDB::editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	IOLoginData::editVIPEntry(accountId, guid, description, icon, notify);
}

void VipRepositoryDB::removeVIPEntry(uint32_t accountId, uint32_t guid) {
	IOLoginData::removeVIPEntry(accountId, guid);
}

std::vector<VIPGroupEntry> VipRepositoryDB::getVIPGroupEntries(uint32_t accountId, uint32_t guid) {
	return IOLoginData::getVIPGroupEntries(accountId, guid);
}

void VipRepositoryDB::addVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) {
	IOLoginData::addVIPGroupEntry(groupId, accountId, groupName, customizable);
}

void VipRepositoryDB::editVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) {
	IOLoginData::editVIPGroupEntry(groupId, accountId, groupName, customizable);
}

void VipRepositoryDB::removeVIPGroupEntry(uint8_t groupId, uint32_t accountId) {
	IOLoginData::removeVIPGroupEntry(groupId, accountId);
}

void VipRepositoryDB::addGuidVIPGroupEntry(uint8_t groupId, uint32_t accountId, uint32_t guid) {
	IOLoginData::addGuidVIPGroupEntry(groupId, accountId, guid);
}

void VipRepositoryDB::removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) {
	IOLoginData::removeGuidVIPGroupEntry(accountId, guid);
}
