/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/vip_repository_db.hpp"

#include "creatures/creatures_definitions.hpp"
#include "database/database.hpp"
#include "database/database_port.hpp"
#include "lib/di/container.hpp"

IVipRepository &IVipRepository::getInstance() {
	return DI::port<IVipRepository>();
}

std::vector<VIPEntry> VipRepositoryDB::getVIPEntries(uint32_t accountId) {
	const std::string query = fmt::format(
		"SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name`, `description`, `icon`, `notify` "
		"FROM `account_viplist` WHERE `account_id` = {}",
		accountId
	);
	std::vector<VIPEntry> entries;

	if (const auto &result = db_.storeQuery(query)) {
		entries.reserve(result->countResults());
		do {
			entries.emplace_back(
				result->getNumber<uint32_t>("player_id"),
				result->getString("name"),
				result->getString("description"),
				result->getNumber<uint32_t>("icon"),
				result->getNumber<uint16_t>("notify") != 0
			);
		} while (result->next());
	}

	return entries;
}

void VipRepositoryDB::addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	const std::string query = fmt::format(
		"INSERT INTO `account_viplist` (`account_id`, `player_id`, `description`, `icon`, `notify`) VALUES ({}, {}, {}, {}, {})",
		accountId,
		guid,
		db_.escapeString(description),
		icon,
		notify
	);
	if (!db_.executeQuery(query)) {
		g_logger().error("Failed to add VIP entry for account {}. QUERY: {}", accountId, query.c_str());
	}
}

void VipRepositoryDB::editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	const std::string query = fmt::format(
		"UPDATE `account_viplist` SET `description` = {}, `icon` = {}, `notify` = {} WHERE `account_id` = {} AND `player_id` = {}",
		db_.escapeString(description),
		icon,
		notify,
		accountId,
		guid
	);
	if (!db_.executeQuery(query)) {
		g_logger().error("Failed to edit VIP entry for account {}. QUERY: {}", accountId, query.c_str());
	}
}

void VipRepositoryDB::removeVIPEntry(uint32_t accountId, uint32_t guid) {
	const std::string query = fmt::format(
		"DELETE FROM `account_viplist` WHERE `account_id` = {} AND `player_id` = {}",
		accountId,
		guid
	);
	std::ignore = db_.executeQuery(query);
}

std::vector<VIPGroupEntry> VipRepositoryDB::getVIPGroupEntries(uint32_t accountId, uint32_t guid) {
	const std::string query = fmt::format(
		"SELECT `id`, `name`, `customizable` FROM `account_vipgroups` WHERE `account_id` = {}",
		accountId
	);

	std::vector<VIPGroupEntry> entries;

	if (const auto &result = db_.storeQuery(query)) {
		entries.reserve(result->countResults());

		do {
			entries.emplace_back(
				result->getNumber<uint32_t>("id"),
				result->getString("name"),
				result->getNumber<uint8_t>("customizable") != 0
			);
		} while (result->next());
	}
	return entries;
}

uint32_t VipRepositoryDB::addVIPGroupEntry(uint32_t accountId, const std::string &groupName, bool customizable) {
	// The id comes from the table's AUTO_INCREMENT, like the default groups the account trigger creates; it
	// used to be the client's 1..8, which could collide with the account's stored ids.
	const std::string query = fmt::format(
		"INSERT INTO `account_vipgroups` (`account_id`, `name`, `customizable`) VALUES ({}, {}, {})",
		accountId,
		db_.escapeString(groupName),
		customizable
	);
	if (!db_.executeQuery(query)) {
		g_logger().error("Failed to add VIP Group entry for account {}. QUERY: {}", accountId, query.c_str());
		return 0;
	}
	return static_cast<uint32_t>(db_.getLastInsertId());
}

void VipRepositoryDB::editVIPGroupEntry(uint32_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) {
	const std::string query = fmt::format(
		"UPDATE `account_vipgroups` SET `name` = {}, `customizable` = {} WHERE `id` = {} AND `account_id` = {}",
		db_.escapeString(groupName),
		customizable,
		groupId,
		accountId
	);
	if (!db_.executeQuery(query)) {
		g_logger().error("Failed to update VIP Group entry for account {} and group {}. QUERY: {}", accountId, groupId, query.c_str());
	}
}

void VipRepositoryDB::removeVIPGroupEntry(uint32_t groupId, uint32_t accountId) {
	const std::string query = fmt::format(
		"DELETE FROM `account_vipgroups` WHERE `id` = {} AND `account_id` = {}",
		groupId,
		accountId
	);
	std::ignore = db_.executeQuery(query);
}

void VipRepositoryDB::replaceVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify, const std::vector<uint32_t> &groupIds) {
	std::vector<std::string> statements;
	statements.emplace_back(fmt::format(
		"UPDATE `account_viplist` SET `description` = {}, `icon` = {}, `notify` = {} WHERE `account_id` = {} AND `player_id` = {}",
		db_.escapeString(description),
		icon,
		notify,
		accountId,
		guid
	));
	statements.emplace_back(fmt::format("DELETE FROM `account_vipgrouplist` WHERE `account_id` = {} AND `player_id` = {}", accountId, guid));
	if (!groupIds.empty()) {
		// IGNORE: a group id with no row in account_vipgroups (removed meanwhile) is skipped, as the
		// one-INSERT-per-group code did, instead of failing the whole edit.
		std::string insert = "INSERT IGNORE INTO `account_vipgrouplist` (`account_id`, `player_id`, `vipgroup_id`) VALUES ";
		for (size_t index = 0; index < groupIds.size(); ++index) {
			insert += fmt::format("{}({}, {}, {})", index == 0 ? "" : ", ", accountId, guid, groupIds[index]);
		}
		statements.emplace_back(std::move(insert));
	}
	if (!db_.commitStatements(statements)) {
		g_logger().error("Failed to replace VIP entry for account {} and player {}", accountId, guid);
	}
}

void VipRepositoryDB::addGuidVIPGroupEntry(uint32_t groupId, uint32_t accountId, uint32_t guid) {
	const std::string query = fmt::format(
		"INSERT INTO `account_vipgrouplist` (`account_id`, `player_id`, `vipgroup_id`) VALUES ({}, {}, {})",
		accountId,
		guid,
		groupId
	);
	if (!db_.executeQuery(query)) {
		g_logger().error("Failed to add guid VIP Group entry for account {}, player {} and group {}. QUERY: {}", accountId, guid, groupId, query.c_str());
	}
}

void VipRepositoryDB::removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) {
	const std::string query = fmt::format(
		"DELETE FROM `account_vipgrouplist` WHERE `account_id` = {} AND `player_id` = {}",
		accountId,
		guid
	);
	std::ignore = db_.executeQuery(query);
}
