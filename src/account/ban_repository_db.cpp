/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "account/ban_repository_db.hpp"

#include "database/database.hpp"
#include "database/databasetasks.hpp"
#include "lib/di/container.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <fmt/format.h>
#endif

IBanRepository &IBanRepository::getInstance() {
	return DI::port<IBanRepository>();
}

BanRepositoryDB::BanRepositoryDB(IDatabase &db) :
	db(db) { }

void BanRepositoryDB::runLater(const std::string &query) {
	g_databaseTasks().execute(query);
}

time_t BanRepositoryDB::now() const {
	return time(nullptr);
}

std::optional<BanInfo> BanRepositoryDB::findAccountBan(uint32_t accountId) {
	const auto result = db.storeQuery(fmt::format(
		"SELECT `reason`, `expires_at`, `banned_at`, `banned_by`, (SELECT `name` FROM `players` WHERE `id` = `banned_by`) AS `name` FROM `account_bans` WHERE `account_id` = {}",
		accountId
	));
	if (!result) {
		return std::nullopt;
	}

	const auto expiresAt = result->getNumber<int64_t>("expires_at");
	if (expiresAt != 0 && now() > expiresAt) {
		// Expired: move it to the history.
		runLater(fmt::format(
			"INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES ({}, {}, {}, {}, {})",
			accountId, db.escapeString(result->getString("reason")), result->getNumber<time_t>("banned_at"), expiresAt, result->getNumber<uint32_t>("banned_by")
		));
		runLater(fmt::format("DELETE FROM `account_bans` WHERE `account_id` = {}", accountId));
		return std::nullopt;
	}

	return BanInfo { result->getString("name"), result->getString("reason"), static_cast<time_t>(expiresAt) };
}

std::optional<BanInfo> BanRepositoryDB::findAddressBan(uint32_t ip) {
	if (ip == 0) {
		return std::nullopt;
	}

	const auto result = db.storeQuery(fmt::format(
		"SELECT `reason`, `expires_at`, (SELECT `name` FROM `players` WHERE `id` = `banned_by`) AS `name` FROM `ip_bans` WHERE `ip` = {}",
		ip
	));
	if (!result) {
		return std::nullopt;
	}

	const auto expiresAt = result->getNumber<int64_t>("expires_at");
	if (expiresAt != 0 && now() > expiresAt) {
		runLater(fmt::format("DELETE FROM `ip_bans` WHERE `ip` = {}", ip));
		return std::nullopt;
	}

	return BanInfo { result->getString("name"), result->getString("reason"), static_cast<time_t>(expiresAt) };
}

bool BanRepositoryDB::isNamelocked(uint32_t playerGuid) {
	return db.storeQuery(fmt::format("SELECT 1 FROM `player_namelocks` WHERE `player_id` = {}", playerGuid)) != nullptr;
}
