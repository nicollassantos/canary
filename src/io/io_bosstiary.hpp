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

#ifndef USE_PRECOMPILED_HEADERS
	#include <functional>
	#include <optional>
	#include <string>
	#include <utility>
	#include <vector>
#endif

enum class BosstiaryRarity_t : uint8_t {
	RARITY_BANE = 0,
	RARITY_ARCHFOE = 1,
	RARITY_NEMESIS = 2,

	// Only for server reading, not send to the client
	BOSS_INVALID = 10,
};

struct LevelInfo {
	uint32_t kills;
	uint16_t points;
};

class MonsterType;
class Player;

class IOBosstiary {
public:
	IOBosstiary() = default;

	// Non copyable
	IOBosstiary(const IOBosstiary &) = delete;
	void operator=(const IOBosstiary &) = delete;

	static IOBosstiary &getInstance();

	/// The boss of the day, through IServerConfigRepository: today's stored one, or a new one chosen with
	/// chooseBoostedBoss, stored and taken out of every player's slots. The day is currentServerSaveDay's (utils/tools.hpp). At boot and at
	/// every server save (Game::refreshBoostedBoss). Returns the race id of a newly drawn boss, after it was
	/// stored; nullopt when today's stays or none could be stored (the boss in memory is then unchanged).
	std::optional<uint16_t> loadBoostedBoss();


	struct BoostedBossChoice {
		BoostedCreatureRecord boss;
		bool isNew = false;
	};
	/// The stored boss when it was chosen `today`; else one of `candidates` (the archfoe bosses), picked with
	/// `pick(n)`, an index in [0, n). nullopt when nothing is stored, or a new one is needed and there is no
	/// candidate.
	static std::optional<BoostedBossChoice> chooseBoostedBoss(const std::optional<BoostedCreatureRecord> &stored, uint16_t today, const std::vector<std::pair<uint16_t, std::string>> &candidates, const std::function<size_t(size_t)> &pick);

	/// The player whose boss cooldown `key` is: the guid of "player.<guid>.boss.cooldown.<raceId>", the KV key of
	/// Player:setBossCooldown (and of the cooldown window); nullopt for any other key.
	static std::optional<uint32_t> bossCooldownOwner(std::string_view key, uint16_t raceId);

	void addBosstiaryMonster(uint16_t raceId, const std::string &name);
	const std::map<uint16_t, std::string> &getBosstiaryMap() const;

	const std::map<BosstiaryRarity_t, std::vector<LevelInfo>> levelInfos = {
		{ BosstiaryRarity_t::RARITY_BANE, { { 25, 5 }, { 100, 15 }, { 300, 30 } } },
		{ BosstiaryRarity_t::RARITY_ARCHFOE, { { 5, 10 }, { 20, 30 }, { 60, 60 } } },
		{ BosstiaryRarity_t::RARITY_NEMESIS, { { 1, 10 }, { 3, 30 }, { 5, 60 } } }
	};

	void setBossBoostedName(std::string_view name);
	std::string getBoostedBossName() const;
	void setBossBoostedId(uint16_t raceId);
	uint16_t getBoostedBossId() const;
	std::shared_ptr<MonsterType> getMonsterTypeByBossRaceId(uint16_t raceId) const;

	void addBosstiaryKill(const std::shared_ptr<Player> &player, const std::shared_ptr<MonsterType> &mtype, uint32_t amount = 1) const;
	uint16_t calculateLootBonus(uint32_t bossPoints) const;
	uint32_t calculateBossPoints(uint16_t lootBonus) const;
	std::vector<uint16_t> getBosstiaryFinished(const std::shared_ptr<Player> &player, uint8_t level = 1) const;
	uint8_t getBossCurrentLevel(const std::shared_ptr<Player> &player, uint16_t bossId) const;
	const std::vector<LevelInfo> &getBossRaceKillStages(BosstiaryRarity_t race) const;

private:
	std::map<uint16_t, std::string> bosstiaryMap;
	std::string boostedBoss;
	uint16_t boostedBossId = 0;
};

constexpr auto g_ioBosstiary = IOBosstiary::getInstance;
