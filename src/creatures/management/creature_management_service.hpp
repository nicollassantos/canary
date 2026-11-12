#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "game/movement/position.hpp"

class Game;
class Creature;
class Monster;
class Npc;
class Player;
class Account;
class IConfigManager;
enum ReturnValue : uint16_t;

class CreatureManagementService {
public:
	CreatureManagementService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	std::shared_ptr<Creature> getCreatureByID(uint32_t id);
	std::shared_ptr<Monster> getMonsterByID(uint32_t id);
	std::shared_ptr<Npc> getNpcByID(uint32_t id);
	std::shared_ptr<Player> getPlayerByID(uint32_t id, bool allowOffline = false);
	std::shared_ptr<Creature> getCreatureByName(const std::string &name);
	std::shared_ptr<Npc> getNpcByName(const std::string &name);
	std::shared_ptr<Player> getPlayerByName(const std::string &s, bool allowOffline = false, bool isNewName = false);
	std::shared_ptr<Player> getPlayerByGUID(const uint32_t &guid, bool allowOffline = false);
	std::string getPlayerNameByGUID(const uint32_t &guid);
	ReturnValue getPlayerByNameWildcard(const std::string &s, std::shared_ptr<Player> &player);
	std::vector<std::shared_ptr<Player>> getPlayersByAccount(const std::shared_ptr<Account> &acc, bool allowOffline = false);

	bool internalPlaceCreature(const std::shared_ptr<Creature> &creature, const Position &pos, bool extendedPos = false, bool forced = false, bool creatureCheck = false);
	bool placeCreature(const std::shared_ptr<Creature> &creature, const Position &pos, bool extendedPos = false, bool forced = false);
	bool removeCreature(const std::shared_ptr<Creature> &creature, bool isLogout = true);

private:
	Game &game_;
	IConfigManager &config_;
};
