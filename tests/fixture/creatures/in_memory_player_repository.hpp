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

#include "creatures/players/player_repository.hpp"
#include "creatures/players/player.hpp"
#include "test_injection.hpp"
#include "lib/di/container.hpp"

namespace di = boost::di;

class InMemoryPlayerRepository final : public IPlayerRepository {
	struct PlayerRecord {
		uint32_t id;
		std::string name;
	};

public:
	InMemoryPlayerRepository() = default;
	InMemoryPlayerRepository(const InMemoryPlayerRepository &) { }
	InMemoryPlayerRepository(InMemoryPlayerRepository &&) { }

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IPlayerRepository>.to<InMemoryPlayerRepository>().in(di::singleton));
		return injector;
	}

	InMemoryPlayerRepository &reset() {
		byName.clear();
		byId.clear();
		bankBalances.clear();
		return *this;
	}

	bool savePlayer(const std::shared_ptr<Player> &player) override {
		if (!player) {
			return false;
		}
		PlayerRecord rec { player->getGUID(), player->getName() };
		byName[rec.name] = rec;
		byId[rec.id] = rec;
		return true;
	}

	bool loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) override {
		auto it = byName.find(name);
		if (it == byName.end()) {
			return false;
		}
		player->setName(it->second.name);
		player->setGUID(it->second.id);
		return true;
	}

	bool loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id) override {
		auto it = byId.find(id);
		if (it == byId.end()) {
			return false;
		}
		player->setName(it->second.name);
		player->setGUID(it->second.id);
		return true;
	}

	uint32_t getGuidByName(const std::string &name) override {
		auto it = byName.find(name);
		return it != byName.end() ? it->second.id : 0;
	}

	std::string getNameByGuid(uint32_t guid) override {
		auto it = byId.find(guid);
		return it != byId.end() ? it->second.name : std::string {};
	}

	void increaseBankBalance(uint32_t guid, uint64_t amount) override {
		bankBalances[guid] += amount;
	}

	uint64_t getBankBalance(uint32_t guid) const {
		auto it = bankBalances.find(guid);
		return it != bankBalances.end() ? it->second : 0;
	}

private:
	std::unordered_map<std::string, PlayerRecord> byName;
	std::unordered_map<uint32_t, PlayerRecord> byId;
	std::unordered_map<uint32_t, uint64_t> bankBalances;
};

template <>
struct TestInjection<IPlayerRepository> {
	using type = InMemoryPlayerRepository;
};
