#pragma once

#include "io/highscore_repository.hpp"

#include <cstdint>
#include <memory>
#include <string>

class Game;
class Player;

enum HighscoreType_t : uint8_t;

class HighscoreService {
public:
	void playerHighscores(const std::shared_ptr<Player> &player, HighscoreType_t type, uint8_t category, uint32_t vocation, const std::string &worldName, uint16_t page, uint8_t entriesPerPage);

	[[nodiscard]] static uint16_t calculateHighscorePages(uint32_t totalEntries, uint8_t entriesPerPage);
	static std::string getSkillNameById(uint8_t &skill);

	/// Handles a loaded page: resets the async task, then sends cached or fresh rows. Static, so the
	/// deferred callback never holds a pointer to the service.
	static void processHighscoreResults(Game &game, std::optional<HighscorePage> result, uint32_t playerID, uint8_t category, uint32_t vocationCID, uint8_t entriesPerPage);
};
