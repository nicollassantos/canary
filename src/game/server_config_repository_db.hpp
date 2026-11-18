/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "game/server_config_repository.hpp"

class IDatabase;

/// IServerConfigRepository on the database (`server_config`, `boosted_creature`): the SQL that was in
/// Game::loadMotdNum/saveMotdNum, loadPlayersRecord/updatePlayersRecord and loadBoostedCreature.
class ServerConfigRepositoryDB final : public IServerConfigRepository {
public:
	explicit ServerConfigRepositoryDB(IDatabase &db);

	[[nodiscard]] std::optional<std::string> find(std::string_view key) override;
	void insert(std::string_view key, std::string_view value) override;
	void update(std::string_view key, std::string_view value) override;
	[[nodiscard]] std::optional<BoostedCreatureRecord> loadBoostedCreature() override;
	[[nodiscard]] bool storeBoostedCreature(const BoostedCreatureRecord &creature, const BoostedCreatureLook &look) override;

private:
	IDatabase &db;
};
