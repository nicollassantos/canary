/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/components/world_registry.hpp"
#include "creatures/players/player.hpp"
#include "lib/di/container.hpp"

#include <memory>
#include <vector>

/// In-memory world for player components: an online-player map and a configurable world type.
class FakeWorldRegistry final : public WorldRegistry {
public:
	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<WorldRegistry>.to<FakeWorldRegistry>().in(di::singleton));
		return injector;
	}

	static FakeWorldRegistry &installed() {
		return dynamic_cast<FakeWorldRegistry &>(WorldRegistry::getInstance());
	}

	std::shared_ptr<Player> getPlayerByID(uint32_t id, bool) override {
		const auto it = players.find(id);
		return it == players.end() ? nullptr : it->second;
	}

	std::shared_ptr<Creature> getCreatureByID(uint32_t id) override {
		return getPlayerByID(id, false);
	}

	const PlayerMap &getPlayers() const override {
		return players;
	}

	void addPlayer(const std::shared_ptr<Player> &player) override {
		players[player->getID()] = player;
		++additions;
	}

	void removePlayer(const std::shared_ptr<Player> &player) override {
		players.erase(player->getID());
		++removals;
	}

	bool removeCreature(const std::shared_ptr<Creature> &creature, bool isLogout) override {
		removedCreatures.push_back(creature);
		lastRemovalWasLogout = isLogout;
		return true;
	}

	void checkPlayersRecord() override {
		++recordChecks;
	}

	WorldType_t getWorldType() const override {
		return worldType;
	}

	WorldType_t worldType = WORLD_TYPE_PVP;
	PlayerMap players;
	uint32_t additions = 0;
	uint32_t removals = 0;
	uint32_t recordChecks = 0;
	std::vector<std::shared_ptr<Creature>> removedCreatures;
	bool lastRemovalWasLogout = false;
};
