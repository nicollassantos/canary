/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/components/player_forge_history.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <functional>
	#include <optional>
	#include <string>
	#include <string_view>
	#include <vector>
#endif

/// A death of the last 30 days, as stored.
struct RecentDeathRow {
	uint32_t time = 0;
	uint32_t level = 0;
	std::string killedBy;
	std::string mostDamageBy;
	/// "Name: X\nType: player|monster\n..." lines.
	std::string participants;
};

struct RecentDeathsPage {
	/// Deaths in the period, all pages.
	uint32_t totalEntries = 0;
	std::vector<RecentDeathRow> rows;
};

/// A death caused by a given player in the last 70 days.
struct PvpKillRow {
	uint32_t time = 0;
	std::string killedBy;
	std::string mostDamageBy;
	bool unjustified = false;
	bool mostDamageUnjustified = false;
	std::string victim;
};

struct PvpKillsPage {
	uint32_t totalEntries = 0;
	std::vector<PvpKillRow> rows;
};

struct CharacterSummary {
	std::string name;
	uint32_t level = 0;
	uint16_t vocation = 0;
};

/// What player components keep in the database besides the character save (driven port): forge
/// history, wheel slot points, the highscore leaders titles check, hirelings, the recent deaths and
/// kills of the Cyclopedia, the characters of an account for badges. The components keep their rules
/// and blob formats; tests use an in-memory fake (tests/fixture/creatures/in_memory_player_progress_repository.hpp).
class IPlayerProgressRepository {
public:
	IPlayerProgressRepository() = default;
	virtual ~IPlayerProgressRepository() = default;
	IPlayerProgressRepository(const IPlayerProgressRepository &) = delete;
	void operator=(const IPlayerProgressRepository &) = delete;

	static IPlayerProgressRepository &getInstance();

	[[nodiscard]] virtual std::vector<ForgeHistory> loadForgeHistory(uint32_t playerGuid) = 0;
	/// Deletes `removedIds` and inserts `added`.
	[[nodiscard]] virtual bool saveForgeHistory(uint32_t playerGuid, const std::vector<uint32_t> &removedIds, const std::vector<ForgeHistory> &added) = 0;

	/// The wheel slot points blob (PlayerWheel's format); nothing when never saved.
	[[nodiscard]] virtual std::optional<std::string> loadWheelSlots(uint32_t playerGuid) = 0;
	[[nodiscard]] virtual bool saveWheelSlots(uint32_t playerGuid, const std::string &slots) = 0;

	/// The non-staff character with the most charm points.
	[[nodiscard]] virtual std::optional<uint32_t> findCharmPointsLeader() = 0;
	/// The non-staff character highest in `skillColumn` (a `players` column) above 10.
	[[nodiscard]] virtual std::optional<uint32_t> findSkillLeader(std::string_view skillColumn) = 0;

	[[nodiscard]] virtual std::optional<uint32_t> countHirelings(uint32_t playerGuid) = 0;
	/// Asynchronous; `done` runs on the dispatcher (nothing when there are no rows).
	virtual void loadRecentDeaths(uint32_t playerGuid, uint32_t offset, uint16_t limit, std::function<void(std::optional<RecentDeathsPage>)> done) = 0;
	virtual void loadRecentPvpKills(const std::string &killerName, uint32_t offset, uint16_t limit, std::function<void(std::optional<PvpKillsPage>)> done) = 0;

	[[nodiscard]] virtual std::vector<CharacterSummary> loadCharacterSummaries(const std::vector<std::string> &names) = 0;
};

constexpr auto g_playerProgressRepository = IPlayerProgressRepository::getInstance;
