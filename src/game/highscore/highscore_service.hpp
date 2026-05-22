#pragma once
#include <cstdint>
#include <memory>
#include <string>

class Game;
class Player;
struct DBResult;
using DBResult_ptr = std::shared_ptr<DBResult>;

enum HighscoreType_t : uint8_t;

class HighscoreService {
public:
	HighscoreService(Game &game) :
		game_(game) { }

	void playerHighscores(const std::shared_ptr<Player> &player, HighscoreType_t type, uint8_t category, uint32_t vocation, const std::string &worldName, uint16_t page, uint8_t entriesPerPage);

	[[nodiscard]] static uint16_t calculateHighscorePages(uint32_t totalEntries, uint8_t entriesPerPage);
	static std::string getSkillNameById(uint8_t &skill);

	std::string generateHighscoreQuery(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, uint32_t playerGUID = 0);
	std::string generateVocationConditionHighscore(uint32_t vocation, const std::string &conditionPrefix = " WHERE ");
	void processHighscoreResults(const DBResult_ptr &result, uint32_t playerID, uint8_t category, uint32_t vocationCID, uint8_t entriesPerPage);
	void cacheQueryHighscore(const std::string &key, const std::string &query, uint32_t page, uint8_t entriesPerPage);
	std::string generateHighscoreOrGetCachedQueryForEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation);
	std::string generateHighscoreOrGetCachedQueryForOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation);

private:
	Game &game_;
};
