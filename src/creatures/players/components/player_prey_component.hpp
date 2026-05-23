/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

class Player;
class PreySlot;
class TaskHuntingSlot;
class MonsterType;
enum PreySlot_t : uint8_t;

class PlayerPreyComponent {
public:
	PlayerPreyComponent() = delete;
	explicit PlayerPreyComponent(Player &player) :
		m_player(player) { }

	// Prey
	void initializePrey();
	void removePreySlotById(PreySlot_t slotid);
	void sendPreyData() const;
	void sendPreyTimeLeft(const std::unique_ptr<PreySlot> &slot) const;
	void reloadPreySlot(PreySlot_t slotid);
	const std::unique_ptr<PreySlot> &getPreySlotById(PreySlot_t slotid);
	bool setPreySlotClass(std::unique_ptr<PreySlot> &slot);
	bool usePreyCards(uint16_t amount);
	void addPreyCards(uint64_t amount);
	uint64_t getPreyCards() const;
	uint32_t getPreyRerollPrice() const;
	std::vector<uint16_t> getPreyBlackList() const;
	const std::unique_ptr<PreySlot> &getPreyWithMonster(uint16_t raceId) const;

	// Task hunting
	void initializeTaskHunting();
	bool isCreatureUnlockedOnTaskHunting(const std::shared_ptr<MonsterType> &mtype) const;
	bool setTaskHuntingSlotClass(std::unique_ptr<TaskHuntingSlot> &slot);
	void reloadTaskSlot(PreySlot_t slotid);
	const std::unique_ptr<TaskHuntingSlot> &getTaskHuntingSlotById(PreySlot_t slotid);
	std::vector<uint16_t> getTaskHuntingBlackList() const;
	void sendTaskHuntingData() const;
	void addTaskHuntingPoints(uint64_t amount);
	bool useTaskHuntingPoints(uint64_t amount);
	uint64_t getTaskHuntingPoints() const;
	uint32_t getTaskHuntingRerollPrice() const;
	const std::unique_ptr<TaskHuntingSlot> &getTaskHuntingWithCreature(uint16_t raceId) const;
	void addHuntingTaskKill(const std::shared_ptr<MonsterType> &mType);

	// Bestiary / cyclopedia tracker
	void parseBestiarySendRaces() const;
	void sendBestiaryCharms() const;
	void addBestiaryKillCount(uint16_t raceid, uint32_t amount);
	uint32_t getBestiaryKillCount(uint16_t raceid) const;
	void sendBestiaryEntryChanged(uint16_t raceid) const;
	void refreshCyclopediaMonsterTracker(const std::unordered_set<std::shared_ptr<MonsterType>> &trackerList, bool isBoss) const;
	bool isBossOnBosstiaryTracker(const std::shared_ptr<MonsterType> &monsterType) const;
	bool isMonsterOnBestiaryTracker(const std::shared_ptr<MonsterType> &monsterType) const;
	const std::unordered_set<std::shared_ptr<MonsterType>> &getCyclopediaMonsterTrackerSet(bool isBoss) const;
	void addMonsterToCyclopediaTrackerList(const std::shared_ptr<MonsterType> &mtype, bool isBoss, bool reloadClient = false);
	void removeMonsterFromCyclopediaTrackerList(const std::shared_ptr<MonsterType> &mtype, bool isBoss, bool reloadClient = false);
	void addBestiaryKill(const std::shared_ptr<MonsterType> &mType);
	void addBosstiaryKill(const std::shared_ptr<MonsterType> &mType);

private:
	Player &m_player;
};
