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
#include <vector>

class Guild;
using GuildWarVector = std::vector<uint32_t>;

class IGuildRepository {
public:
	IGuildRepository() = default;
	virtual ~IGuildRepository() = default;

	IGuildRepository(const IGuildRepository &) = delete;
	void operator=(const IGuildRepository &) = delete;

	static IGuildRepository &getInstance();

	[[nodiscard]] virtual std::shared_ptr<Guild> loadGuild(uint32_t guildId) = 0;
	virtual void saveGuild(const std::shared_ptr<Guild> &guild) = 0;
	[[nodiscard]] virtual uint32_t getGuildIdByName(const std::string &name) = 0;
	virtual void getWarList(uint32_t guildId, GuildWarVector &guildWarVector) = 0;
};

constexpr auto g_guildRepository = IGuildRepository::getInstance;
