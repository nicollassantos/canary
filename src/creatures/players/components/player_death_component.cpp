/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_death_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "game/game.hpp"
#include "io/iobestiary.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"
#include "lua/creature/creatureevent.hpp"
#include "map/spectators.hpp"
#include "utils/tools.hpp"

void PlayerDeathComponent::death(const std::shared_ptr<Creature> &lastHitCreature) {
	if (!g_configManager().getBoolean(TOGGLE_MOUNT_IN_PZ) && m_player.isMounted()) {
		m_player.dismount();
		g_game().internalCreatureChangeOutfit(m_player.getPlayer(), m_player.defaultOutfit);
	}

	m_player.loginPosition = m_player.town->getTemplePosition();

	g_game().sendSingleSoundEffect(m_player.getPlayer()->getPosition(), m_player.sex == PLAYERSEX_FEMALE ? SoundEffect_t::HUMAN_FEMALE_DEATH : SoundEffect_t::HUMAN_MALE_DEATH, m_player.getPlayer());
	if (m_player.skillLoss) {
		int playerDmg = 0;
		int othersDmg = 0;
		uint32_t sumLevels = 0;
		uint32_t inFightTicks = 5 * 60 * 1000;
		for (const auto &[creatureId, damageInfo] : m_player.damageMap) {
			const auto &[total, ticks] = damageInfo;
			if ((OTSYS_TIME() - ticks) <= inFightTicks) {
				const auto &damageDealer = g_game().getPlayerByID(creatureId);
				if (damageDealer) {
					playerDmg += total;
					sumLevels += damageDealer->getLevel();
				} else {
					othersDmg += total;
				}
			}
		}

		bool pvpDeath = false;
		if (playerDmg > 0 || othersDmg > 0) {
			pvpDeath = (Player::lastHitIsPlayer(lastHitCreature) || playerDmg / (playerDmg + static_cast<double>(othersDmg)) >= 0.05);
		}

		uint8_t unfairFightReduction = 100;
		if (pvpDeath && sumLevels > m_player.level) {
			double reduce = m_player.level / static_cast<double>(sumLevels);
			unfairFightReduction = std::max<uint8_t>(20, std::floor((reduce * 100) + 0.5));
		}

		// Magic level loss
		uint64_t sumMana = 0;
		uint64_t lostMana = 0;

		// Sum up all the mana
		for (uint32_t i = 1; i <= m_player.magLevel; ++i) {
			sumMana += m_player.vocation->getReqMana(i);
		}

		sumMana += m_player.manaSpent;

		double deathLossPercent = getLostPercent() * (unfairFightReduction / 100.);

		// Charm bless bestiary
		const auto charmBless = m_player.charmsArray[CHARM_BLESS];
		const auto charmBlessRaceId = charmBless.raceId;
		const auto &charm = g_iobestiary().getBestiaryCharm(CHARM_BLESS);
		if (charm && lastHitCreature && lastHitCreature->getMonster() && charmBlessRaceId != 0) {
			const auto &mType = g_monsters().getMonsterType(lastHitCreature->getName());
			if (mType && mType->info.raceid == charmBlessRaceId) {
				const auto percentReduction = charm->chance[charmBless.tier] / 100;
				deathLossPercent -= deathLossPercent * percentReduction;
			}
		}

		lostMana = static_cast<uint64_t>(sumMana * deathLossPercent);

		while (lostMana > m_player.manaSpent && m_player.magLevel > 0) {
			lostMana -= m_player.manaSpent;
			m_player.manaSpent = m_player.vocation->getReqMana(m_player.magLevel);
			m_player.magLevel--;
		}

		m_player.manaSpent -= lostMana;

		uint64_t nextReqMana = m_player.vocation->getReqMana(m_player.magLevel + 1);
		if (nextReqMana > m_player.vocation->getReqMana(m_player.magLevel)) {
			m_player.magLevelPercent = Player::getPercentLevel(m_player.manaSpent, nextReqMana);
		} else {
			m_player.magLevelPercent = 0;
		}

		// Level loss
		auto expLoss = static_cast<uint64_t>(std::ceil(m_player.experience * deathLossPercent));
		g_logger().debug("[{}] - m_player.experience lost {}", __FUNCTION__, expLoss);

		g_events().eventPlayerOnLoseExperience(m_player.getPlayer(), expLoss);
		g_callbacks().executeCallback(EventCallback_t::playerOnLoseExperience, m_player.getPlayer(), expLoss);

		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, "You are dead.");
		std::ostringstream lostExp;
		lostExp << "You lost " << expLoss << " experience.";

		// Skill loss
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { // For each skill
			uint64_t sumSkillTries = 0;
			for (uint16_t c = 11; c <= m_player.skills[i].level; ++c) { // Sum up all required tries for all skill levels
				sumSkillTries += m_player.vocation->getReqSkillTries(i, c);
			}

			sumSkillTries += m_player.skills[i].tries;

			auto lostSkillTries = static_cast<uint32_t>(sumSkillTries * deathLossPercent);
			while (lostSkillTries > m_player.skills[i].tries) {
				lostSkillTries -= m_player.skills[i].tries;

				if (m_player.skills[i].level <= 10) {
					m_player.skills[i].level = 10;
					m_player.skills[i].tries = 0;
					lostSkillTries = 0;
					break;
				}

				m_player.skills[i].tries = m_player.vocation->getReqSkillTries(i, m_player.skills[i].level);
				m_player.skills[i].level--;
			}

			m_player.skills[i].tries = std::max<int32_t>(0, m_player.skills[i].tries - lostSkillTries);
			m_player.skills[i].percent = Player::getPercentLevel(m_player.skills[i].tries, m_player.vocation->getReqSkillTries(i, m_player.skills[i].level));
		}

		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, lostExp.str());

		if (expLoss != 0) {
			uint32_t oldLevel = m_player.level;

			if (m_player.vocation->getId() == VOCATION_NONE || m_player.level > 7) {
				m_player.experience -= expLoss;
			}

			while (m_player.level > 1 && m_player.experience < Player::getExpForLevel(m_player.level)) {
				--m_player.level;
				m_player.healthMax = std::max<int32_t>(0, m_player.healthMax - m_player.vocation->getHPGain());
				m_player.manaMax = std::max<int32_t>(0, m_player.manaMax - m_player.vocation->getManaGain());
				m_player.capacity = std::max<int32_t>(0, m_player.capacity - m_player.vocation->getCapGain());
			}

			if (oldLevel != m_player.level) {
				std::ostringstream ss;
				ss << "You were downgraded from Level " << oldLevel << " to Level " << m_player.level << '.';
				m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
			}

			uint64_t currLevelExp = Player::getExpForLevel(m_player.level);
			uint64_t nextLevelExp = Player::getExpForLevel(m_player.level + 1);
			if (nextLevelExp > currLevelExp) {
				m_player.levelPercent = Player::getPercentLevel(m_player.experience - currLevelExp, nextLevelExp - currLevelExp);
			} else {
				m_player.levelPercent = 0;
			}
		}

		std::ostringstream deathType;
		deathType << "You died during ";
		if (pvpDeath) {
			deathType << "PvP.";
		} else {
			deathType << "PvE.";
		}
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, deathType.str());

		auto adventurerBlessingLevel = g_configManager().getNumber(ADVENTURERSBLESSING_LEVEL);
		auto willNotLoseBless = m_player.getLevel() < adventurerBlessingLevel && m_player.getVocationId() > VOCATION_NONE;

		std::string bless = m_player.getBlessingsName();
		std::ostringstream blessOutput;
		if (willNotLoseBless) {
			blessOutput << fmt::format("You still have adventurer's blessings for being level lower than {}!", adventurerBlessingLevel);
		} else {
			bless.empty() ? blessOutput << "You weren't protected with any blessings."
						  : blessOutput << "You were blessed with " << bless;

			const auto playerSkull = m_player.getSkull();
			bool hasSkull = (playerSkull == Skulls_t::SKULL_RED || playerSkull == Skulls_t::SKULL_BLACK);
			uint8_t maxBlessing = 8;
			if (!hasSkull && pvpDeath && m_player.hasBlessing(1)) {
				m_player.removeBlessing(1, 1); // Remove TOF only
			} else {
				for (int i = 2; i <= maxBlessing; i++) {
					m_player.removeBlessing(i, 1);
				}

				const auto &playerAmulet = m_player.getThing(CONST_SLOT_NECKLACE);
				bool usingAol = (playerAmulet && playerAmulet->getItem()->getID() == ITEM_AMULETOFLOSS);
				if (usingAol) {
					m_player.removeItemOfType(ITEM_AMULETOFLOSS, 1, -1);
				}
			}
		}
		m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, blessOutput.str());

		m_player.sendStats();
		m_player.sendSkills();
		m_player.sendReLoginWindow(unfairFightReduction);
		m_player.sendBlessStatus();
		m_player.lastLogout = getTimeNow();
		if (m_player.getSkull() == SKULL_BLACK) {
			m_player.health = 40;
			m_player.mana = 0;
		} else {
			m_player.health = m_player.healthMax;
			m_player.mana = m_player.manaMax;
		}

		auto it = m_player.conditions.begin(), end = m_player.conditions.end();
		while (it != end) {
			auto condition = *it;
			// isSupress block to delete spells conditions (ensures that the player cannot, for example, reset the cooldown time of the familiar and summon several)
			if (condition->isPersistent() && condition->isRemovableOnDeath()) {
				it = m_player.conditions.erase(it);

				condition->endCondition(m_player.getPlayer());
				m_player.onEndCondition(condition->getType());
			} else {
				++it;
			}
		}
		despawn();
	} else {
		m_player.setSkillLoss(true);

		auto it = m_player.conditions.begin(), end = m_player.conditions.end();
		while (it != end) {
			auto condition = *it;
			if (condition->isPersistent()) {
				it = m_player.conditions.erase(it);

				condition->endCondition(m_player.getPlayer());
				m_player.onEndCondition(condition->getType());
			} else {
				++it;
			}
		}

		m_player.health = m_player.healthMax;
		g_game().internalTeleport(m_player.getPlayer(), m_player.getTemplePosition(), true);
		g_game().addCreatureHealth(m_player.getPlayer());
		g_game().addPlayerMana(m_player.getPlayer());
		m_player.onThink(EVENT_CREATURE_THINK_INTERVAL);
		m_player.onIdleStatus();
		m_player.sendStats();
	}

	if (m_player.getPlayerVocationEnum() == VOCATION_MONK_CIP) {
		m_player.emptyHarmony();
	}
}

void PlayerDeathComponent::despawn() {
	if (m_player.isDead()) {
		return;
	}

	m_player.listWalkDir.clear();
	m_player.stopEventWalk();
	m_player.onWalkAborted();
	m_player.closeAllExternalContainers();
	g_game().playerSetAttackedCreature(m_player.getPlayer()->getID(), 0);
	g_game().playerFollowCreature(m_player.getPlayer()->getID(), 0);

	// remove check
	Game::removeCreatureCheck(m_player.getPlayer());

	// remove from map
	const auto &tile = m_player.getTile();
	if (!tile) {
		return;
	}

	std::vector<int32_t> oldStackPosVector;

	const auto &spectators = Spectators().find<Creature>(tile->getPosition(), true);
	size_t i = 0;
	for (const auto &spectator : spectators) {
		if (const auto &player = spectator->getPlayer()) {
			oldStackPosVector.emplace_back(player->canSeeCreature(m_player.getPlayer()) ? tile->getClientIndexOfCreature(player, m_player.getPlayer()) : -1);
		}
		if (const auto &player = spectator->getPlayer()) {
			player->sendRemoveTileThing(tile->getPosition(), oldStackPosVector[i++]);
		}

		spectator->onRemoveCreature(m_player.getPlayer(), false);
	}

	tile->removeCreature(m_player.getPlayer());

	m_player.getParent()->postRemoveNotification(m_player.getPlayer(), nullptr, 0);

	g_game().removePlayer(m_player.getPlayer());

	// show player as pending
	for (const auto &[key, player] : g_game().getPlayers()) {
		player->vip().notifyStatusChange(m_player.getPlayer(), VipStatus_t::Pending, false);
	}

	if (m_player.m_party && g_configManager().getBoolean(LEAVE_PARTY_ON_DEATH)) {
		m_player.m_party->leaveParty(m_player.getPlayer());
	}

	m_player.setDead(true);
}

bool PlayerDeathComponent::dropCorpse(const std::shared_ptr<Creature> &lastHitCreature, const std::shared_ptr<Creature> &mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified) {
	if (m_player.getZoneType() != ZONE_PVP || !Player::lastHitIsPlayer(lastHitCreature)) {
		return m_player.Creature::dropCorpse(lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
	}

	m_player.setDropLoot(true);
	return false;
}

void PlayerDeathComponent::addUnjustifiedDead(const std::shared_ptr<Player> &attacked) {
	if (m_player.hasFlag(PlayerFlags_t::NotGainInFight) || attacked == m_player.getPlayer() || g_game().getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
		return;
	}

	m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, "Warning! The murder of " + attacked->getName() + " was not justified.");

	const auto killTime = time(nullptr);
	m_player.unjustifiedKills.emplace_back(attacked->getGUID(), killTime, true);
	m_player.updateLastKillTimeCache(killTime);

	uint8_t dayKills = 0;
	uint8_t weekKills = 0;
	uint8_t monthKills = 0;

	for (const auto &kill : m_player.unjustifiedKills) {
		const auto diff = time(nullptr) - kill.time;
		if (diff <= 4 * 60 * 60) {
			dayKills += 1;
		}
		if (diff <= 7 * 24 * 60 * 60) {
			weekKills += 1;
		}
		if (diff <= 30 * 24 * 60 * 60) {
			monthKills += 1;
		}
	}

	if (m_player.getSkull() != SKULL_BLACK) {
		if (dayKills >= 2 * g_configManager().getNumber(DAY_KILLS_TO_RED) || weekKills >= 2 * g_configManager().getNumber(WEEK_KILLS_TO_RED) || monthKills >= 2 * g_configManager().getNumber(MONTH_KILLS_TO_RED)) {
			m_player.setSkull(SKULL_BLACK);
			// start black skull time
			m_player.skullTicks = static_cast<int64_t>(g_configManager().getNumber(BLACK_SKULL_DURATION)) * 24 * 60 * 60;
		} else if (dayKills >= g_configManager().getNumber(DAY_KILLS_TO_RED) || weekKills >= g_configManager().getNumber(WEEK_KILLS_TO_RED) || monthKills >= g_configManager().getNumber(MONTH_KILLS_TO_RED)) {
			m_player.setSkull(SKULL_RED);
			// reset red skull time
			m_player.skullTicks = static_cast<int64_t>(g_configManager().getNumber(RED_SKULL_DURATION)) * 24 * 60 * 60;
		}
	}

	m_player.sendUnjustifiedPoints();
}

double PlayerDeathComponent::getLostPercent() const {
	int32_t blessingCount = 0;
	const uint8_t maxBlessing = 8;
	for (int i = 2; i <= maxBlessing; i++) {
		if (m_player.hasBlessing(i)) {
			blessingCount++;
		}
	}

	int32_t deathLosePercent = g_configManager().getNumber(DEATH_LOSE_PERCENT);
	if (deathLosePercent != -1) {
		if (m_player.isPromoted()) {
			deathLosePercent -= 3;
		}

		deathLosePercent -= blessingCount;
		return std::max<int32_t>(0, deathLosePercent) / 100.;
	}

	bool isRetro = g_configManager().getBoolean(TOGGLE_SERVER_IS_RETRO);
	const auto factor = (isRetro ? 6.31 : 8);
	double percentReduction = (blessingCount * factor) / 100.;

	double lossPercent;
	if (m_player.level >= 24) {
		const double tmpLevel = m_player.level + (m_player.levelPercent / 100.);
		lossPercent = ((tmpLevel + 50) * 50 * ((tmpLevel * tmpLevel) - (5 * tmpLevel) + 8)) / m_player.experience;
	} else {
		percentReduction = (percentReduction >= 0.40 ? 0.50 : percentReduction);
		lossPercent = 10;
	}

	g_logger().debug("[{}] - lossPercent {}", __FUNCTION__, lossPercent);
	g_logger().debug("[{}] - before promotion {}", __FUNCTION__, percentReduction);

	if (m_player.isPromoted()) {
		percentReduction += 30 / 100.;
		g_logger().debug("[{}] - after promotion {}", __FUNCTION__, percentReduction);
	}

	g_logger().debug("[{}] - total lost percent {}", __FUNCTION__, (lossPercent * (1 - percentReduction)) / 100.);

	return (lossPercent * (1 - percentReduction)) / 100.;
}