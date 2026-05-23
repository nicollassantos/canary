#pragma once

#include <cstdint>
#include <memory>

struct CombatDamage;
class Player;
class Creature;
class Monster;
class Item;
class Condition;
class MonsterType;
enum BlockType_t : uint8_t;
enum CombatType_t : uint8_t;
enum ConditionType_t : uint8_t;

class PlayerCombatEventComponent {
public:
	PlayerCombatEventComponent() = delete;
	explicit PlayerCombatEventComponent(Player &player) :
		m_player(player) { }

	void onBlockHit();
	void onTakeDamage(const std::shared_ptr<Creature> &attacker, int32_t damage);
	void onAttackedCreatureBlockHit(const BlockType_t &blockType);
	bool hasShield() const;
	bool isPzLocked() const;
	BlockType_t blockHit(const std::shared_ptr<Creature> &attacker, const CombatType_t &combatType, int32_t &damage, bool checkDefense, bool checkArmor, bool field);
	void doAttacking(uint32_t interval);
	std::shared_ptr<Item> getCorpse(const std::shared_ptr<Creature> &lastHitCreature, const std::shared_ptr<Creature> &mostDamageCreature);

	void onAddCondition(ConditionType_t type);
	void onCleanseCondition(ConditionType_t type) const;
	void onAddCombatCondition(ConditionType_t type);
	void onEndCondition(ConditionType_t type);
	void onCombatRemoveCondition(const std::shared_ptr<Condition> &condition);
	void onAttackedCreature(const std::shared_ptr<Creature> &target);
	void onAttacked();
	void onIdleStatus();
	void onPlacedCreature();
	void onAttackedCreatureDrainHealth(const std::shared_ptr<Creature> &target, int32_t points);
	void onTargetCreatureGainHealth(const std::shared_ptr<Creature> &target, int32_t points);
	bool onKilledPlayer(const std::shared_ptr<Player> &target, bool lastHit);
	void addHuntingTaskKill(const std::shared_ptr<MonsterType> &mType);
	void addBestiaryKill(const std::shared_ptr<MonsterType> &mType);
	void addBosstiaryKill(const std::shared_ptr<MonsterType> &mType);
	bool onKilledMonster(const std::shared_ptr<Monster> &monster);
	void gainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target);
	void onGainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target);
	void onGainSharedExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target);

	void parseAttackRecvHazardSystem(CombatDamage &damage, const std::shared_ptr<Monster> &monster);
	void parseAttackDealtHazardSystem(CombatDamage &damage, const std::shared_ptr<Monster> &monster) const;
	void setHazardSystemPoints(int32_t count);
	uint16_t getHazardSystemPoints() const;

private:
	Player &m_player;
};
