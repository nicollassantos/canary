/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/player_progress_repository.hpp"
#include "database/database_port.hpp"

/// IPlayerProgressRepository on the database: the SQL that was in PlayerForgeHistory, PlayerWheel,
/// PlayerTitle, PlayerCyclopedia and PlayerBadge.
class PlayerProgressRepositoryDB final : public IPlayerProgressRepository {
public:
	explicit PlayerProgressRepositoryDB(IDatabase &db);

	[[nodiscard]] std::vector<ForgeHistory> loadForgeHistory(uint32_t playerGuid) override;
	[[nodiscard]] bool saveForgeHistory(uint32_t playerGuid, const std::vector<uint32_t> &removedIds, const std::vector<ForgeHistory> &added) override;
	[[nodiscard]] std::optional<std::string> loadWheelSlots(uint32_t playerGuid) override;
	[[nodiscard]] bool saveWheelSlots(uint32_t playerGuid, const std::string &slots) override;
	[[nodiscard]] std::optional<uint32_t> findCharmPointsLeader() override;
	[[nodiscard]] std::optional<uint32_t> findSkillLeader(std::string_view skillColumn) override;
	[[nodiscard]] std::optional<uint32_t> countHirelings(uint32_t playerGuid) override;
	void loadRecentDeaths(uint32_t playerGuid, uint32_t offset, uint16_t limit, std::function<void(std::optional<RecentDeathsPage>)> done) override;
	void loadRecentPvpKills(const std::string &killerName, uint32_t offset, uint16_t limit, std::function<void(std::optional<PvpKillsPage>)> done) override;
	[[nodiscard]] std::vector<CharacterSummary> loadCharacterSummaries(const std::vector<std::string> &names) override;

	/// The queries and row readers behind the asynchronous loads (integration tests run them directly).
	[[nodiscard]] static std::string recentDeathsQuery(uint32_t playerGuid, uint32_t offset, uint16_t limit);
	[[nodiscard]] static std::optional<RecentDeathsPage> readRecentDeaths(const DBResult_ptr &result);
	[[nodiscard]] std::string recentPvpKillsQuery(const std::string &killerName, uint32_t offset, uint16_t limit) const;
	[[nodiscard]] static std::optional<PvpKillsPage> readRecentPvpKills(const DBResult_ptr &result);

private:
	IDatabase &db;
};
