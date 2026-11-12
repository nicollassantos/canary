#pragma once

#include "io/highscore_repository.hpp"
#include "utils/request_cooldown.hpp"
#include "utils/tools.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class Game;
struct HighscoreCacheEntry;
class Player;

enum HighscoreType_t : uint8_t;

/// Highscore requests. Every query computes a full ranking on the single shared database
/// connection, so requests are throttled: fresh cached pages are answered without a query, each
/// player waits RequestCooldownMs between requests, and at most MaxQueriesInFlight queries wait
/// for the database at once.
class HighscoreService {
public:
	static constexpr uint32_t MaxQueriesInFlight = 8;
	static constexpr int64_t RequestCooldownMs = 1000;

	explicit HighscoreService(Game &game) :
		game_(game) { }

	void playerHighscores(const std::shared_ptr<Player> &player, HighscoreType_t type, uint8_t category, uint32_t vocation, const std::string &worldName, uint16_t page, uint8_t entriesPerPage);

	[[nodiscard]] static uint16_t calculateHighscorePages(uint32_t totalEntries, uint8_t entriesPerPage);
	static std::string getSkillNameById(uint8_t &skill);

	/// Handles a loaded page: resets the async task, then sends cached or fresh rows. Static, so the
	/// deferred callback never holds a pointer to the service.
	static void processHighscoreResults(Game &game, std::optional<HighscorePage> result, uint32_t playerID, uint8_t category, uint32_t vocationCID, uint8_t entriesPerPage);

	[[nodiscard]] static uint32_t queriesInFlight() {
		return inFlight;
	}
	/// Tests only: forget queries whose rows a test never delivered.
	static void resetQueriesInFlight() {
		inFlight = 0;
	}

	/// Milliseconds clock for the per-player cooldown; tests replace it.
	std::function<int64_t()> now = [] { return OTSYS_TIME(); };

private:
	[[nodiscard]] static std::string cacheKey(uint8_t category, uint32_t vocationId, uint8_t entriesPerPage, uint32_t page);
	static void sendCached(const std::shared_ptr<Player> &player, const HighscoreCacheEntry &entry, uint8_t category, uint32_t vocationCID);

	Game &game_;
	RequestCooldown requestCooldown { RequestCooldownMs };
	// Queries handed to the repository whose rows have not arrived yet (dispatcher thread only).
	inline static uint32_t inFlight = 0;
};
