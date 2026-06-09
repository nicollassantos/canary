/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "io/guild_repository.hpp"
#include "creatures/players/grouping/guild.hpp"
#include "test_injection.hpp"
#include "lib/di/container.hpp"

namespace di = boost::di;

class InMemoryGuildRepository final : public IGuildRepository {
public:
	InMemoryGuildRepository() = default;
	InMemoryGuildRepository(const InMemoryGuildRepository &) { }
	InMemoryGuildRepository(InMemoryGuildRepository &&) { }

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IGuildRepository>.to<InMemoryGuildRepository>().in(di::singleton));
		return injector;
	}

	InMemoryGuildRepository &reset() {
		guildsById.clear();
		guildsByName.clear();
		warLists.clear();
		return *this;
	}

	void addGuild(const std::shared_ptr<Guild> &guild) {
		guildsById[guild->getId()] = guild;
		guildsByName[guild->getName()] = guild;
	}

	std::shared_ptr<Guild> loadGuild(uint32_t guildId) override {
		auto it = guildsById.find(guildId);
		return it != guildsById.end() ? it->second : nullptr;
	}

	void saveGuild(const std::shared_ptr<Guild> &guild) override {
		if (!guild) {
			return;
		}
		guildsById[guild->getId()] = guild;
		guildsByName[guild->getName()] = guild;
	}

	uint32_t getGuildIdByName(const std::string &name) override {
		auto it = guildsByName.find(name);
		return it != guildsByName.end() ? it->second->getId() : 0;
	}

	void getWarList(uint32_t guildId, GuildWarVector &guildWarVector) override {
		auto it = warLists.find(guildId);
		if (it != warLists.end()) {
			guildWarVector = it->second;
		}
	}

	void setWarList(uint32_t guildId, GuildWarVector wars) {
		warLists[guildId] = std::move(wars);
	}

private:
	std::unordered_map<uint32_t, std::shared_ptr<Guild>> guildsById;
	std::unordered_map<std::string, std::shared_ptr<Guild>> guildsByName;
	std::unordered_map<uint32_t, GuildWarVector> warLists;
};

template <>
struct TestInjection<IGuildRepository> {
	using type = InMemoryGuildRepository;
};
