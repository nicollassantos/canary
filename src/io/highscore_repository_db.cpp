/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/highscore_repository_db.hpp"

#include "creatures/players/vocations/vocation.hpp"
#include "database/database.hpp"
#include "database/databasetasks.hpp"
#include "enums/account_group_type.hpp"
#include "game/highscore/highscore_cache.hpp"
#include "lib/metrics/metrics.hpp"
#include "lib/di/container.hpp"

namespace {
	// Built queries only depend on their key, but the keys come from the client.
	constexpr std::chrono::minutes QueryCacheTtl { 10 };
}

IHighscoreRepository &IHighscoreRepository::getInstance() {
	return DI::port<IHighscoreRepository>();
}

bool HighscoreRepositoryDB::loadEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, OnLoaded onLoaded) {
	const auto query = generateHighscoreOrGetCachedQueryForEntries(categoryName, page, entriesPerPage, vocation);
	if (query.empty()) {
		return false;
	}
	run(query, std::move(onLoaded));
	return true;
}

bool HighscoreRepositoryDB::loadOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation, OnLoaded onLoaded) {
	const auto query = generateHighscoreOrGetCachedQueryForOurRank(categoryName, entriesPerPage, playerGUID, vocation);
	if (query.empty()) {
		return false;
	}
	run(query, std::move(onLoaded));
	return true;
}

void HighscoreRepositoryDB::run(const std::string &query, OnLoaded onLoaded) const {
	// The callback outlives this call; it captures the caller's callback by value only.
	g_databaseTasks().store(query, [onLoaded = std::move(onLoaded)](const DBResult_ptr &result, bool) {
		onLoaded(readHighscorePage(result));
	});
}

std::optional<HighscorePage> HighscoreRepositoryDB::readHighscorePage(const DBResult_ptr &result) {
	if (!result) {
		return std::nullopt;
	}

	HighscorePage page;
	page.page = result->getNumber<uint16_t>("page");
	page.total = result->getNumber<uint32_t>("total");
	page.characters.reserve(result->countResults());
	do {
		const auto &voc = g_vocations().getVocation(result->getNumber<uint16_t>("vocation"));
		uint8_t characterVocation = voc ? voc->getClientId() : 0;
		std::string loyaltyTitle; // todo get loyalty title from player
		page.characters.emplace_back(std::move(result->getString("name")), result->getNumber<uint64_t>("points"), result->getNumber<uint32_t>("id"), result->getNumber<uint32_t>("rank"), result->getNumber<uint16_t>("level"), characterVocation, loyaltyTitle);
	} while (result->next());
	return page;
}

std::string HighscoreRepositoryDB::generateHighscoreQuery(
	const std::string &categoryName,
	uint32_t page,
	uint8_t entriesPerPage,
	uint32_t vocation,
	uint32_t playerGUID /*= 0*/
) {
	if (categoryName.empty()) {
		g_logger().error("Category name cannot be empty.");
		return "";
	}
	if (entriesPerPage == 0) {
		g_logger().warn("[{}] entriesPerPage cannot be 0.", __FUNCTION__);
		return "";
	}

	uint32_t startPage = (page - 1) * static_cast<uint32_t>(entriesPerPage);
	uint32_t endPage = startPage + static_cast<uint32_t>(entriesPerPage);
	std::string entriesStr = std::to_string(entriesPerPage);

	const std::string vocationCondition = vocation != 0xFFFFFFFF ? generateVocationConditionHighscore(vocation) : "";
	const std::string countVocationCondition = vocation != 0xFFFFFFFF ? generateVocationConditionHighscore(vocation, " AND ") : "";

	std::string query = fmt::format(
		"SELECT `id`, `name`, `level`, `vocation`, `points`, `rank`, `rn` AS `entries`, "
		"(SELECT COUNT(*) FROM `players` WHERE `group_id` < {}{}) AS `total`, ",
		static_cast<int>(GROUP_TYPE_GAMEMASTER),
		countVocationCondition
	);

	if (playerGUID != 0) {
		query += fmt::format("(@ourRow DIV {0}) + 1 AS `page` FROM (", entriesStr);
	} else {
		query += fmt::format("{} AS `page` FROM (", page);
	}

	query += fmt::format(
		"SELECT `id`, `name`, `level`, `vocation`, `{}` AS `points`, "
		"@curRank := IF(@prevRank = `{}`, @curRank, IF(@prevRank := `{}`, @curRank + 1, @curRank + 1)) AS `rank`, "
		"(@row := @row + 1) AS `rn`",
		categoryName, categoryName, categoryName
	);

	if (playerGUID != 0) {
		query += fmt::format(", @ourRow := IF(`id` = {}, @row - 1, @ourRow) AS `rw`", playerGUID);
	}

	query += fmt::format(
		" FROM (SELECT `id`, `name`, `level`, `vocation`, `{}` FROM `players` `p`, "
		"(SELECT @curRank := 0, @prevRank := NULL, @row := 0, @ourRow := 0) `r` "
		"WHERE `group_id` < {} ORDER BY `{}` DESC) `t`",
		categoryName, static_cast<int>(GROUP_TYPE_GAMEMASTER), categoryName
	);

	if (vocation != 0xFFFFFFFF) {
		query += vocationCondition;
	}

	query += ") `T` WHERE ";

	if (playerGUID != 0) {
		query += fmt::format(
			"`rn` > ((@ourRow DIV {0}) * {0}) AND `rn` <= (((@ourRow DIV {0}) * {0}) + {0})",
			entriesStr
		);
	} else {
		query += fmt::format("`rn` > {} AND `rn` <= {}", startPage, endPage);
	}

	return query;
}

std::string HighscoreRepositoryDB::generateVocationConditionHighscore(uint32_t vocation, const std::string &conditionPrefix) {
	std::ostringstream queryPart;
	bool firstVocation = true;

	const auto vocationsMap = g_vocations().getVocations();
	for (const auto &it : vocationsMap) {
		const auto &voc = it.second;
		if (voc->getFromVocation() == vocation) {
			if (firstVocation) {
				queryPart << conditionPrefix << "(`vocation` = " << voc->getId();
				firstVocation = false;
			} else {
				queryPart << " OR `vocation` = " << voc->getId();
			}
		}
	}

	if (!firstVocation) {
		queryPart << ")";
	}

	return queryPart.str();
}

void HighscoreRepositoryDB::cacheQueryHighscore(const std::string &key, const std::string &query, uint32_t page, uint8_t entriesPerPage) {
	const auto now = std::chrono::steady_clock::now();
	if (const auto dropped = highscore_cache::prune(queryCache, now, QueryCacheTtl)) {
		g_metrics().addCounter("highscore_cache_evictions", static_cast<double>(dropped), { { "cache", "query" } });
	}
	QueryHighscoreCacheEntry queryEntry { query, page, entriesPerPage, now };
	queryCache[key] = queryEntry;
}

std::string HighscoreRepositoryDB::generateHighscoreOrGetCachedQueryForEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation) {
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "Entries_" << categoryName << "_" << page << "_" << static_cast<int>(entriesPerPage) << "_" << vocation;
	std::string cacheKey = cacheKeyStream.str();

	if (queryCache.find(cacheKey) != queryCache.end()) {
		const QueryHighscoreCacheEntry &cachedEntry = queryCache[cacheKey];
		if (cachedEntry.page == page) {
			return cachedEntry.query;
		}
	}

	std::string newQuery = generateHighscoreQuery(categoryName, page, entriesPerPage, vocation);
	if (!newQuery.empty()) {
		cacheQueryHighscore(cacheKey, newQuery, page, entriesPerPage);
	}

	return newQuery;
}

std::string HighscoreRepositoryDB::generateHighscoreOrGetCachedQueryForOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation) {
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "OurRank_" << categoryName << "_" << static_cast<int>(entriesPerPage) << "_" << playerGUID << "_" << vocation;
	std::string cacheKey = cacheKeyStream.str();

	if (queryCache.find(cacheKey) != queryCache.end()) {
		const QueryHighscoreCacheEntry &cachedEntry = queryCache[cacheKey];
		if (cachedEntry.page == entriesPerPage) {
			return cachedEntry.query;
		}
	}

	std::string newQuery = generateHighscoreQuery(categoryName, 0, entriesPerPage, vocation, playerGUID);
	if (!newQuery.empty()) {
		cacheQueryHighscore(cacheKey, newQuery, entriesPerPage, entriesPerPage);
	}

	return newQuery;
}
