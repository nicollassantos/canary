/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_experience_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/creature.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "game/game.hpp"
#include "lib/metrics/metrics.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"
#include "lua/creature/creatureevent.hpp"
#include "map/spectators.hpp"
#include "utils/tools.hpp"

uint16_t PlayerExperienceComponent::getBaseSkill(uint8_t skill) const {
	return m_player.skills[skill].level;
}

double_t PlayerExperienceComponent::getSkillPercent(skills_t skill) const {
	return m_player.skills[skill].percent;
}

void PlayerExperienceComponent::addSkillAdvance(skills_t skill, uint64_t count) {
	uint64_t currReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level);
	uint64_t nextReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level + 1);
	if (currReqTries >= nextReqTries) {
		// player has reached max skill
		return;
	}

	g_events().eventPlayerOnGainSkillTries(m_player.getPlayer(), skill, count);
	g_callbacks().executeCallback(EventCallback_t::playerOnGainSkillTries, m_player.getPlayer(), std::ref(skill), std::ref(count));
	if (count == 0) {
		return;
	}

	bool sendUpdateSkills = false;
	while ((m_player.skills[skill].tries + count) >= nextReqTries) {
		count -= nextReqTries - m_player.skills[skill].tries;
		m_player.skills[skill].level++;
		m_player.skills[skill].tries = 0;
		m_player.skills[skill].percent = 0;

		std::ostringstream ss;
		ss << "You advanced to " << getSkillName(skill) << " level " << m_player.skills[skill].level << '.';
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		if (skill == SKILL_LEVEL) {
			m_player.sendTakeScreenshot(SCREENSHOT_TYPE_LEVELUP);
		} else {
			m_player.sendTakeScreenshot(SCREENSHOT_TYPE_SKILLUP);
		}

		g_creatureEvents().playerAdvance(m_player.getPlayer(), skill, (m_player.skills[skill].level - 1), m_player.skills[skill].level);

		sendUpdateSkills = true;
		currReqTries = nextReqTries;
		nextReqTries = m_player.vocation->getReqSkillTries(skill, m_player.skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			count = 0;
			break;
		}
	}

	m_player.skills[skill].tries += count;

	double_t newPercent;
	if (nextReqTries > currReqTries) {
		newPercent = Player::getPercentLevel(m_player.skills[skill].tries, nextReqTries);
	} else {
		newPercent = 0;
	}

	if (m_player.skills[skill].percent != newPercent) {
		m_player.skills[skill].percent = newPercent;
		sendUpdateSkills = true;
	}

	if (sendUpdateSkills) {
		m_player.sendSkills();
		m_player.sendStats();
	}
}

void PlayerExperienceComponent::addManaSpent(uint64_t amount) {
	if (m_player.hasFlag(PlayerFlags_t::NotGainMana)) {
		return;
	}

	uint64_t currReqMana = m_player.vocation->getReqMana(m_player.magLevel);
	uint64_t nextReqMana = m_player.vocation->getReqMana(m_player.magLevel + 1);
	if (currReqMana >= nextReqMana) {
		// player has reached max magic level
		return;
	}

	g_events().eventPlayerOnGainSkillTries(m_player.getPlayer(), SKILL_MAGLEVEL, amount);
	g_callbacks().executeCallback(EventCallback_t::playerOnGainSkillTries, m_player.getPlayer(), SKILL_MAGLEVEL, amount);
	if (amount == 0) {
		return;
	}

	bool sendUpdateStats = false;
	while ((m_player.manaSpent + amount) >= nextReqMana) {
		amount -= nextReqMana - m_player.manaSpent;

		m_player.magLevel++;
		m_player.manaSpent = 0;

		std::ostringstream ss;
		ss << "You advanced to magic level " << m_player.magLevel << '.';
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		m_player.sendTakeScreenshot(SCREENSHOT_TYPE_SKILLUP);

		g_creatureEvents().playerAdvance(m_player.getPlayer(), SKILL_MAGLEVEL, m_player.magLevel - 1, m_player.magLevel);
		m_player.sendTakeScreenshot(SCREENSHOT_TYPE_SKILLUP);

		sendUpdateStats = true;
		currReqMana = nextReqMana;
		nextReqMana = m_player.vocation->getReqMana(m_player.magLevel + 1);
		if (currReqMana >= nextReqMana) {
			return;
		}
	}

	m_player.manaSpent += amount;

	const uint8_t oldPercent = m_player.magLevelPercent;
	if (nextReqMana > currReqMana) {
		m_player.magLevelPercent = Player::getPercentLevel(m_player.manaSpent, nextReqMana);
	} else {
		m_player.magLevelPercent = 0;
	}

	if (oldPercent != m_player.magLevelPercent) {
		sendUpdateStats = true;
	}

	if (sendUpdateStats) {
		m_player.sendStats();
		m_player.sendSkills();
	}
}

void PlayerExperienceComponent::addExperience(const std::shared_ptr<Creature> &target, uint64_t exp, bool sendText /* = false*/) {
	uint64_t currLevelExp = Player::getExpForLevel(m_player.level);
	uint64_t nextLevelExp = Player::getExpForLevel(m_player.level + 1);
	uint64_t rawExp = exp;
	if (currLevelExp >= nextLevelExp) {
		// player has reached max level
		m_player.levelPercent = 0;
		m_player.sendStats();
		return;
	}

	g_callbacks().executeCallback(EventCallback_t::playerOnGainExperience, m_player.getPlayer(), target, std::ref(exp), std::ref(rawExp));

	g_events().eventPlayerOnGainExperience(m_player.getPlayer(), target, exp, rawExp);
	if (exp == 0) {
		return;
	}

	const auto rate = exp / rawExp;
	const std::map<std::string, std::string> attrs({ { "player", m_player.getName() }, { "level", std::to_string(m_player.getLevel()) }, { "rate", std::to_string(rate) } });
	if (sendText) {
		g_metrics().addCounter("player_experience_raw", rawExp, attrs);
		g_metrics().addCounter("player_experience_actual", exp, attrs);
	} else {
		g_metrics().addCounter("player_experience_bonus_raw", rawExp, attrs);
		g_metrics().addCounter("player_experience_bonus_actual", exp, attrs);
	}

	// Hazard system experience
	const auto &monster = target && target->getMonster() ? target->getMonster() : nullptr;
	const bool handleHazardExperience = monster && monster->getHazard() && m_player.getHazardSystemPoints() > 0;
	if (handleHazardExperience) {
		exp += (exp * (1.75 * m_player.getHazardSystemPoints() * g_configManager().getFloat(HAZARD_EXP_BONUS_MULTIPLIER))) / 100.;
	}

	const bool handleAnimusMastery = monster && m_player.animusMastery().has(monster->getMonsterType()->name);
	float animusMasteryMultiplier = 0;

	if (handleAnimusMastery) {
		animusMasteryMultiplier = m_player.animusMastery().getExperienceMultiplier();
		exp *= animusMasteryMultiplier;
	}

	m_player.experience += exp;

	if (sendText) {
		std::string expString = fmt::format("{} experience point{}.", exp, (exp != 1 ? "s" : ""));
		if (m_player.isVip()) {
			uint8_t expPercent = g_configManager().getNumber(VIP_BONUS_EXP);
			if (expPercent > 0) {
				expString = expString + fmt::format(" (VIP bonus {}%)", expPercent > 100 ? 100 : expPercent);
			}
		}

		if (handleAnimusMastery) {
			expString = fmt::format("{} (animus mastery bonus {:.1f}%)", expString, (animusMasteryMultiplier - 1) * 100);
		}

		TextMessage message(MESSAGE_EXPERIENCE, "You gained " + expString + (handleHazardExperience ? " (Hazard)" : ""));
		message.position = m_player.position;
		message.primary.value = exp;
		message.primary.color = TEXTCOLOR_WHITE_EXP;
		m_player.sendTextMessage(message);

		auto spectators = Spectators().find<Player>(m_player.position);
		spectators.erase(m_player.getPlayer());
		if (!spectators.empty()) {
			message.type = MESSAGE_EXPERIENCE_OTHERS;
			message.text = m_player.getName() + " gained " + expString;
			for (const auto &spectator : spectators) {
				spectator->getPlayer()->sendTextMessage(message);
			}
		}
	}

	const uint32_t prevLevel = m_player.level;
	while (m_player.experience >= nextLevelExp) {
		++m_player.level;
		// Player stats gain for vocations level <= 8
		if (m_player.vocation->getId() != VOCATION_NONE && m_player.level <= 8) {
			const auto &noneVocation = g_vocations().getVocation(VOCATION_NONE);
			m_player.healthMax += noneVocation->getHPGain();
			m_player.health += noneVocation->getHPGain();
			m_player.manaMax += noneVocation->getManaGain();
			m_player.mana += noneVocation->getManaGain();
			m_player.capacity += noneVocation->getCapGain();
		} else {
			m_player.healthMax += m_player.vocation->getHPGain();
			m_player.health += m_player.vocation->getHPGain();
			m_player.manaMax += m_player.vocation->getManaGain();
			m_player.mana += m_player.vocation->getManaGain();
			m_player.capacity += m_player.vocation->getCapGain();
		}

		currLevelExp = nextLevelExp;
		nextLevelExp = Player::getExpForLevel(m_player.level + 1);
		if (currLevelExp >= nextLevelExp) {
			// player has reached max level
			break;
		}
	}

	if (prevLevel != m_player.level) {
		m_player.health = m_player.healthMax;
		m_player.mana = m_player.manaMax;

		m_player.updateBaseSpeed();
		m_player.setBaseSpeed(m_player.getBaseSpeed());
		g_game().changeSpeed(m_player.getPlayer(), 0);
		g_game().addCreatureHealth(m_player.getPlayer());
		g_game().addPlayerMana(m_player.getPlayer());

		if (m_player.m_party) {
			m_player.m_party->updateSharedExperience();
		}

		g_creatureEvents().playerAdvance(m_player.getPlayer(), SKILL_LEVEL, prevLevel, m_player.level);

		std::ostringstream ss;
		ss << "You advanced from Level " << prevLevel << " to Level " << m_player.level << '.';
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		m_player.sendTakeScreenshot(SCREENSHOT_TYPE_LEVELUP);
	}

	if (nextLevelExp > currLevelExp) {
		m_player.levelPercent = Player::getPercentLevel(m_player.experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		m_player.levelPercent = 0;
	}
	m_player.sendStats();
	m_player.sendExperienceTracker(rawExp, exp);
}

void PlayerExperienceComponent::removeExperience(uint64_t exp, bool sendText /* = false*/) {
	if (m_player.experience == 0 || exp == 0) {
		return;
	}

	g_events().eventPlayerOnLoseExperience(m_player.getPlayer(), exp);
	g_callbacks().executeCallback(EventCallback_t::playerOnLoseExperience, m_player.getPlayer(), std::ref(exp));
	if (exp == 0) {
		return;
	}

	uint64_t lostExp = m_player.experience;
	m_player.experience = std::max<int64_t>(0, m_player.experience - exp);

	if (sendText) {
		lostExp -= m_player.experience;

		const std::string expString = fmt::format("You lost {} experience point{}.", lostExp, (lostExp != 1 ? "s" : ""));

		TextMessage message(MESSAGE_EXPERIENCE, expString);
		message.position = m_player.position;
		message.primary.value = lostExp;
		message.primary.color = TEXTCOLOR_RED;
		m_player.sendTextMessage(message);

		auto spectators = Spectators().find<Player>(m_player.position);
		spectators.erase(m_player.getPlayer());
		if (!spectators.empty()) {
			message.type = MESSAGE_EXPERIENCE_OTHERS;
			message.text = m_player.getName() + " lost " + expString;
			for (const auto &spectator : spectators) {
				spectator->getPlayer()->sendTextMessage(message);
			}
		}
	}

	const uint32_t oldLevel = m_player.level;
	uint64_t currLevelExp = Player::getExpForLevel(m_player.level);

	while (m_player.level > 1 && m_player.experience < currLevelExp) {
		--m_player.level;
		// Player stats loss for vocations level <= 8
		if (m_player.vocation->getId() != VOCATION_NONE && m_player.level <= 8) {
			const auto &noneVocation = g_vocations().getVocation(VOCATION_NONE);
			m_player.healthMax = std::max<int32_t>(0, m_player.healthMax - noneVocation->getHPGain());
			m_player.manaMax = std::max<int32_t>(0, m_player.manaMax - noneVocation->getManaGain());
			m_player.capacity = std::max<int32_t>(0, m_player.capacity - noneVocation->getCapGain());
		} else {
			m_player.healthMax = std::max<int32_t>(0, m_player.healthMax - m_player.vocation->getHPGain());
			m_player.manaMax = std::max<int32_t>(0, m_player.manaMax - m_player.vocation->getManaGain());
			m_player.capacity = std::max<int32_t>(0, m_player.capacity - m_player.vocation->getCapGain());
		}
		currLevelExp = Player::getExpForLevel(m_player.level);
	}

	if (oldLevel != m_player.level) {
		m_player.health = m_player.healthMax;
		m_player.mana = m_player.manaMax;

		m_player.updateBaseSpeed();
		m_player.setBaseSpeed(m_player.getBaseSpeed());

		g_game().changeSpeed(m_player.getPlayer(), 0);
		g_game().addCreatureHealth(m_player.getPlayer());
		g_game().addPlayerMana(m_player.getPlayer());

		if (m_player.m_party) {
			m_player.m_party->updateSharedExperience();
		}

		std::ostringstream ss;
		ss << "You were downgraded from Level " << oldLevel << " to Level " << m_player.level << '.';
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
	}

	const uint64_t nextLevelExp = Player::getExpForLevel(m_player.level + 1);
	if (nextLevelExp > currLevelExp) {
		m_player.levelPercent = Player::getPercentLevel(m_player.experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		m_player.levelPercent = 0;
	}
	m_player.sendStats();
	m_player.sendExperienceTracker(0, -static_cast<int64_t>(exp));
}

void PlayerExperienceComponent::gainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target) {
	if (m_player.hasFlag(PlayerFlags_t::NotGainExperience) || gainExp == 0 || m_player.staminaMinutes == 0) {
		return;
	}

	addExperience(target, gainExp, true);
}

uint16_t PlayerExperienceComponent::getSkillLevel(skills_t skill) const {
	if (skill == SKILL_MAGLEVEL) {
		return static_cast<uint16_t>(std::min<uint32_t>(m_player.getMagicLevel(), std::numeric_limits<uint16_t>::max()));
	}

	const auto skillIndex = static_cast<int32_t>(skill);
	if (skillIndex < SKILL_FIRST || skillIndex > SKILL_LAST) {
		g_logger().error("[{}] Invalid skill type {}.", __FUNCTION__, skillIndex);
		return 0;
	}

	auto skillLevel = m_player.getLoyaltySkill(skill);
	skillLevel = std::max<int32_t>(0, skillLevel + m_player.varSkills[skill]);

	const auto &maxValuePerSkill = getMaxValuePerSkill();
	if (const auto it = maxValuePerSkill.find(skill);
	    it != maxValuePerSkill.end()) {
		skillLevel = std::min<int32_t>(it->second, skillLevel);
	}

	// Wheel of destiny
	if (skill == SKILL_FIST) {
		skillLevel += m_player.wheel().getStat(WheelStat_t::FIST);
	} else if (skill >= SKILL_CLUB && skill <= SKILL_AXE) {
		skillLevel += m_player.wheel().getStat(WheelStat_t::MELEE);
	} else if (skill == SKILL_DISTANCE) {
		skillLevel += m_player.wheel().getMajorStatConditional("Positional Tactics", WheelMajor_t::DISTANCE);
		skillLevel += m_player.wheel().getStat(WheelStat_t::DISTANCE);
	} else if (skill == SKILL_SHIELD) {
		skillLevel += m_player.wheel().getMajorStatConditional("Battle Instinct", WheelMajor_t::SHIELD);
	} else if (skill == SKILL_LIFE_LEECH_AMOUNT) {
		skillLevel += m_player.wheel().getStat(WheelStat_t::LIFE_LEECH);
	} else if (skill == SKILL_MANA_LEECH_AMOUNT) {
		skillLevel += m_player.wheel().getStat(WheelStat_t::MANA_LEECH);
	} else if (skill == SKILL_CRITICAL_HIT_DAMAGE) {
		skillLevel += m_player.wheel().getStat(WheelStat_t::CRITICAL_DAMAGE);
		skillLevel += m_player.wheel().getMajorStatConditional("Combat Mastery", WheelMajor_t::CRITICAL_DMG_2);
		skillLevel += m_player.wheel().getMajorStatConditional("Ballistic Mastery", WheelMajor_t::CRITICAL_DMG);
		skillLevel += m_player.wheel().checkAvatarSkill(WheelAvatarSkill_t::CRITICAL_DAMAGE);
		skillLevel += m_player.m_weaponProficiency.getGeneralCritical().damage * 10000;
	} else if (skill == SKILL_CRITICAL_HIT_CHANCE) {
		skillLevel += m_player.m_weaponProficiency.getGeneralCritical().chance * 10000;
	}

	// Weapon proficiency
	const auto weaponProficiencySkill = m_player.m_weaponProficiency.getSkillBonus(skill);
	skillLevel += weaponProficiencySkill;

	const int32_t avatarCritChance = m_player.wheel().checkAvatarSkill(WheelAvatarSkill_t::CRITICAL_CHANCE);
	if (skill == SKILL_CRITICAL_HIT_CHANCE && avatarCritChance > 0) {
		skillLevel = avatarCritChance; // 100%
	}

	return std::min<uint16_t>(std::numeric_limits<uint16_t>::max(), std::max<uint16_t>(0, static_cast<uint16_t>(skillLevel)));
}