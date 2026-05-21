/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/combat/combat.hpp"
#include <sstream>

class Game;
class IConfigManager;
class Monster;
struct TextMessage;

class CombatService {
public:
	CombatService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	bool combatBlockHit(CombatDamage &damage, const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, bool checkDefense, bool checkArmor, bool field, bool condition = false);
	void combatGetTypeInfo(CombatType_t combatType, const std::shared_ptr<Creature> &target, TextColor_t &color, uint16_t &effect);
	bool combatChangeHealth(const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, CombatDamage &damage, bool isEvent = false);
	bool combatChangeMana(const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, CombatDamage &damage);
	void applyCharmRune(const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Creature> &target, const int32_t &realDamage) const;
	void applyManaLeech(const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Creature> &target, const CombatDamage &damage, const int32_t &realDamage) const;
	void applyLifeLeech(const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Creature> &target, const CombatDamage &damage, const int32_t &realDamage) const;
	void handleHazardSystemAttack(CombatDamage &damage, const std::shared_ptr<Player> &player, const std::shared_ptr<Monster> &monster, bool isPlayerAttacker);
	void notifySpectators(const CreatureVector &spectators, const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster);
	void applyPvPDamage(CombatDamage &damage, const std::shared_ptr<Player> &attacker, const std::shared_ptr<Player> &target);

private:
	void sendBlockEffect(BlockType_t blockType, CombatType_t combatType, const Position &targetPos, const std::shared_ptr<Creature> &source);
	float pvpLevelDifferenceDamageMultiplier(const std::shared_ptr<Player> &attacker, const std::shared_ptr<Player> &target);
	void applyWheelOfDestinyHealing(CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer, std::shared_ptr<Creature> target);
	void applyWheelOfDestinyEffectsToDamage(CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Creature> &target) const;
	int32_t applyHealthChange(const CombatDamage &damage, const std::shared_ptr<Creature> &target) const;
	int32_t calculateLeechAmount(const int32_t &realDamage, const uint16_t &skillAmount, int targetsAffected) const;
	void updatePlayerPartyHuntAnalyzer(const CombatDamage &damage, const std::shared_ptr<Player> &player) const;
	void sendDamageMessageAndEffects(
		const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
		const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Player> &targetPlayer,
		TextMessage &message, const CreatureVector &spectators, int32_t realDamage
	);
	void sendEffects(
		const std::shared_ptr<Creature> &target, const CombatDamage &damage, const Position &targetPos,
		TextMessage &message, const CreatureVector &spectators
	);
	void sendMessages(
		const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
		const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Player> &targetPlayer,
		TextMessage &message, const CreatureVector &spectators, int32_t realDamage
	) const;
	bool shouldSendMessage(const TextMessage &message) const;
	void buildMessageAsAttacker(
		const std::shared_ptr<Creature> &target, const CombatDamage &damage, TextMessage &message,
		std::stringstream &ss, const std::string &damageString, bool amplified = false, const std::shared_ptr<Player> &attackerPlayer = nullptr
	) const;
	void buildMessageAsTarget(
		const std::shared_ptr<Creature> &attacker, const CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer,
		const std::shared_ptr<Player> &targetPlayer, TextMessage &message, std::stringstream &ss,
		const std::string &damageString
	) const;
	void buildMessageAsSpectator(
		const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
		const std::shared_ptr<Player> &targetPlayer, TextMessage &message, std::stringstream &ss,
		const std::string &damageString, std::string &spectatorMessage
	) const;

	Game &game_;
	IConfigManager &config_;
};
