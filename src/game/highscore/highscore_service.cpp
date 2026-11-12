#include "game/highscore/highscore_service.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "game/game.hpp"
#include "game/game_definitions.hpp"
#include "game/highscore/highscore_cache.hpp"
#include "io/highscore_repository.hpp"
#include "lib/metrics/metrics.hpp"
#include "server/server_definitions.hpp"
#include "utils/tools.hpp"

std::string HighscoreService::cacheKey(uint8_t category, uint32_t vocationId, uint8_t entriesPerPage, uint32_t page) {
	std::ostringstream cacheKeyStream;
	cacheKeyStream << "Highscore_" << static_cast<int>(category) << "_" << static_cast<int>(vocationId) << "_" << static_cast<int>(entriesPerPage) << "_" << page;
	return cacheKeyStream.str();
}

void HighscoreService::sendCached(const std::shared_ptr<Player> &player, const HighscoreCacheEntry &cacheEntry, uint8_t category, uint32_t vocationCID) {
	auto cachedTime = cacheEntry.timestamp;
	auto durationSinceEpoch = cachedTime.time_since_epoch();
	auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch).count();
	auto updateTimer = static_cast<uint32_t>(secondsSinceEpoch);
	player->sendHighscores(cacheEntry.characters, category, vocationCID, cacheEntry.page, static_cast<uint16_t>(cacheEntry.entriesPerPage), updateTimer);
}

void HighscoreService::processHighscoreResults(Game &game, std::optional<HighscorePage> result, uint32_t playerID, uint8_t category, uint32_t vocationCID, uint8_t entriesPerPage) {
	// Release the slot first: the player may have logged out while the query ran.
	if (inFlight > 0) {
		--inFlight;
	}

	const auto &player = game.getPlayerByID(playerID);
	if (!player) {
		return;
	}

	player->resetAsyncOngoingTask(PlayerAsyncTask_Highscore);

	if (!result) {
		player->sendHighscoresNoData();
		return;
	}

	auto page = result->page;
	auto pages = calculateHighscorePages(result->total, entriesPerPage);

	uint32_t vocationId = getVocationIdFromClientId(vocationCID);
	std::string cacheKey = HighscoreService::cacheKey(category, vocationId, entriesPerPage, page);

	auto it = game.highscoreCache.find(cacheKey);
	auto now = std::chrono::system_clock::now();
	if (it != game.highscoreCache.end() && (now - it->second.timestamp < HIGHSCORE_CACHE_EXPIRATION_TIME)) {
		sendCached(player, it->second, category, vocationCID);
	} else {
		player->sendHighscores(result->characters, category, vocationCID, page, static_cast<uint16_t>(pages), getTimeNow());
		if (const auto dropped = highscore_cache::prune(game.highscoreCache, now, HIGHSCORE_CACHE_EXPIRATION_TIME)) {
			g_metrics().addCounter("highscore_cache_evictions", static_cast<double>(dropped), { { "cache", "result" } });
		}
		game.highscoreCache[cacheKey] = { std::move(result->characters), page, pages, now };
	}
}

[[nodiscard]] uint16_t HighscoreService::calculateHighscorePages(uint32_t totalEntries, uint8_t entriesPerPage) {
	if (entriesPerPage == 0) {
		return 0;
	}

	return static_cast<uint16_t>((totalEntries + entriesPerPage - 1) / entriesPerPage);
}

void HighscoreService::playerHighscores(const std::shared_ptr<Player> &player, HighscoreType_t type, uint8_t category, uint32_t vocation, const std::string &, uint16_t page, uint8_t entriesPerPage) {
	if (player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore)) {
		return;
	}
	g_metrics().addCounter("highscore_requests_total", 1, { { "type", type == HIGHSCORE_OURRANK ? "our_rank" : "entries" } });

	// Page 0 only comes from a crafted packet: (page - 1) would wrap and still run the full ranking.
	if (entriesPerPage == 0 || (type == HIGHSCORE_GETENTRIES && page == 0)) {
		g_metrics().addCounter("highscore_requests_rejected", 1, { { "reason", entriesPerPage == 0 ? "zero_entries" : "page_zero" } });
		player->sendHighscoresNoData();
		return;
	}

	std::string categoryName = getSkillNameById(category);

	uint32_t vocationId = getVocationIdFromClientId(vocation);

	// A fresh cached page costs no query, so it is served even inside the cooldown.
	if (type == HIGHSCORE_GETENTRIES) {
		const auto cached = game_.highscoreCache.find(cacheKey(category, vocationId, entriesPerPage, page));
		if (cached != game_.highscoreCache.end() && std::chrono::system_clock::now() - cached->second.timestamp < HIGHSCORE_CACHE_EXPIRATION_TIME) {
			g_metrics().addCounter("highscore_served_from_cache", 1);
			sendCached(player, cached->second, category, vocation);
			return;
		}
	}

	if (!requestCooldown.tryTake(player->getGUID(), now())) {
		g_metrics().addCounter("highscore_requests_rejected", 1, { { "reason", "cooldown" } });
		player->sendHighscoresNoData();
		return;
	}
	if (inFlight >= MaxQueriesInFlight) {
		g_metrics().addCounter("highscore_requests_rejected", 1, { { "reason", "server_busy" } });
		player->sendHighscoresNoData();
		return;
	}

	uint32_t playerID = player->getID();
	// The rows arrive later on the dispatcher: capture values only and resolve the game then.
	auto onLoaded = [playerID, category, vocation, entriesPerPage](std::optional<HighscorePage> result) {
		processHighscoreResults(g_game(), std::move(result), playerID, category, vocation, entriesPerPage);
	};

	bool loading = false;
	if (type == HIGHSCORE_GETENTRIES) {
		loading = g_highscoreRepository().loadEntries(categoryName, page, entriesPerPage, vocationId, std::move(onLoaded));
	} else if (type == HIGHSCORE_OURRANK) {
		loading = g_highscoreRepository().loadOurRank(categoryName, entriesPerPage, player->getGUID(), vocationId, std::move(onLoaded));
	}
	if (!loading) {
		player->sendHighscoresNoData();
		return;
	}

	++inFlight;
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
