#include "creatures/management/creature_management_service.hpp"
#include "account/account.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/grouping/team_finder.hpp"
#include "creatures/players/player.hpp"
#include "enums/account_errors.hpp"
#include "game/game.hpp"
#include "io/iologindata.hpp"
#include "lib/metrics/metrics.hpp"
#include "map/spectators.hpp"
#include "utils/tools.hpp"
#include "utils/wildcardtree.hpp"

std::shared_ptr<Creature> CreatureManagementService::getCreatureByID(uint32_t id) {
	if (id >= Player::getFirstID() && id <= Player::getLastID()) {
		return getPlayerByID(id);
	} else if (id <= Monster::monsterAutoID) {
		return getMonsterByID(id);
	} else if (id <= Npc::npcAutoID) {
		return getNpcByID(id);
	} else {
		g_logger().warn("Creature with id {} not exists");
	}
	return nullptr;
}

std::shared_ptr<Monster> CreatureManagementService::getMonsterByID(uint32_t id) {
	if (id == 0) {
		return nullptr;
	}

	auto it = game_.monstersIdIndex.find(id);
	if (it == game_.monstersIdIndex.end()) {
		return nullptr;
	}

	if (it->second >= game_.monsters.size()) {
		return nullptr;
	}

	return game_.monsters[it->second];
}

std::shared_ptr<Npc> CreatureManagementService::getNpcByID(uint32_t id) {
	if (id == 0) {
		return nullptr;
	}

	auto it = game_.npcsIdIndex.find(id);
	if (it == game_.npcsIdIndex.end()) {
		return nullptr;
	}

	return game_.npcs[it->second];
}

std::shared_ptr<Player> CreatureManagementService::getPlayerByID(uint32_t id, bool allowOffline) {
	auto playerMap = game_.players.find(id);
	if (playerMap != game_.players.end()) {
		return playerMap->second;
	}

	if (!allowOffline) {
		return nullptr;
	}
	std::shared_ptr<Player> tmpPlayer = std::make_shared<Player>(nullptr);
	if (!IOLoginData::loadPlayerById(tmpPlayer, id)) {
		return nullptr;
	}
	tmpPlayer->setOnline(false);
	return tmpPlayer;
}

std::shared_ptr<Creature> CreatureManagementService::getCreatureByName(const std::string &creatureName) {
	if (creatureName.empty()) {
		return nullptr;
	}

	const std::string &lowerCaseName = asLowerCaseString(creatureName);

	auto m_it = game_.mappedPlayerNames.find(lowerCaseName);
	if (m_it != game_.mappedPlayerNames.end()) {
		return m_it->second.lock();
	}

	auto npcIterator = game_.npcsNameIndex.find(lowerCaseName);
	if (npcIterator != game_.npcsNameIndex.end()) {
		return game_.npcs[npcIterator->second];
	}

	auto monsterIterator = game_.monstersNameIndex.find(lowerCaseName);
	if (monsterIterator != game_.monstersNameIndex.end()) {
		return game_.monsters[monsterIterator->second];
	}
	return nullptr;
}

std::shared_ptr<Npc> CreatureManagementService::getNpcByName(const std::string &npcName) {
	if (npcName.empty()) {
		return nullptr;
	}

	const std::string lowerCaseName = asLowerCaseString(npcName);
	auto it = game_.npcsNameIndex.find(lowerCaseName);
	if (it != game_.npcsNameIndex.end()) {
		return game_.npcs[it->second];
	}

	return nullptr;
}

std::shared_ptr<Player> CreatureManagementService::getPlayerByName(const std::string &s, bool allowOffline, bool isNewName) {
	if (s.empty()) {
		return nullptr;
	}

	auto it = game_.mappedPlayerNames.find(asLowerCaseString(s));
	if (it == game_.mappedPlayerNames.end() || it->second.expired()) {
		if (!allowOffline) {
			return nullptr;
		}
		std::shared_ptr<Player> tmpPlayer = std::make_shared<Player>(nullptr);
		if (!IOLoginData::loadPlayerByName(tmpPlayer, s)) {
			if (!isNewName) {
				g_logger().error("Failed to load player {} from database", s);
			} else {
				g_logger().info("New name {} is available", s);
			}
			return nullptr;
		}
		tmpPlayer->setOnline(false);
		return tmpPlayer;
	}
	return it->second.lock();
}

std::shared_ptr<Player> CreatureManagementService::getPlayerByGUID(const uint32_t &guid, bool allowOffline) {
	if (guid == 0) {
		return nullptr;
	}
	for (const auto &it : game_.players) {
		if (guid == it.second->getGUID()) {
			return it.second;
		}
	}
	if (!allowOffline) {
		return nullptr;
	}
	std::shared_ptr<Player> tmpPlayer = std::make_shared<Player>(nullptr);
	if (!IOLoginData::loadPlayerById(tmpPlayer, guid)) {
		return nullptr;
	}
	tmpPlayer->setOnline(false);
	return tmpPlayer;
}

std::string CreatureManagementService::getPlayerNameByGUID(const uint32_t &guid) {
	if (guid == 0) {
		return "";
	}
	if (game_.m_playerNameCache.contains(guid)) {
		return game_.m_playerNameCache.at(guid);
	}
	const auto &player = getPlayerByGUID(guid, true);
	auto name = player ? player->getName() : "";
	if (!name.empty()) {
		game_.m_playerNameCache[guid] = name;
	}
	return name;
}

ReturnValue CreatureManagementService::getPlayerByNameWildcard(const std::string &s, std::shared_ptr<Player> &player) {
	size_t strlen = s.length();
	if (strlen == 0 || strlen > 29) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	if (s.back() == '~') {
		const std::string &query = asLowerCaseString(s.substr(0, strlen - 1));
		std::string result;
		ReturnValue ret = game_.wildcardTree->findOne(query, result);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}

		player = getPlayerByName(result);
	} else {
		player = getPlayerByName(s);
	}

	if (!player) {
		return RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE;
	}

	return RETURNVALUE_NOERROR;
}

std::vector<std::shared_ptr<Player>> CreatureManagementService::getPlayersByAccount(const std::shared_ptr<Account> &acc, bool allowOffline) {
	auto [accountPlayers, error] = acc->getAccountPlayers();
	if (error != AccountErrors_t::Ok) {
		return {};
	}
	std::vector<std::shared_ptr<Player>> ret;
	for (const auto &[name, _] : accountPlayers) {
		const auto &player = getPlayerByName(name, allowOffline);
		if (player) {
			ret.push_back(player);
		}
	}
	return ret;
}

bool CreatureManagementService::internalPlaceCreature(const std::shared_ptr<Creature> &creature, const Position &pos, bool extendedPos, bool forced, bool creatureCheck) {
	if (creature->getParent() != nullptr) {
		return false;
	}
	const auto &tile = game_.map.getTile(pos);
	if (!tile) {
		return false;
	}
	auto toZones = tile->getZones();
	if (auto ret = game_.beforeCreatureZoneChange(creature, {}, toZones); ret != RETURNVALUE_NOERROR) {
		return false;
	}

	if (!game_.map.placeCreature(pos, creature, extendedPos, forced)) {
		return false;
	}

	creature->setID();
	creature->addList();
	creature->updateCalculatedStepSpeed();

	if (creatureCheck) {
		game_.addCreatureCheck(creature);
		creature->onPlacedCreature();
	}
	game_.afterCreatureZoneChange(creature, {}, toZones);
	return true;
}

bool CreatureManagementService::placeCreature(const std::shared_ptr<Creature> &creature, const Position &pos, bool extendedPos, bool forced) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (!internalPlaceCreature(creature, pos, extendedPos, forced)) {
		return false;
	}

	bool hasPlayerSpectators = false;
	for (const auto &spectator : Spectators().find<Creature>(creature->getPosition(), true)) {
		if (const auto &tmpPlayer = spectator->getPlayer()) {
			tmpPlayer->sendCreatureAppear(creature, creature->getPosition(), true);
			hasPlayerSpectators = true;
		}
		spectator->onCreatureAppear(creature, true);
	}

	if (hasPlayerSpectators) {
		game_.addCreatureCheck(creature);
	}

	auto parent = creature->getParent();
	if (parent) {
		parent->postAddNotification(creature, nullptr, 0);
	}
	creature->onPlacedCreature();
	return true;
}

bool CreatureManagementService::removeCreature(const std::shared_ptr<Creature> &creature, bool isLogout) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	if (!creature || creature->isRemoved()) {
		return false;
	}

	std::shared_ptr<Tile> tile = creature->getTile();
	if (!tile) {
		g_logger().error("[{}] tile on position '{}' for creature '{}' not exist", __FUNCTION__, creature->getPosition().toString(), creature->getName());
	}
	auto fromZones = creature->getZones();

	if (tile) {
		std::vector<int32_t> oldStackPosVector;
		auto spectators = Spectators().find<Creature>(tile->getPosition(), true);
		auto playersSpectators = spectators.filter<Player>();

		for (const auto &spectator : playersSpectators) {
			if (const auto &player = spectator->getPlayer()) {
				oldStackPosVector.push_back(player->canSeeCreature(creature) ? tile->getClientIndexOfCreature(player, creature) : -1);
			}
		}

		tile->removeCreature(creature);

		const Position &tilePosition = tile->getPosition();

		// Send to client
		size_t i = 0;
		for (const auto &spectator : playersSpectators) {
			if (const auto &player = spectator->getPlayer()) {
				player->sendRemoveTileThing(tilePosition, oldStackPosVector[i++]);
			}
		}

		// event method
		for (const auto &spectator : spectators) {
			spectator->onRemoveCreature(creature, isLogout);
		}
	}

	if (creature->getMaster() && !creature->getMaster()->isRemoved()) {
		creature->setMaster(nullptr);
	}

	creature->getParent()->postRemoveNotification(creature, nullptr, 0);
	game_.afterCreatureZoneChange(creature, fromZones, {});

	creature->removeList();
	creature->setRemoved();

	Game::removeCreatureCheck(creature);

	for (const auto &summon : creature->getSummons()) {
		summon->setSkillLoss(false);
		removeCreature(summon);
	}

	if (creature->getPlayer() && isLogout) {
		auto it = game_.teamFinderMap.find(creature->getPlayer()->getGUID());
		if (it != game_.teamFinderMap.end()) {
			game_.teamFinderMap.erase(it);
		}
	}

	return true;
}
