/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/guild_repository_db.hpp"

#include "io/ioguild.hpp"
#include "lib/di/container.hpp"

IGuildRepository &IGuildRepository::getInstance() {
	return inject<IGuildRepository>();
}

std::shared_ptr<Guild> GuildRepositoryDB::loadGuild(uint32_t guildId) {
	return IOGuild::loadGuild(guildId);
}

void GuildRepositoryDB::saveGuild(const std::shared_ptr<Guild> &guild) {
	IOGuild::saveGuild(guild);
}

uint32_t GuildRepositoryDB::getGuildIdByName(const std::string &name) {
	return IOGuild::getGuildIdByName(name);
}

void GuildRepositoryDB::getWarList(uint32_t guildId, GuildWarVector &guildWarVector) {
	IOGuild::getWarList(guildId, guildWarVector);
}
