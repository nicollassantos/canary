/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/player_progress_repository_db.hpp"

#include "database/database.hpp"
#include "database/databasetasks.hpp"
#include "enums/account_group_type.hpp"
#include "lib/di/container.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
	#include <fmt/format.h>
	#include <fmt/ranges.h>
#endif

IPlayerProgressRepository &IPlayerProgressRepository::getInstance() {
	return DI::port<IPlayerProgressRepository>();
}

PlayerProgressRepositoryDB::PlayerProgressRepositoryDB(IDatabase &db) :
	db(db) { }

std::vector<ForgeHistory> PlayerProgressRepositoryDB::loadForgeHistory(uint32_t playerGuid) {
	std::vector<ForgeHistory> history;
	const auto result = db.storeQuery(fmt::format("SELECT * FROM forge_history WHERE player_id = {}", playerGuid));
	if (!result) {
		return history;
	}
	do {
		ForgeHistory entry;
		entry.id = result->getNumber<uint32_t>("id");
		entry.actionType = static_cast<ForgeAction_t>(result->getNumber<uint8_t>("action_type"));
		entry.description = result->getString("description");
		entry.createdAt = result->getNumber<uint64_t>("done_at");
		entry.success = result->getNumber<bool>("is_success");
		history.push_back(std::move(entry));
	} while (result->next());
	return history;
}

bool PlayerProgressRepositoryDB::saveForgeHistory(uint32_t playerGuid, const std::vector<uint32_t> &removedIds, const std::vector<ForgeHistory> &added) {
	if (!removedIds.empty() && !db.executeQuery(fmt::format("DELETE FROM `forge_history` WHERE `player_id` = {} AND `id` IN ({})", playerGuid, fmt::join(removedIds, ", ")))) {
		return false;
	}
	DBInsert insert("INSERT INTO `forge_history` (`player_id`, `action_type`, `description`, `done_at`, `is_success`) VALUES ");
	for (const auto &entry : added) {
		if (!insert.addRow(fmt::format("{}, {}, {}, {}, {}", playerGuid, static_cast<int>(entry.actionType), db.escapeString(entry.description), entry.createdAt, entry.success ? 1 : 0))) {
			return false;
		}
	}
	return insert.execute();
}

std::optional<std::string> PlayerProgressRepositoryDB::loadWheelSlots(uint32_t playerGuid) {
	const auto result = db.storeQuery(fmt::format("SELECT `slot` FROM `player_wheeldata` WHERE `player_id` = {}", playerGuid));
	if (!result) {
		return std::nullopt;
	}
	unsigned long size = 0;
	const char* slots = result->getStream("slot", size);
	return std::string(slots, size);
}

bool PlayerProgressRepositoryDB::saveWheelSlots(uint32_t playerGuid, const std::string &slots) {
	if (slots.empty()) {
		return true;
	}
	DBInsert insert("INSERT INTO `player_wheeldata` (`player_id`, `slot`) VALUES ");
	insert.upsert({ "slot" });
	if (!insert.addRow(fmt::format("{}, {}", playerGuid, db.escapeBlob(slots.data(), static_cast<uint32_t>(slots.size()))))) {
		return false;
	}
	return insert.execute();
}

std::optional<uint32_t> PlayerProgressRepositoryDB::findCharmPointsLeader() {
	const auto result = db.storeQuery(fmt::format(
		"SELECT `pc`.`player_id`, `pc`.`charm_points`, `p`.`group_id` FROM `player_charms` pc JOIN `players` p ON `pc`.`player_id` = `p`.`id` WHERE `p`.`group_id` < {} ORDER BY `pc`.`charm_points` DESC LIMIT 1",
		static_cast<int>(GROUP_TYPE_GAMEMASTER)
	));
	return result ? std::optional<uint32_t>(result->getNumber<uint32_t>("player_id")) : std::nullopt;
}

std::optional<uint32_t> PlayerProgressRepositoryDB::findSkillLeader(std::string_view skillColumn) {
	// The column goes into the SQL as a name: only a plain identifier is accepted.
	if (skillColumn.empty() || !std::ranges::all_of(skillColumn, [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; })) {
		return std::nullopt;
	}
	const auto result = db.storeQuery(fmt::format(
		"SELECT `id` FROM `players` WHERE `group_id` < {} AND `{}` > 10 ORDER BY `{}` DESC LIMIT 1",
		static_cast<int>(GROUP_TYPE_GAMEMASTER), skillColumn, skillColumn
	));
	return result ? std::optional<uint32_t>(result->getNumber<uint32_t>("id")) : std::nullopt;
}

std::optional<uint32_t> PlayerProgressRepositoryDB::countHirelings(uint32_t playerGuid) {
	const auto result = db.storeQuery(fmt::format("SELECT COUNT(*) as `count` FROM `player_hirelings` WHERE `player_id` = {}", playerGuid));
	return result ? std::optional<uint32_t>(result->getNumber<uint32_t>("count")) : std::nullopt;
}

std::string PlayerProgressRepositoryDB::recentDeathsQuery(uint32_t playerGuid, uint32_t offset, uint16_t limit) {
	return fmt::format(
		"SELECT `time`, `level`, `killed_by`, `mostdamage_by`, `participants`, (select count(*) FROM `player_deaths` WHERE `player_id` = {} AND `time` >= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 30 DAY))) as `entries` FROM `player_deaths` WHERE `player_id` = {} AND `time` >= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 30 DAY)) ORDER BY `time` DESC LIMIT {}, {}",
		playerGuid, playerGuid, offset, limit
	);
}

std::optional<RecentDeathsPage> PlayerProgressRepositoryDB::readRecentDeaths(const DBResult_ptr &result) {
	if (!result) {
		return std::nullopt;
	}
	RecentDeathsPage page;
	page.totalEntries = result->getNumber<uint32_t>("entries");
	do {
		page.rows.push_back({ result->getNumber<uint32_t>("time"), result->getNumber<uint32_t>("level"), result->getString("killed_by"), result->getString("mostdamage_by"), result->getString("participants") });
	} while (result->next());
	return page;
}

void PlayerProgressRepositoryDB::loadRecentDeaths(uint32_t playerGuid, uint32_t offset, uint16_t limit, std::function<void(std::optional<RecentDeathsPage>)> done) {
	g_databaseTasks().store(recentDeathsQuery(playerGuid, offset, limit), [done = std::move(done)](const DBResult_ptr &result, bool) {
		done(readRecentDeaths(result));
	});
}

std::string PlayerProgressRepositoryDB::recentPvpKillsQuery(const std::string &killerName, uint32_t offset, uint16_t limit) const {
	const auto name = db.escapeString(killerName);
	return fmt::format(
		"SELECT `d`.`time`, `d`.`killed_by`, `d`.`mostdamage_by`, `d`.`unjustified`, `d`.`mostdamage_unjustified`, `p`.`name`, (select count(*) FROM `player_deaths` WHERE ((`killed_by` = {} AND `is_player` = 1) OR (`mostdamage_by` = {} AND `mostdamage_is_player` = 1))) as `entries` FROM `player_deaths` AS `d` INNER JOIN `players` AS `p` ON `d`.`player_id` = `p`.`id` WHERE ((`d`.`killed_by` = {} AND `d`.`is_player` = 1) OR (`d`.`mostdamage_by` = {} AND `d`.`mostdamage_is_player` = 1)) AND `time` >= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 70 DAY)) ORDER BY `time` DESC LIMIT {}, {}",
		name, name, name, name, offset, limit
	);
}

std::optional<PvpKillsPage> PlayerProgressRepositoryDB::readRecentPvpKills(const DBResult_ptr &result) {
	if (!result) {
		return std::nullopt;
	}
	PvpKillsPage page;
	page.totalEntries = result->getNumber<uint32_t>("entries");
	do {
		page.rows.push_back({
			result->getNumber<uint32_t>("time"),
			result->getString("killed_by"),
			result->getString("mostdamage_by"),
			result->getNumber<uint32_t>("unjustified") == 1,
			result->getNumber<uint32_t>("mostdamage_unjustified") == 1,
			result->getString("name"),
		});
	} while (result->next());
	return page;
}

void PlayerProgressRepositoryDB::loadRecentPvpKills(const std::string &killerName, uint32_t offset, uint16_t limit, std::function<void(std::optional<PvpKillsPage>)> done) {
	g_databaseTasks().store(recentPvpKillsQuery(killerName, offset, limit), [done = std::move(done)](const DBResult_ptr &result, bool) {
		done(readRecentPvpKills(result));
	});
}

std::vector<CharacterSummary> PlayerProgressRepositoryDB::loadCharacterSummaries(const std::vector<std::string> &names) {
	std::vector<CharacterSummary> characters;
	if (names.empty()) {
		return characters;
	}
	std::vector<std::string> escaped;
	escaped.reserve(names.size());
	for (const auto &name : names) {
		escaped.push_back(db.escapeString(name));
	}
	const auto result = db.storeQuery(fmt::format("SELECT name, level, vocation FROM players WHERE name IN ({})", fmt::join(escaped, ", ")));
	if (!result) {
		return characters;
	}
	do {
		characters.push_back({ result->getString("name"), result->getNumber<uint32_t>("level"), result->getNumber<uint16_t>("vocation") });
	} while (result->next());
	return characters;
}
