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
#include <vector>

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
		onlinePlayers.clear();
		onlineUpdates = 0;
		failOnlineUpdate = false;
		playerLoads = 0;
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
		++playerLoads;
		auto it = byName.find(name);
		if (it == byName.end()) {
			return false;
		}
		player->setName(it->second.name);
		player->setGUID(it->second.id);
		return true;
	}

	bool loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id, bool = true) override {
		++playerLoads;
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

	bool formatPlayerName(std::string &name) override {
		const auto it = findByNameInsensitive(name);
		if (it == byName.end()) {
			return false;
		}
		name = it->second.name;
		return true;
	}

	bool getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) override {
		const auto it = findByNameInsensitive(name);
		if (it == byName.end()) {
			return false;
		}
		guid = it->second.id;
		specialVip = false;
		name = it->second.name;
		return true;
	}

	bool gameWorldAuthentication(const std::string &, const std::string &, std::string &, uint32_t &, bool, uint32_t) override {
		return false;
	}

	bool updateOnlinePlayers(const std::vector<uint32_t> &onlineGuids) override {
		++onlineUpdates;
		if (failOnlineUpdate) {
			return false;
		}
		onlinePlayers = onlineGuids;
		return true;
	}

	std::optional<HouseAccessFacts> loadHouseAccessFacts(uint32_t guid) override {
		const auto it = houseAccessFacts.find(guid);
		if (it == houseAccessFacts.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	std::unordered_map<uint32_t, HouseAccessFacts> houseAccessFacts;

	/// Last list written by updateOnlinePlayers, in the order the caller sent it.
	std::vector<uint32_t> onlinePlayers;
	uint32_t onlineUpdates = 0;
	size_t playerLoads = 0;
	bool failOnlineUpdate = false;

	uint64_t getBankBalance(uint32_t guid) const {
		auto it = bankBalances.find(guid);
		return it != bankBalances.end() ? it->second : 0;
	}

private:
	std::unordered_map<std::string, PlayerRecord>::const_iterator findByNameInsensitive(const std::string &name) const {
		const auto lower = [](std::string value) {
			std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		};
		const auto wanted = lower(name);
		return std::ranges::find_if(byName, [&](const auto &entry) { return lower(entry.first) == wanted; });
	}

	std::unordered_map<std::string, PlayerRecord> byName;
	std::unordered_map<uint32_t, PlayerRecord> byId;
	std::unordered_map<uint32_t, uint64_t> bankBalances;
};

template <>
struct TestInjection<IPlayerRepository> {
	using type = InMemoryPlayerRepository;
};
