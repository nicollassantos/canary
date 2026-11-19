/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <optional>
	#include <string>
	#include <string_view>
#endif

/// The boosted creature of the day as stored (`boosted_creature`, a single row).
struct BoostedCreatureRecord {
	/// Day of the month it was chosen.
	uint16_t day = 0;
	std::string name;
	uint16_t raceId = 0;
};

struct BoostedCreatureLook {
	uint16_t lookType = 0;
	uint8_t lookFeet = 0;
	uint8_t lookLegs = 0;
	uint8_t lookHead = 0;
	uint8_t lookBody = 0;
	uint8_t lookAddons = 0;
	uint16_t lookMount = 0;
};

/// The boosted boss's outfit, as `boosted_boss` keeps it (for the website).
struct BoostedBossLook {
	uint16_t lookTypeEx = 0;
	uint16_t lookType = 0;
	uint8_t lookFeet = 0;
	uint8_t lookLegs = 0;
	uint8_t lookHead = 0;
	uint8_t lookBody = 0;
	uint8_t lookAddons = 0;
	uint16_t lookMount = 0;
};

/// Server-wide values kept in the database (driven port): the `server_config` key/value table
/// (motd number and hash, players record) and the boosted creature. Game keeps the rules; tests use
/// an in-memory fake (tests/fixture/game/in_memory_server_config_repository.hpp).
class IServerConfigRepository {
public:
	IServerConfigRepository() = default;
	virtual ~IServerConfigRepository() = default;
	IServerConfigRepository(const IServerConfigRepository &) = delete;
	void operator=(const IServerConfigRepository &) = delete;

	static IServerConfigRepository &getInstance();

	[[nodiscard]] virtual std::optional<std::string> find(std::string_view key) = 0;
	virtual void insert(std::string_view key, std::string_view value) = 0;
	virtual void update(std::string_view key, std::string_view value) = 0;

	[[nodiscard]] virtual std::optional<BoostedCreatureRecord> loadBoostedCreature() = 0;
	[[nodiscard]] virtual bool storeBoostedCreature(const BoostedCreatureRecord &creature, const BoostedCreatureLook &look) = 0;

	/// The boosted boss of the day as stored (`boosted_boss`, a single row).
	[[nodiscard]] virtual std::optional<BoostedCreatureRecord> loadBoostedBoss() = 0;
	/// Stores a new boosted boss and takes it out of every player's Bosstiary slots, as upstream did (Tibia's
	/// manual does not say what happens to a slot boss that becomes the boosted one), in one transaction;
	/// `look` is empty when the boss has no monster type.
	[[nodiscard]] virtual bool storeBoostedBoss(const BoostedCreatureRecord &boss, const std::optional<BoostedBossLook> &look) = 0;
};

constexpr auto g_serverConfigRepository = IServerConfigRepository::getInstance;
