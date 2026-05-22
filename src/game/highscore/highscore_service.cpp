#include "game/highscore/highscore_service.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "database/databasetasks.hpp"
#include "enums/account_group_type.hpp"
#include "game/game.hpp"
#include "game/game_definitions.hpp"
#include "server/server_definitions.hpp"
#include "utils/tools.hpp"

std::string HighscoreService::generateHighscoreQuery(
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

std::string HighscoreService::generateVocationConditionHighscore(uint32_t vocation, const std::string &conditionPrefix) {
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

void HighscoreService::processHighscoreResults(const DBResult_ptr &result, uint32_t playerID, uint8_t category, uint32_t vocationCID, uint8_t entriesPerPage) {
	const auto &player = game_.getPlayerByID(playerID);
	if (!player) {
		return;
	}

	player->resetAsyncOngoingTask(PlayerAsyncTask_Highscore);

	if (!result) {
		player->sendHighscoresNoData();
		return;
	}

	auto page = result->getNumber<uint16_t>("page");
	auto pages = calculateHighscorePages(result->getNumber<uint32_t>("total"), entriesPerPage);

	uint32_t vocationId = getVocationIdFromClientId(vocationCID);
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "Highscore_" << static_cast<int>(category) << "_" << static_cast<int>(vocationId) << "_" << static_cast<int>(entriesPerPage) << "_" << page;
	std::string cacheKey = cacheKeyStream.str();

	auto it = game_.highscoreCache.find(cacheKey);
	auto now = std::chrono::system_clock::now();
	if (it != game_.highscoreCache.end() && (now - it->second.timestamp < HIGHSCORE_CACHE_EXPIRATION_TIME)) {
		auto &cacheEntry = it->second;
		auto cachedTime = it->second.timestamp;
		auto durationSinceEpoch = cachedTime.time_since_epoch();
		auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch).count();
		auto updateTimer = static_cast<uint32_t>(secondsSinceEpoch);
		player->sendHighscores(cacheEntry.characters, category, vocationCID, cacheEntry.page, static_cast<uint16_t>(cacheEntry.entriesPerPage), updateTimer);
	} else {
		std::vector<HighscoreCharacter> characters;
		characters.reserve(result->countResults());
		if (result) {
			do {
				const auto &voc = g_vocations().getVocation(result->getNumber<uint16_t>("vocation"));
				uint8_t characterVocation = voc ? voc->getClientId() : 0;
				std::string loyaltyTitle; // todo get loyalty title from player
				characters.emplace_back(std::move(result->getString("name")), result->getNumber<uint64_t>("points"), result->getNumber<uint32_t>("id"), result->getNumber<uint32_t>("rank"), result->getNumber<uint16_t>("level"), characterVocation, loyaltyTitle);
			} while (result->next());
		}

		player->sendHighscores(characters, category, vocationCID, page, static_cast<uint16_t>(pages), getTimeNow());
		game_.highscoreCache[cacheKey] = { characters, page, pages, now };
	}
}

[[nodiscard]] uint16_t HighscoreService::calculateHighscorePages(uint32_t totalEntries, uint8_t entriesPerPage) {
	if (entriesPerPage == 0) {
		return 0;
	}

	return static_cast<uint16_t>((totalEntries + entriesPerPage - 1) / entriesPerPage);
}

void HighscoreService::cacheQueryHighscore(const std::string &key, const std::string &query, uint32_t page, uint8_t entriesPerPage) {
	QueryHighscoreCacheEntry queryEntry { query, page, entriesPerPage, std::chrono::steady_clock::now() };
	game_.queryCache[key] = queryEntry;
}

std::string HighscoreService::generateHighscoreOrGetCachedQueryForEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation) {
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "Entries_" << categoryName << "_" << page << "_" << static_cast<int>(entriesPerPage) << "_" << vocation;
	std::string cacheKey = cacheKeyStream.str();

	if (game_.queryCache.find(cacheKey) != game_.queryCache.end()) {
		const QueryHighscoreCacheEntry &cachedEntry = game_.queryCache[cacheKey];
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

std::string HighscoreService::generateHighscoreOrGetCachedQueryForOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation) {
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "OurRank_" << categoryName << "_" << static_cast<int>(entriesPerPage) << "_" << playerGUID << "_" << vocation;
	std::string cacheKey = cacheKeyStream.str();

	if (game_.queryCache.find(cacheKey) != game_.queryCache.end()) {
		const QueryHighscoreCacheEntry &cachedEntry = game_.queryCache[cacheKey];
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

void HighscoreService::playerHighscores(const std::shared_ptr<Player> &player, HighscoreType_t type, uint8_t category, uint32_t vocation, const std::string &, uint16_t page, uint8_t entriesPerPage) {
	if (player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore)) {
		return;
	}
	if (entriesPerPage == 0) {
		player->sendHighscoresNoData();
		return;
	}

	std::string categoryName = getSkillNameById(category);

	std::string query;

	uint32_t vocationId = getVocationIdFromClientId(vocation);
	if (type == HIGHSCORE_GETENTRIES) {
		query = generateHighscoreOrGetCachedQueryForEntries(categoryName, page, entriesPerPage, vocationId);
	} else if (type == HIGHSCORE_OURRANK) {
		query = generateHighscoreOrGetCachedQueryForOurRank(categoryName, entriesPerPage, player->getGUID(), vocationId);
	}
	if (query.empty()) {
		player->sendHighscoresNoData();
		return;
	}

	uint32_t playerID = player->getID();
	std::function<void(DBResult_ptr, bool)> callback = [this, playerID, category, vocation, entriesPerPage](const DBResult_ptr &result, bool) {
		processHighscoreResults(result, playerID, category, vocation, entriesPerPage);
	};

	g_databaseTasks().store(query, callback);
	player->addAsyncOngoingTask(PlayerAsyncTask_Highscore);
}

std::string HighscoreService::getSkillNameById(uint8_t &skill) {
	switch (static_cast<HighscoreCategories_t>(skill)) {
		case HighscoreCategories_t::FIST_FIGHTING:
			return "skill_fist";
		case HighscoreCategories_t::CLUB_FIGHTING:
			return "skill_club";
		case HighscoreCategories_t::SWORD_FIGHTING:
			return "skill_sword";
		case HighscoreCategories_t::AXE_FIGHTING:
			return "skill_axe";
		case HighscoreCategories_t::DISTANCE_FIGHTING:
			return "skill_dist";
		case HighscoreCategories_t::SHIELDING:
			return "skill_shielding";
		case HighscoreCategories_t::FISHING:
			return "skill_fishing";
		case HighscoreCategories_t::MAGIC_LEVEL:
			return "maglevel";
		case HighscoreCategories_t::BOSS_POINTS:
			return "boss_points";
		default:
			skill = static_cast<uint8_t>(HighscoreCategories_t::EXPERIENCE);
			return "experience";
	}
}
