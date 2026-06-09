/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "io/guild_repository.hpp"

class GuildRepositoryDB final : public IGuildRepository {
public:
	GuildRepositoryDB() = default;

	[[nodiscard]] std::shared_ptr<Guild> loadGuild(uint32_t guildId) override;
	void saveGuild(const std::shared_ptr<Guild> &guild) override;
	[[nodiscard]] uint32_t getGuildIdByName(const std::string &name) override;
	void getWarList(uint32_t guildId, GuildWarVector &guildWarVector) override;
};
