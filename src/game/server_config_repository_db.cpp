/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/server_config_repository_db.hpp"

#include "database/database.hpp"
#include "lib/di/container.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <fmt/format.h>
#endif

IServerConfigRepository &IServerConfigRepository::getInstance() {
	return DI::port<IServerConfigRepository>();
}

ServerConfigRepositoryDB::ServerConfigRepositoryDB(IDatabase &db) :
	db(db) { }

std::optional<std::string> ServerConfigRepositoryDB::find(std::string_view key) {
	const auto result = db.storeQuery(fmt::format("SELECT `value` FROM `server_config` WHERE `config` = {}", db.escapeString(std::string(key))));
	return result ? std::optional<std::string>(result->getString("value")) : std::nullopt;
}

void ServerConfigRepositoryDB::insert(std::string_view key, std::string_view value) {
	std::ignore = db.executeQuery(fmt::format("INSERT INTO `server_config` (`config`, `value`) VALUES ({}, {})", db.escapeString(std::string(key)), db.escapeString(std::string(value))));
}

void ServerConfigRepositoryDB::update(std::string_view key, std::string_view value) {
	std::ignore = db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = {} WHERE `config` = {}", db.escapeString(std::string(value)), db.escapeString(std::string(key))));
}

std::optional<BoostedCreatureRecord> ServerConfigRepositoryDB::loadBoostedCreature() {
	const auto result = db.storeQuery("SELECT `date`, `boostname`, `raceid` FROM `boosted_creature`");
	if (!result) {
		return std::nullopt;
	}
	return BoostedCreatureRecord { result->getNumber<uint16_t>("date"), result->getString("boostname"), result->getNumber<uint16_t>("raceid") };
}

bool ServerConfigRepositoryDB::storeBoostedCreature(const BoostedCreatureRecord &creature, const BoostedCreatureLook &look) {
	return db.executeQuery(fmt::format(
		"UPDATE `boosted_creature` SET `date` = '{}', `boostname` = {}, `looktype` = {}, `lookfeet` = {}, `looklegs` = {}, `lookhead` = {}, `lookbody` = {}, `lookaddons` = {}, `lookmount` = {}, `raceid` = '{}'",
		creature.day, db.escapeString(creature.name), look.lookType, look.lookFeet, look.lookLegs, look.lookHead, look.lookBody, look.lookAddons, look.lookMount, creature.raceId
	));
}

std::optional<BoostedCreatureRecord> ServerConfigRepositoryDB::loadBoostedBoss() {
	const auto result = db.storeQuery("SELECT `date`, `boostname`, `raceid` FROM `boosted_boss`");
	if (!result) {
		return std::nullopt;
	}
	return BoostedCreatureRecord { result->getNumber<uint16_t>("date"), result->getString("boostname"), result->getNumber<uint16_t>("raceid") };
}

bool ServerConfigRepositoryDB::storeBoostedBoss(const BoostedCreatureRecord &boss, const std::optional<BoostedBossLook> &look) {
	std::string update = fmt::format("UPDATE `boosted_boss` SET `date` = '{}', `boostname` = {}, ", boss.day, db.escapeString(boss.name));
	if (look) {
		update += fmt::format(
			"`looktypeEx` = {}, `looktype` = {}, `lookfeet` = {}, `looklegs` = {}, `lookhead` = {}, `lookbody` = {}, `lookaddons` = {}, `lookmount` = {}, ",
			look->lookTypeEx, look->lookType, look->lookFeet, look->lookLegs, look->lookHead, look->lookBody, look->lookAddons, look->lookMount
		);
	}
	update += fmt::format("`raceid` = '{}'", boss.raceId);
	// Upstream ran these as three statements: the slots could be released, or not, apart from the boss.
	return db.commitStatements({
		std::move(update),
		fmt::format("UPDATE `player_bosstiary` SET `bossIdSlotOne` = IF(`bossIdSlotOne` = {0}, 0, `bossIdSlotOne`), `bossIdSlotTwo` = IF(`bossIdSlotTwo` = {0}, 0, `bossIdSlotTwo`) WHERE `bossIdSlotOne` = {0} OR `bossIdSlotTwo` = {0}", boss.raceId),
		// Tibia: one free change after each server save; the boss of the day is drawn then.
		"UPDATE `player_bosstiary` SET `removeTimes` = 1 WHERE `removeTimes` <> 1",
	});
}
