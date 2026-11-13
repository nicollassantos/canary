/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/guild_repository_db.hpp"

#include "creatures/players/grouping/guild.hpp"
#include "database/database_port.hpp"
#include "database/database.hpp"
#include "lib/di/container.hpp"

IGuildRepository &IGuildRepository::getInstance() {
	return DI::port<IGuildRepository>();
}

std::shared_ptr<Guild> GuildRepositoryDB::loadGuild(uint32_t guildId) {
	DBResult_ptr result = db_.storeQuery(fmt::format("SELECT `name`, `balance` FROM `guilds` WHERE `id` = {}", guildId));
	if (!result) {
		return nullptr;
	}

	const auto guild = std::make_shared<Guild>(guildId, result->getString("name"));
	guild->setBankBalance(result->getNumber<uint64_t>("balance"));

	if ((result = db_.storeQuery(fmt::format("SELECT `id`, `name`, `level` FROM `guild_ranks` WHERE `guild_id` = {}", guildId)))) {
		do {
			guild->addRank(result->getNumber<uint32_t>("id"), result->getString("name"), result->getNumber<uint16_t>("level"));
		} while (result->next());
	}
	return guild;
}

void GuildRepositoryDB::saveGuild(const std::shared_ptr<Guild> &guild) {
	if (!guild) {
		return;
	}
	(void)db_.executeQuery(fmt::format("UPDATE `guilds` SET `balance` = {} WHERE `id` = {}", guild->getBankBalance(), guild->getId()));
}

uint32_t GuildRepositoryDB::getGuildIdByName(const std::string &name) {
	const DBResult_ptr result = db_.storeQuery(fmt::format("SELECT `id` FROM `guilds` WHERE `name` = {}", db_.escapeString(name)));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool GuildRepositoryDB::formatGuildName(std::string &name) {
	const DBResult_ptr result = db_.storeQuery(fmt::format("SELECT `name` FROM `guilds` WHERE `name` = {} LIMIT 1", db_.escapeString(name)));
	if (!result) {
		return false;
	}
	name = result->getString("name");
	return true;
}

void GuildRepositoryDB::getWarList(uint32_t guildId, GuildWarVector &guildWarVector) {
	const DBResult_ptr result = db_.storeQuery(fmt::format("SELECT `guild1`, `guild2` FROM `guild_wars` WHERE (`guild1` = {} OR `guild2` = {}) AND `status` = 1", guildId, guildId));
	if (!result) {
		return;
	}

	do {
		const auto guild1 = result->getNumber<uint32_t>("guild1");
		guildWarVector.push_back(guildId != guild1 ? guild1 : result->getNumber<uint32_t>("guild2"));
	} while (result->next());
}
