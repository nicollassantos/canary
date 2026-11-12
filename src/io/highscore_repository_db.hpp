/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "io/highscore_repository.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <chrono>
	#include <memory>
	#include <parallel_hashmap/phmap.h>
#endif

class DBResult;
using DBResult_ptr = std::shared_ptr<DBResult>;

struct QueryHighscoreCacheEntry {
	std::string query;
	uint32_t page;
	uint8_t entriesPerPage;
	std::chrono::time_point<std::chrono::steady_clock> timestamp;
};

/// IHighscoreRepository adapter. Owns the highscore SQL (moved from Game/HighscoreService with the
/// upstream names and signatures, so tools/merge/moved_merge.py still matches them) and the query
/// cache. Queries run through the shared DatabaseTasks pool.
class HighscoreRepositoryDB final : public IHighscoreRepository {
public:
	[[nodiscard]] bool loadEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, OnLoaded onLoaded) override;
	[[nodiscard]] bool loadOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation, OnLoaded onLoaded) override;

	/// Turns the query rows into a page; std::nullopt when the query returned nothing.
	[[nodiscard]] static std::optional<HighscorePage> readHighscorePage(const DBResult_ptr &result);

	std::string generateHighscoreQuery(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, uint32_t playerGUID = 0);
	std::string generateVocationConditionHighscore(uint32_t vocation, const std::string &conditionPrefix = " WHERE ");
	void cacheQueryHighscore(const std::string &key, const std::string &query, uint32_t page, uint8_t entriesPerPage);
	std::string generateHighscoreOrGetCachedQueryForEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation);
	std::string generateHighscoreOrGetCachedQueryForOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation);

private:
	void run(const std::string &query, OnLoaded onLoaded) const;

	phmap::flat_hash_map<std::string, QueryHighscoreCacheEntry> queryCache;
};
