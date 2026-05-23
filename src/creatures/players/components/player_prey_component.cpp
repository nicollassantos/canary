/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_prey_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/player.hpp"
#include "io/io_bosstiary.hpp"
#include "io/iobestiary.hpp"
#include "io/ioprey.hpp"
#include "kv/kv.hpp"
#include "lib/logging/logger.hpp"
#include "server/network/protocol/protocolgame.hpp"

// Cyclopedia / tracker

const std::unordered_set<std::shared_ptr<MonsterType>> &PlayerPreyComponent::getCyclopediaMonsterTrackerSet(bool isBoss) const {
	return isBoss ? m_player.m_bosstiaryMonsterTracker : m_player.m_bestiaryMonsterTracker;
}

void PlayerPreyComponent::addMonsterToCyclopediaTrackerList(const std::shared_ptr<MonsterType> &mtype, bool isBoss, bool reloadClient /* = false */) {
	if (!m_player.client) {
		return;
	}

	const uint16_t raceId = mtype ? mtype->info.raceid : 0;
	auto &tracker = isBoss ? m_player.m_bosstiaryMonsterTracker : m_player.m_bestiaryMonsterTracker;
	if (tracker.size() < static_cast<size_t>(std::numeric_limits<uint8_t>::max()) && tracker.emplace(mtype).second) {
		if (reloadClient && raceId != 0) {
			if (isBoss) {
				m_player.client->parseSendBosstiary();
			} else {
				m_player.client->sendBestiaryEntryChanged(raceId);
			}
		}

		m_player.client->refreshCyclopediaMonsterTracker(tracker, isBoss);
	}
}

void PlayerPreyComponent::removeMonsterFromCyclopediaTrackerList(const std::shared_ptr<MonsterType> &mtype, bool isBoss, bool reloadClient /* = false */) {
	if (!m_player.client) {
		return;
	}

	const uint16_t raceId = mtype ? mtype->info.raceid : 0;
	auto &tracker = isBoss ? m_player.m_bosstiaryMonsterTracker : m_player.m_bestiaryMonsterTracker;

	if (tracker.erase(mtype) > 0) {
		if (reloadClient && raceId != 0) {
			if (isBoss) {
				m_player.client->parseSendBosstiary();
			} else {
				m_player.client->sendBestiaryEntryChanged(raceId);
			}
		}

		m_player.client->refreshCyclopediaMonsterTracker(tracker, isBoss);
	}
}

void PlayerPreyComponent::sendBestiaryEntryChanged(uint16_t raceid) const {
	if (m_player.client) {
		m_player.client->sendBestiaryEntryChanged(raceid);
	}
}

void PlayerPreyComponent::refreshCyclopediaMonsterTracker(const std::unordered_set<std::shared_ptr<MonsterType>> &trackerList, bool isBoss) const {
	if (m_player.client) {
		m_player.client->refreshCyclopediaMonsterTracker(trackerList, isBoss);
	}
}

bool PlayerPreyComponent::isBossOnBosstiaryTracker(const std::shared_ptr<MonsterType> &monsterType) const {
	return monsterType ? m_player.m_bosstiaryMonsterTracker.contains(monsterType) : false;
}

bool PlayerPreyComponent::isMonsterOnBestiaryTracker(const std::shared_ptr<MonsterType> &monsterType) const {
	return monsterType ? m_player.m_bestiaryMonsterTracker.contains(monsterType) : false;
}

// Prey

void PlayerPreyComponent::sendPreyData() const {
	if (m_player.client) {
		for (const std::unique_ptr<PreySlot> &slot : m_player.preys) {
			m_player.client->sendPreyData(slot);
		}

		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards);
	}
}

void PlayerPreyComponent::sendPreyTimeLeft(const std::unique_ptr<PreySlot> &slot) const {
	if (g_configManager().getBoolean(PREY_ENABLED) && m_player.client) {
		m_player.client->sendPreyTimeLeft(slot);
	}
}

void PlayerPreyComponent::reloadPreySlot(PreySlot_t slotid) {
	if (g_configManager().getBoolean(PREY_ENABLED) && m_player.client) {
		m_player.client->sendPreyData(getPreySlotById(slotid));
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
}

const std::unique_ptr<PreySlot> &PlayerPreyComponent::getPreySlotById(PreySlot_t slotid) {
	if (auto it = std::ranges::find_if(m_player.preys, [slotid](const std::unique_ptr<PreySlot> &preyIt) {
			return preyIt->id == slotid;
		});
	    it != m_player.preys.end()) {
		return *it;
	}

	return PreySlotNull;
}

bool PlayerPreyComponent::setPreySlotClass(std::unique_ptr<PreySlot> &slot) {
	if (getPreySlotById(slot->id)) {
		return false;
	}

	m_player.preys.emplace_back(std::move(slot));
	return true;
}

bool PlayerPreyComponent::usePreyCards(uint16_t amount) {
	if (m_player.preyCards < amount) {
		return false;
	}

	m_player.preyCards -= amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
	return true;
}

void PlayerPreyComponent::addPreyCards(uint64_t amount) {
	m_player.preyCards += amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
}

uint64_t PlayerPreyComponent::getPreyCards() const {
	return m_player.preyCards;
}

uint32_t PlayerPreyComponent::getPreyRerollPrice() const {
	return m_player.getLevel() * g_configManager().getNumber(PREY_REROLL_PRICE_LEVEL);
}

std::vector<uint16_t> PlayerPreyComponent::getPreyBlackList() const {
	std::vector<uint16_t> rt;
	for (const std::unique_ptr<PreySlot> &slot : m_player.preys) {
		if (slot) {
			if (slot->isOccupied()) {
				rt.push_back(slot->selectedRaceId);
			}
			for (uint16_t raceId : slot->raceIdList) {
				rt.push_back(raceId);
			}
		}
	}
	return rt;
}

const std::unique_ptr<PreySlot> &PlayerPreyComponent::getPreyWithMonster(uint16_t raceId) const {
	if (!g_configManager().getBoolean(PREY_ENABLED)) {
		return PreySlotNull;
	}

	if (auto it = std::ranges::find_if(m_player.preys, [raceId](const std::unique_ptr<PreySlot> &preyPtr) {
			return preyPtr->selectedRaceId == raceId;
		});
	    it != m_player.preys.end()) {
		return *it;
	}

	return PreySlotNull;
}

void PlayerPreyComponent::initializePrey() {
	if (m_player.preys.empty()) {
		for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
			auto slot = std::make_unique<PreySlot>(static_cast<PreySlot_t>(slotId));
			if (!g_configManager().getBoolean(PREY_ENABLED)) {
				slot->state = PreyDataState_Inactive;
			} else if (slot->id == PreySlot_Three && !g_configManager().getBoolean(PREY_FREE_THIRD_SLOT)) {
				slot->state = PreyDataState_Locked;
			} else if (slot->id == PreySlot_Two && !m_player.isPremium()) {
				slot->state = PreyDataState_Locked;
			} else {
				slot->state = PreyDataState_Selection;
				slot->reloadMonsterGrid(getPreyBlackList(), m_player.getLevel());
			}

			setPreySlotClass(slot);
		}
	}
}

void PlayerPreyComponent::removePreySlotById(PreySlot_t slotid) {
	const auto it = std::ranges::remove_if(m_player.preys, [slotid](const auto &preyIt) {
						return preyIt->id == slotid;
					}).begin();

	m_player.preys.erase(it, m_player.preys.end());
}

// Task hunting

void PlayerPreyComponent::initializeTaskHunting() {
	if (m_player.taskHunting.empty()) {
		for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
			auto slot = std::make_unique<TaskHuntingSlot>(static_cast<PreySlot_t>(slotId));
			if (!g_configManager().getBoolean(TASK_HUNTING_ENABLED)) {
				slot->state = PreyTaskDataState_Inactive;
			} else if (slot->id == PreySlot_Three && !g_configManager().getBoolean(TASK_HUNTING_FREE_THIRD_SLOT)) {
				slot->state = PreyTaskDataState_Locked;
			} else if (slot->id == PreySlot_Two && !m_player.isPremium()) {
				slot->state = PreyTaskDataState_Locked;
			} else {
				slot->state = PreyTaskDataState_Selection;
				slot->reloadMonsterGrid(getTaskHuntingBlackList(), m_player.getLevel());
			}

			setTaskHuntingSlotClass(slot);
		}
	}

	if (m_player.client && g_configManager().getBoolean(TASK_HUNTING_ENABLED) && !m_player.client->oldProtocol) {
		auto buffer = g_ioprey().getTaskHuntingBaseDate();
		m_player.client->writeToOutputBuffer(buffer);
	}
}

bool PlayerPreyComponent::isCreatureUnlockedOnTaskHunting(const std::shared_ptr<MonsterType> &mtype) const {
	if (!mtype) {
		return false;
	}

	return getBestiaryKillCount(mtype->info.raceid) >= mtype->info.bestiaryToUnlock;
}

bool PlayerPreyComponent::setTaskHuntingSlotClass(std::unique_ptr<TaskHuntingSlot> &slot) {
	if (getTaskHuntingSlotById(slot->id)) {
		return false;
	}

	m_player.taskHunting.emplace_back(std::move(slot));
	return true;
}

void PlayerPreyComponent::reloadTaskSlot(PreySlot_t slotid) {
	if (g_configManager().getBoolean(TASK_HUNTING_ENABLED) && m_player.client) {
		m_player.client->sendTaskHuntingData(getTaskHuntingSlotById(slotid));
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
}

const std::unique_ptr<TaskHuntingSlot> &PlayerPreyComponent::getTaskHuntingSlotById(PreySlot_t slotid) {
	if (auto it = std::ranges::find_if(m_player.taskHunting, [slotid](const std::unique_ptr<TaskHuntingSlot> &itTask) {
			return itTask->id == slotid;
		});
	    it != m_player.taskHunting.end()) {
		return *it;
	}

	return TaskHuntingSlotNull;
}

std::vector<uint16_t> PlayerPreyComponent::getTaskHuntingBlackList() const {
	std::vector<uint16_t> rt;

	std::ranges::for_each(m_player.taskHunting, [&rt](const std::unique_ptr<TaskHuntingSlot> &slot) {
		if (slot->isOccupied()) {
			rt.push_back(slot->selectedRaceId);
		} else {
			std::ranges::for_each(slot->raceIdList, [&rt](uint16_t raceId) {
				rt.push_back(raceId);
			});
		}
	});

	return rt;
}

void PlayerPreyComponent::sendTaskHuntingData() const {
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
		for (const std::unique_ptr<TaskHuntingSlot> &slot : m_player.taskHunting) {
			if (slot) {
				m_player.client->sendTaskHuntingData(slot);
			}
		}
	}
}

void PlayerPreyComponent::addTaskHuntingPoints(uint64_t amount) {
	m_player.taskHuntingPoints += amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
}

bool PlayerPreyComponent::useTaskHuntingPoints(uint64_t amount) {
	if (m_player.taskHuntingPoints < amount) {
		return false;
	}

	m_player.taskHuntingPoints -= amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.preyCards, m_player.taskHuntingPoints);
	}
	return true;
}

uint64_t PlayerPreyComponent::getTaskHuntingPoints() const {
	return m_player.taskHuntingPoints;
}

uint32_t PlayerPreyComponent::getTaskHuntingRerollPrice() const {
	return m_player.getLevel() * g_configManager().getNumber(TASK_HUNTING_REROLL_PRICE_LEVEL);
}

const std::unique_ptr<TaskHuntingSlot> &PlayerPreyComponent::getTaskHuntingWithCreature(uint16_t raceId) const {
	if (!g_configManager().getBoolean(TASK_HUNTING_ENABLED)) {
		return TaskHuntingSlotNull;
	}

	if (auto it = std::ranges::find_if(m_player.taskHunting, [raceId](const std::unique_ptr<TaskHuntingSlot> &itTask) {
			return itTask->selectedRaceId == raceId;
		});
	    it != m_player.taskHunting.end()) {
		return *it;
	}

	return TaskHuntingSlotNull;
}

void PlayerPreyComponent::addHuntingTaskKill(const std::shared_ptr<MonsterType> &mType) {
	const auto &taskSlot = getTaskHuntingWithCreature(mType->info.raceid);
	if (!taskSlot) {
		return;
	}

	if (const auto &option = g_ioprey().getTaskRewardOption(taskSlot)) {
		taskSlot->currentKills += 1;
		if ((taskSlot->upgrade && taskSlot->currentKills >= option->secondKills) || (!taskSlot->upgrade && taskSlot->currentKills >= option->firstKills)) {
			taskSlot->state = PreyTaskDataState_Completed;
			const std::string message = "You succesfully finished your hunting task. Your reward is ready to be claimed!";
			m_player.sendTextMessage(MESSAGE_STATUS, message);
		}
		reloadTaskSlot(taskSlot->id);
	}
}

// Bestiary

void PlayerPreyComponent::parseBestiarySendRaces() const {
	if (m_player.client) {
		m_player.client->parseBestiarySendRaces();
	}
}

void PlayerPreyComponent::sendBestiaryCharms() const {
	if (m_player.client) {
		m_player.client->sendBestiaryCharms();
	}
}

void PlayerPreyComponent::addBestiaryKillCount(uint16_t raceid, uint32_t amount) {
	const uint32_t oldCount = getBestiaryKillCount(raceid);
	const uint32_t key = STORAGEVALUE_BESTIARYKILLCOUNT + raceid;
	m_player.addStorageValue(key, static_cast<int32_t>(oldCount + amount), true);
}

uint32_t PlayerPreyComponent::getBestiaryKillCount(uint16_t raceid) const {
	const uint32_t key = STORAGEVALUE_BESTIARYKILLCOUNT + raceid;
	const auto value = m_player.getStorageValue(key);
	return value > 0 ? static_cast<uint32_t>(value) : 0;
}

void PlayerPreyComponent::addBestiaryKill(const std::shared_ptr<MonsterType> &mType) {
	if (mType->isBoss()) {
		return;
	}
	uint32_t kills = g_configManager().getNumber(BESTIARY_KILL_MULTIPLIER);

	auto scopedDoubleBestiary = g_kv().scoped("eventscheduler")->get("double-bestiary");
	bool doubleBestiaryEnabled = scopedDoubleBestiary && scopedDoubleBestiary->get<bool>();
	if (doubleBestiaryEnabled) {
		kills *= 2;
		g_logger().debug("[{}] double bestiary is enabled.", __FUNCTION__);
	}

	if (m_player.isConcoctionActive(Concoction_t::BestiaryBetterment)) {
		kills *= 2;
	}
	g_iobestiary().addBestiaryKill(m_player.getPlayer(), mType, kills);
}

void PlayerPreyComponent::addBosstiaryKill(const std::shared_ptr<MonsterType> &mType) {
	if (!mType->isBoss()) {
		return;
	}
	uint32_t kills = g_configManager().getNumber(BOSSTIARY_KILL_MULTIPLIER);

	auto scopedDoubleBosstiary = g_kv().scoped("eventscheduler")->get("double-bosstiary");
	bool doubleBosstiaryEnabled = scopedDoubleBosstiary && scopedDoubleBosstiary->get<bool>();
	if (doubleBosstiaryEnabled) {
		kills *= 2;
		g_logger().debug("[{}] double bosstiary is enabled.", __FUNCTION__);
	}

	if (g_ioBosstiary().getBoostedBossId() == mType->info.raceid) {
		kills *= g_configManager().getNumber(BOOSTED_BOSS_KILL_BONUS);
	}
	g_ioBosstiary().addBosstiaryKill(m_player.getPlayer(), mType, kills);
}
