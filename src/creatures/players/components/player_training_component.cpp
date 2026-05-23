/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_training_component.hpp"

#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/creatureevent.hpp"
#include "lua/creature/events.hpp"
#include "utils/tools.hpp"

bool PlayerTrainingComponent::addOfflineTrainingTries(skills_t skill, uint64_t tries) {
	if (tries == 0 || skill == SKILL_LEVEL) {
		return false;
	}

	bool sendUpdate = false;
	uint32_t oldSkillValue, newSkillValue;
	long double oldPercentToNextLevel, newPercentToNextLevel;

	if (skill == SKILL_MAGLEVEL) {
		uint64_t currReqMana = m_player.vocation->getReqMana(m_player.magLevel);
		uint64_t nextReqMana = m_player.vocation->getReqMana(m_player.magLevel + 1);

		if (currReqMana >= nextReqMana) {
			return false;
		}

		oldSkillValue = m_player.magLevel;
		oldPercentToNextLevel = static_cast<long double>(m_player.manaSpent * 100) / nextReqMana;

		g_events().eventPlayerOnGainSkillTries(m_player.static_self_cast<Player>(), SKILL_MAGLEVEL, tries);
		g_callbacks().executeCallback(EventCallback_t::playerOnGainSkillTries, m_player.getPlayer(), SKILL_MAGLEVEL, std::ref(tries));

		uint32_t currMagLevel = m_player.magLevel;
		while ((m_player.manaSpent + tries) >= nextReqMana) {
			tries -= nextReqMana - m_player.manaSpent;

			m_player.magLevel++;
			m_player.manaSpent = 0;

			g_creatureEvents().playerAdvance(m_player.static_self_cast<Player>(), SKILL_MAGLEVEL, m_player.magLevel - 1, m_player.magLevel);

			sendUpdate = true;
			currReqMana = nextReqMana;
			nextReqMana = m_player.vocation->getReqMana(m_player.magLevel + 1);

			if (currReqMana >= nextReqMana) {
				tries = 0;
				break;
			}
		}

		m_player.manaSpent += tries;

		if (m_player.magLevel != currMagLevel) {
			std::ostringstream ss;
			ss << "You advanced to magic level " << m_player.magLevel << '.';
			m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
			m_player.sendTakeScreenshot(SCREENSHOT_TYPE_SKILLUP);
		}

		uint8_t newPercent;
		if (nextReqMana > currReqMana) {
			newPercent = Player::getPercentLevel(m_player.manaSpent, nextReqMana);
			newPercentToNextLevel = static_cast<long double>(m_player.manaSpent * 100) / nextReqMana;
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (newPercent != m_player.magLevelPercent) {
			m_player.magLevelPercent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = m_player.magLevel;
	} else {
		uint64_t currReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level);
		uint64_t nextReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			return false;
		}

		oldSkillValue = m_player.skills[skill].level;
		oldPercentToNextLevel = static_cast<long double>(m_player.skills[skill].tries * 100) / nextReqTries;

		g_events().eventPlayerOnGainSkillTries(m_player.static_self_cast<Player>(), skill, tries);
		g_callbacks().executeCallback(EventCallback_t::playerOnGainSkillTries, m_player.getPlayer(), skill, tries);
		uint32_t currSkillLevel = m_player.skills[skill].level;

		while ((m_player.skills[skill].tries + tries) >= nextReqTries) {
			tries -= nextReqTries - m_player.skills[skill].tries;

			m_player.skills[skill].level++;
			m_player.skills[skill].tries = 0;
			m_player.skills[skill].percent = 0;

			g_creatureEvents().playerAdvance(m_player.static_self_cast<Player>(), skill, (m_player.skills[skill].level - 1), m_player.skills[skill].level);

			sendUpdate = true;
			currReqTries = nextReqTries;
			nextReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level + 1);

			if (currReqTries >= nextReqTries) {
				tries = 0;
				break;
			}
		}

		m_player.skills[skill].tries += tries;

		if (currSkillLevel != m_player.skills[skill].level) {
			std::ostringstream ss;
			ss << "You advanced to " << getSkillName(skill) << " level " << m_player.skills[skill].level << '.';
			m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
			if (skill == SKILL_LEVEL) {
				m_player.sendTakeScreenshot(SCREENSHOT_TYPE_LEVELUP);
			} else {
				m_player.sendTakeScreenshot(SCREENSHOT_TYPE_SKILLUP);
			}
		}

		uint8_t newPercent;
		if (nextReqTries > currReqTries) {
			newPercent = Player::getPercentLevel(m_player.skills[skill].tries, nextReqTries);
			newPercentToNextLevel = static_cast<long double>(m_player.skills[skill].tries * 100) / nextReqTries;
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (m_player.skills[skill].percent != newPercent) {
			m_player.skills[skill].percent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = m_player.skills[skill].level;
	}

	if (sendUpdate) {
		m_player.sendSkills();
		m_player.sendStats();
	}

	std::string message = fmt::format(
		"Your {} skill changed from level {} (with {:.2f}% progress towards level {}) to level {} (with {:.2f}% progress towards level {})",
		ucwords(getSkillName(skill)),
		oldSkillValue,
		oldPercentToNextLevel,
		oldSkillValue + 1,
		newSkillValue,
		newPercentToNextLevel,
		newSkillValue + 1
	);

	m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, message);
	return sendUpdate;
}

void PlayerTrainingComponent::addOfflineTrainingTime(int32_t addTime) {
	m_player.offlineTrainingTime = std::min<int32_t>(12 * 3600 * 1000, m_player.offlineTrainingTime + addTime);
}

void PlayerTrainingComponent::removeOfflineTrainingTime(int32_t removeTime) {
	m_player.offlineTrainingTime = std::max<int32_t>(0, m_player.offlineTrainingTime - removeTime);
}

int32_t PlayerTrainingComponent::getOfflineTrainingTime() const {
	return m_player.offlineTrainingTime;
}

int8_t PlayerTrainingComponent::getOfflineTrainingSkill() const {
	return m_player.offlineTrainingSkill;
}

void PlayerTrainingComponent::setOfflineTrainingSkill(int8_t skill) {
	m_player.offlineTrainingSkill = skill;
}
