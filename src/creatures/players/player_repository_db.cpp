/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/player_repository_db.hpp"

#include "creatures/players/grouping/groups.hpp"
#include "database/database.hpp"
#include "database/database_port.hpp"
#include "game/game.hpp"
#include "io/iologindata.hpp"
#include "lib/di/container.hpp"

IPlayerRepository &IPlayerRepository::getInstance() {
	return DI::port<IPlayerRepository>();
}

bool PlayerRepositoryDB::loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id, bool disableIrrelevantInfo) {
	return IOLoginData::loadPlayerById(player, id, disableIrrelevantInfo);
}

bool PlayerRepositoryDB::loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) {
	return IOLoginData::loadPlayerByName(player, name);
}

bool PlayerRepositoryDB::savePlayer(const std::shared_ptr<Player> &player) {
	return IOLoginData::savePlayer(player);
}

bool PlayerRepositoryDB::gameWorldAuthentication(const std::string &accountDescriptor, const std::string &sessionOrPassword, std::string &characterName, uint32_t &accountId, bool oldProtocol, uint32_t ip) {
	return IOLoginData::gameWorldAuthentication(accountDescriptor, sessionOrPassword, characterName, accountId, oldProtocol, ip);
}

std::string PlayerRepositoryDB::getNameByGuid(uint32_t guid) {
	std::ostringstream query;
	query << "SELECT `name` FROM `players` WHERE `id` = " << guid;
	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return {};
	}
	return result->getString("name");
}

uint32_t PlayerRepositoryDB::getGuidByName(const std::string &name) {
	std::ostringstream query;
	query << "SELECT `id` FROM `players` WHERE `name` = " << db_.escapeString(name);
	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool PlayerRepositoryDB::getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) {
	std::ostringstream query;
	query << "SELECT `name`, `id`, `group_id`, `account_id` FROM `players` WHERE `name` = " << db_.escapeString(name);
	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return false;
	}

	name = result->getString("name");
	guid = result->getNumber<uint32_t>("id");
	if (auto group = g_game().groups.getGroup(result->getNumber<uint16_t>("group_id"))) {
		specialVip = group->flags[Groups::getFlagNumber(PlayerFlags_t::SpecialVIP)];
	} else {
		specialVip = false;
	}
	return true;
}

bool PlayerRepositoryDB::formatPlayerName(std::string &name) {
	std::ostringstream query;
	query << "SELECT `name` FROM `players` WHERE `name` = " << db_.escapeString(name);

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return false;
	}

	name = result->getString("name");
	return true;
}

void PlayerRepositoryDB::increaseBankBalance(uint32_t guid, uint64_t bankBalance) {
	std::ostringstream query;
	query << "UPDATE `players` SET `balance` = `balance` + " << bankBalance << " WHERE `id` = " << guid;
	std::ignore = db_.executeQuery(query.str());
}

bool PlayerRepositoryDB::updateOnlinePlayers(const std::vector<uint32_t> &onlineGuids) {
	auto updateOperation = [this, &onlineGuids]() {
		if (onlineGuids.empty()) {
			auto result = db_.storeQuery("SELECT COUNT(*) AS count FROM players_online;");
			if (!result) {
				g_logger().error("[Game::updatePlayersOnline] Failed to count players_online records.");
				return false;
			}

			if (result->getNumber<int>("count") == 0) {
				return true;
			}

			if (!db_.executeQuery("DELETE FROM `players_online`;")) {
				g_logger().error("[Game::updatePlayersOnline] Failed to clear players_online records.");
				return false;
			}

			return true;
		}

		DBInsert stmt("INSERT IGNORE INTO `players_online` (`player_id`) VALUES ");
		for (const auto playerGuid : onlineGuids) {
			if (!stmt.addRow(fmt::format("{}", playerGuid))) {
				g_logger().error("[Game::updatePlayersOnline] Failed to add players_online insert row.");
				return false;
			}
		}

		if (!stmt.execute()) {
			g_logger().error("[Game::updatePlayersOnline] Failed to insert players_online records.");
			return false;
		}

		const auto cleanupQuery = fmt::format(
			"DELETE FROM `players_online` WHERE `player_id` NOT IN ({});",
			fmt::join(onlineGuids, ",")
		);
		if (!db_.executeQuery(cleanupQuery)) {
			g_logger().error("[Game::updatePlayersOnline] Failed to prune offline players_online records.");
			return false;
		}

		return true;
	};

	// DBTransaction and DBInsert run on the global Database, the same instance injected as db_.
	return DBTransaction::executeWithinTransactionRollbackOnFailure(updateOperation);
}

std::optional<HouseAccessFacts> PlayerRepositoryDB::loadHouseAccessFacts(uint32_t guid) {
	const auto result = db_.storeQuery(fmt::format(
		"SELECT `p`.`account_id`, `p`.`group_id`, COALESCE(`gm`.`rank_id`, 0) AS `rank_id` FROM `players` `p` "
		"LEFT JOIN `guild_membership` `gm` ON `gm`.`player_id` = `p`.`id` WHERE `p`.`id` = {}",
		guid
	));
	if (!result) {
		return std::nullopt;
	}

	HouseAccessFacts facts;
	facts.guid = guid;
	facts.accountId = result->getNumber<uint32_t>("account_id");
	facts.guildRankId = result->getNumber<uint32_t>("rank_id");
	if (const auto group = g_game().groups.getGroup(result->getNumber<uint16_t>("group_id"))) {
		facts.canEditHouses = group->flags[Groups::getFlagNumber(PlayerFlags_t::CanEditHouses)];
	}
	return facts;
}
