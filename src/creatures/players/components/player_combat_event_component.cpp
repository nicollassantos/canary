#include "creatures/players/components/player_combat_event_component.hpp"

#include "creatures/players/player.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/combat/combat.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "config/configmanager.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "game/scheduling/events_scheduler.hpp"
#include "items/weapons/weapons.hpp"
#include "lua/creature/creatureevent.hpp"
#include "server/network/protocol/protocolgame.hpp"

void PlayerCombatEventComponent::onBlockHit() {
	if (m_player.shieldBlockCount > 0) {
		--m_player.shieldBlockCount;

		if (hasShield()) {
			m_player.addSkillAdvance(SKILL_SHIELD, 1);
		}
	}
}
void PlayerCombatEventComponent::onTakeDamage(const std::shared_ptr<Creature> &attacker, int32_t damage) {
	// nothing here yet
}
void PlayerCombatEventComponent::onAttackedCreatureBlockHit(const BlockType_t &blockType) {
	m_player.lastAttackBlockType = blockType;

	switch (blockType) {
		case BLOCK_NONE: {
			m_player.addAttackSkillPoint = true;
			m_player.bloodHitCount = 30;
			m_player.shieldBlockCount = 30;
			break;
		}

		case BLOCK_DEFENSE:
		case BLOCK_ARMOR: {
			// need to draw blood every 30 hits
			if (m_player.bloodHitCount > 0) {
				m_player.addAttackSkillPoint = true;
				--m_player.bloodHitCount;
			} else {
				m_player.addAttackSkillPoint = false;
			}
			break;
		}

		default: {
			m_player.addAttackSkillPoint = false;
			break;
		}
	}
}
bool PlayerCombatEventComponent::hasShield() const {
	const auto &itemLeft = m_player.inventory[CONST_SLOT_LEFT];
	if (itemLeft && itemLeft->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}

	const auto &itemRight = m_player.inventory[CONST_SLOT_RIGHT];
	if (itemRight && itemRight->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}
	return false;
}
bool PlayerCombatEventComponent::isPzLocked() const {
	return m_player.pzLocked;
}
BlockType_t PlayerCombatEventComponent::blockHit(const std::shared_ptr<Creature> &attacker, const CombatType_t &combatType, int32_t &damage, bool checkDefense, bool checkArmor, bool field) {
	BlockType_t blockType = m_player.Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor, field);
	if (attacker) {
		m_player.sendCreatureSquare(attacker, SQ_COLOR_BLACK);
	}

	if (blockType != BLOCK_NONE) {
		return blockType;
	}

	if (damage > 0) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			if (!m_player.isItemAbilityEnabled(static_cast<Slots_t>(slot))) {
				continue;
			}

			const auto &item = m_player.inventory[slot];
			if (!item) {
				continue;
			}

			for (uint8_t slotid = 0; slotid < item->getImbuementSlot(); slotid++) {
				ImbuementInfo imbuementInfo;
				if (!item->getImbuementInfo(slotid, &imbuementInfo)) {
					continue;
				}

				const int16_t &imbuementAbsorbPercent = imbuementInfo.imbuement->absorbPercent[combatTypeToIndex(combatType)];

				if (imbuementAbsorbPercent != 0) {
					damage -= std::ceil(damage * (imbuementAbsorbPercent / 100.));
				}
			}

			// Absorb Percent
			const ItemType &it = Item::items[item->getID()];
			if (it.abilities) {
				int totalAbsorbPercent = 0;
				const int16_t &absorbPercent = it.abilities->absorbPercent[combatTypeToIndex(combatType)];
				if (absorbPercent != 0) {
					totalAbsorbPercent += absorbPercent;
				}

				if (field) {
					const int16_t &fieldAbsorbPercent = it.abilities->fieldAbsorbPercent[combatTypeToIndex(combatType)];
					if (fieldAbsorbPercent != 0) {
						totalAbsorbPercent += fieldAbsorbPercent;
					}
				}

				if (totalAbsorbPercent != 0) {
					damage -= std::round(damage * (totalAbsorbPercent / 100.0));

					const auto charges = item->getAttribute<uint16_t>(ItemAttribute_t::CHARGES);
					if (charges != 0) {
						g_game().transformItem(item, item->getID(), charges - 1);
					}
				}
			}
		}

		// Wheel of destiny - apply resistance
		m_player.wheel().adjustDamageBasedOnResistanceAndSkill(damage, combatType);

		if (damage <= 0) {
			damage = 0;
			blockType = BLOCK_ARMOR;
		}
	}

	return blockType;
}
void PlayerCombatEventComponent::doAttacking(uint32_t interval) {
	if (m_player.lastAttack == 0) {
		m_player.lastAttack = OTSYS_TIME() - m_player.getAttackSpeed() - 1;
	}

	if (m_player.hasCondition(CONDITION_PACIFIED)) {
		return;
	}

	const auto &attackedCreature = m_player.getAttackedCreature();
	if (!attackedCreature) {
		return;
	}

	if ((OTSYS_TIME() - m_player.lastAttack) >= m_player.getAttackSpeed()) {
		bool result = false;

		const auto &tool = m_player.getWeapon();
		const auto &weapon = g_weapons().getWeapon(tool);
		uint32_t delay = m_player.getAttackSpeed();
		bool classicSpeed = g_configManager().getBoolean(CLASSIC_ATTACK_SPEED);

		if (weapon) {
			if (!weapon->interruptSwing()) {
				result = weapon->useWeapon(m_player.getPlayer(), tool, attackedCreature);
			} else if (!classicSpeed && !m_player.canDoAction()) {
				delay = m_player.getNextActionTime();
			} else {
				result = weapon->useWeapon(m_player.getPlayer(), tool, attackedCreature);
			}
		} else if (m_player.hasWeaponDistanceEquipped()) {
			return;
		} else {
			result = Weapon::useFist(m_player.getPlayer(), attackedCreature);
		}

		const auto &task = m_player.createPlayerTask(
			std::max<uint32_t>(SCHEDULER_MINTICKS, delay), [self = std::weak_ptr<Creature>(m_player.getCreature())] {
				if (const auto &creature = self.lock()) {
					creature->checkCreatureAttack(true);
				} }, __FUNCTION__
		);

		if (!classicSpeed) {
			m_player.setNextActionTask(task, false);
		} else {
			g_dispatcher().scheduleEvent(task);
		}

		if (result) {
			m_player.updateLastAggressiveAction();
			m_player.updateLastAttack();
		}
	}
}
std::shared_ptr<Item> PlayerCombatEventComponent::getCorpse(const std::shared_ptr<Creature> &lastHitCreature, const std::shared_ptr<Creature> &mostDamageCreature) {
	const auto &corpse = m_player.Creature::getCorpse(lastHitCreature, mostDamageCreature);
	if (corpse && corpse->getContainer()) {
		std::ostringstream ss;

		ss << "You recognize " << m_player.getNameDescription() << ". ";

		std::string responsibleName;
		std::string secondaryResponsibleName;
		bool hasOthers = false;

		if (lastHitCreature) {
			if (lastHitCreature->getPlayer()) {
				responsibleName = lastHitCreature->getNameDescription();
			} else if (auto master = lastHitCreature->getMaster(); master && master->getPlayer()) {
				responsibleName = master->getNameDescription();
			}
		}

		if (mostDamageCreature) {
			if (mostDamageCreature->getPlayer()) {
				if (responsibleName != mostDamageCreature->getNameDescription()) {
					secondaryResponsibleName = responsibleName;
					responsibleName = mostDamageCreature->getNameDescription();
				}
			} else if (auto master = mostDamageCreature->getMaster(); master && master->getPlayer()) {
				if (responsibleName != master->getNameDescription()) {
					secondaryResponsibleName = responsibleName;
					responsibleName = master->getNameDescription();
				}
			}
		}

		uint32_t inFightTicks = 5 * 60 * 1000;
		for (const auto &[creatureId, damageInfo] : m_player.damageMap) {
			const auto &[total, ticks] = damageInfo;
			if ((OTSYS_TIME() - ticks) <= inFightTicks) {
				const auto &attacker = g_game().getCreatureByID(creatureId);
				if (attacker && !attacker->getPlayer()) {
					hasOthers = true;
					break;
				}
			}
		}

		if (!responsibleName.empty()) {
			ss << m_player.getSubjectPronoun() << " " << m_player.getSubjectVerb(true) << " killed by " << responsibleName;

			if (!secondaryResponsibleName.empty()) {
				ss << " and " << secondaryResponsibleName;
			} else if (hasOthers) {
				ss << " and others";
			}
			ss << '.';
		} else if (lastHitCreature) {
			ss << m_player.getSubjectPronoun() << " " << m_player.getSubjectVerb(true) << " killed by " << lastHitCreature->getNameDescription();
			if (hasOthers) {
				ss << " and others";
			}
			ss << '.';
		} else {
			ss << "No attackers were identified.";
		}

		corpse->setAttribute(ItemAttribute_t::DESCRIPTION, ss.str());
	}
	return corpse;
}
void PlayerCombatEventComponent::onAddCondition(ConditionType_t type) {
	m_player.Creature::onAddCondition(type);

	if (type == CONDITION_PARALYZE) {
		m_player.updateParalyzeWalkExhaust();
	}

	if (type == CONDITION_OUTFIT && m_player.isMounted()) {
		m_player.dismount();
		m_player.wasMounted = true;
	}

	m_player.sendIcons();
}
void PlayerCombatEventComponent::onCleanseCondition(ConditionType_t type) const {
	static const std::unordered_map<ConditionType_t, std::string_view> conditionMessages = {
		{ CONDITION_POISON, "poisoned" },
		{ CONDITION_FIRE, "burning" },
		{ CONDITION_ENERGY, "electrified" },
		{ CONDITION_FREEZING, "freezing" },
		{ CONDITION_CURSED, "cursed" },
		{ CONDITION_DAZZLED, "dazzled" },
		{ CONDITION_BLEEDING, "bleeding" },
		{ CONDITION_PARALYZE, "paralyzed" },
		{ CONDITION_ROOTED, "rooted" },
		{ CONDITION_FEARED, "feared" }
	};

	auto it = conditionMessages.find(type);
	if (it != conditionMessages.end()) {
		m_player.sendTextMessage(MESSAGE_PARTY, fmt::format("You are no longer {}. (cleanse charm)", it->second));
	}
}
void PlayerCombatEventComponent::onAddCombatCondition(ConditionType_t type) {
	if (type == CONDITION_PARALYZE) {
		m_player.updateParalyzeWalkExhaust();
	}

	if (IsConditionSuppressible(type)) {
		m_player.updateLastConditionTime(type);
	}
	switch (type) {
		case CONDITION_POISON:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are poisoned.");
			break;

		case CONDITION_DROWN:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are drowning.");
			break;

		case CONDITION_PARALYZE:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are paralyzed.");
			break;

		case CONDITION_DRUNK:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are drunk.");
			break;

		case CONDITION_LESSERHEX:

		case CONDITION_INTENSEHEX:

		case CONDITION_GREATERHEX:

			m_player.sendTextMessage(MESSAGE_FAILURE, "You are hexed.");
			break;
		case CONDITION_ROOTED:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are rooted.");
			break;

		case CONDITION_FEARED:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are feared.");
			break;

		case CONDITION_CURSED:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are cursed.");
			break;

		case CONDITION_FREEZING:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are freezing.");
			break;

		case CONDITION_DAZZLED:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are dazzled.");
			break;

		case CONDITION_BLEEDING:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are bleeding.");
			break;

		case CONDITION_AGONY:
			m_player.sendTextMessage(MESSAGE_FAILURE, "You are in agony.");
			break;

		default:
			break;
	}
}
void PlayerCombatEventComponent::onEndCondition(ConditionType_t type) {
	m_player.Creature::onEndCondition(type);

	const auto &conditionFight = m_player.getCondition(CONDITION_INFIGHT);
	if (type == CONDITION_INFIGHT && !conditionFight) {
		onIdleStatus();
		m_player.pzLocked = false;
		m_player.clearAttacked();
		m_player.sendOpenPvpSituations();

		if (m_player.getSkull() != SKULL_RED && m_player.getSkull() != SKULL_BLACK) {
			m_player.setSkull(SKULL_NONE);
		}
	}

	if (type == CONDITION_OUTFIT && m_player.wasMounted) {
		m_player.toggleMount(true);
	}

	m_player.sendIcons();
}
void PlayerCombatEventComponent::onCombatRemoveCondition(const std::shared_ptr<Condition> &condition) {
	if (!condition) {
		return;
	}

	// m_player.Creature::onCombatRemoveCondition(condition);
	if (condition->getId() > 0) {
		// Means the condition is from an item, id == slot
		if (g_game().getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			const auto &item = m_player.getInventoryItem(static_cast<Slots_t>(condition->getId()));
			if (item) {
				// 25% chance to destroy the item
				if (25 >= uniform_random(1, 100)) {
					g_game().internalRemoveItem(item);
				}
			}
		}
	} else {
		if (!m_player.canDoAction()) {
			const uint32_t delay = m_player.getNextActionTime();
			const int32_t ticks = delay - (delay % EVENT_CREATURE_THINK_INTERVAL);
			if (ticks < 0 || condition->getType() == CONDITION_PARALYZE) {
				m_player.removeCondition(condition);
			} else {
				condition->setTicks(ticks);
			}
		} else {
			m_player.removeCondition(condition);
		}
	}
}
void PlayerCombatEventComponent::onAttackedCreature(const std::shared_ptr<Creature> &target) {
	m_player.Creature::onAttackedCreature(target);

	if (!target) {
		return;
	}

	if (target->getZoneType() == ZONE_PVP) {
		return;
	}

	if (target == m_player.getPlayer()) {
		m_player.addInFightTicks();
		return;
	}

	if (m_player.hasFlag(PlayerFlags_t::NotGainInFight)) {
		return;
	}

	const auto &targetPlayer = target->getPlayer();
	if (targetPlayer && !m_player.isPartner(targetPlayer) && !m_player.isGuildMate(targetPlayer)) {
		if (!m_player.pzLocked && g_game().getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			m_player.pzLocked = true;
			m_player.sendIcons();
		}

		if (m_player.getSkull() == SKULL_NONE && m_player.getSkullClient(targetPlayer) == SKULL_YELLOW) {
			m_player.addAttacked(targetPlayer);
			targetPlayer->sendCreatureSkull(m_player.getPlayer());
		} else if (!targetPlayer->hasAttacked(m_player.getPlayer())) {
			if (!m_player.pzLocked) {
				m_player.pzLocked = true;
				m_player.sendIcons();
			}

			if (!Combat::isInPvpZone(m_player.getPlayer(), targetPlayer) && !m_player.isInWar(targetPlayer)) {
				m_player.addAttacked(targetPlayer);

				if (targetPlayer->getSkull() == SKULL_NONE && m_player.getSkull() == SKULL_NONE && !targetPlayer->hasKilled(m_player.getPlayer())) {
					m_player.setSkull(SKULL_WHITE);
				}

				if (m_player.getSkull() == SKULL_NONE) {
					targetPlayer->sendCreatureSkull(m_player.getPlayer());
				}
			}
		}
	}

	m_player.addInFightTicks();
	m_player.sendOpenPvpSituations();
}
void PlayerCombatEventComponent::onAttacked() {
	m_player.Creature::onAttacked();

	m_player.addInFightTicks();
	m_player.sendOpenPvpSituations();
}
void PlayerCombatEventComponent::onIdleStatus() {
	m_player.Creature::onIdleStatus();

	if (m_player.m_party) {
		m_player.m_party->clearPlayerPoints(m_player.getPlayer());
	}
}
void PlayerCombatEventComponent::onPlacedCreature() {
	// scripting event - onLogin
	if (!g_creatureEvents().playerLogin(m_player.getPlayer())) {
		m_player.removePlayer(true);
	}

	m_player.onChangeZone(m_player.getZoneType());

	m_player.refreshSkullTicksFromLastKill();
	m_player.sendUnjustifiedPoints();
	m_player.sendOpenPvpSituations();

	const auto activeEvents = g_eventsScheduler().getActiveEvents();
	if (!activeEvents.empty()) {
		std::string eventsList;
		for (size_t i = 0; i < activeEvents.size(); ++i) {
			eventsList.append(activeEvents[i]);
			if (i < activeEvents.size() - 1) {
				eventsList.append(", ");
			}
		}
		g_logger().info("[{}] Active EventScheduler: {}", m_player.getName(), eventsList);
		m_player.sendTextMessage(MESSAGE_BOOSTED_CREATURE, fmt::format("Active EventScheduler: {}", eventsList));
	}
}
void PlayerCombatEventComponent::onAttackedCreatureDrainHealth(const std::shared_ptr<Creature> &target, int32_t points) {
	m_player.Creature::onAttackedCreatureDrainHealth(target, points);

	if (target) {
		if (m_player.m_party && !Combat::isPlayerCombat(target)) {
			const auto &tmpMonster = target->getMonster();
			if (tmpMonster && tmpMonster->isHostile()) {
				// We have fulfilled a requirement for shared experience
				m_player.m_party->updatePlayerTicks(m_player.getPlayer(), points);
			}
		}
	}
}
void PlayerCombatEventComponent::onTargetCreatureGainHealth(const std::shared_ptr<Creature> &target, int32_t points) {
	if (target && m_player.m_party) {
		std::shared_ptr<Player> tmpPlayer = nullptr;

		if (m_player.isPartner(tmpPlayer) && (tmpPlayer != m_player.getPlayer())) {
			tmpPlayer = target->getPlayer();
		} else if (const auto &targetMaster = target->getMaster()) {
			if (const auto &targetMasterPlayer = targetMaster->getPlayer()) {
				tmpPlayer = targetMasterPlayer;
			}
		}

		if (m_player.isPartner(tmpPlayer)) {
			m_player.m_party->updatePlayerTicks(m_player.getPlayer(), points);
		}
	}
}
bool PlayerCombatEventComponent::onKilledPlayer(const std::shared_ptr<Player> &target, bool lastHit) {
	bool unjustified = false;
	if (target->getZoneType() == ZONE_PVP) {
		target->setDropLoot(false);
		target->setSkillLoss(false);
	} else if (!m_player.hasFlag(PlayerFlags_t::NotGainInFight) && !m_player.isPartner(target)) {
		if (!Combat::isInPvpZone(m_player.getPlayer(), target) && m_player.hasAttacked(target) && !target->hasAttacked(m_player.getPlayer()) && !m_player.isGuildMate(target) && target != m_player.getPlayer()) {
			if (target->hasKilled(m_player.getPlayer())) {
				for (auto &kill : target->unjustifiedKills) {
					if (kill.target == m_player.getGUID() && kill.unavenged) {
						kill.unavenged = false;
						m_player.attackedSet.erase(target->guid);
						break;
					}
				}
			} else if (target->getSkull() == SKULL_NONE && !m_player.isInWar(target)) {
				unjustified = true;
				m_player.addUnjustifiedDead(target);
			}

			if (lastHit && m_player.hasCondition(CONDITION_INFIGHT)) {
				m_player.pzLocked = true;
				const auto &condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_configManager().getNumber(WHITE_SKULL_TIME), 0);
				m_player.addCondition(condition);
			}
		}
	}
	return unjustified;
}
void PlayerCombatEventComponent::addHuntingTaskKill(const std::shared_ptr<MonsterType> &mType) {
	m_player.m_preyComponent.addHuntingTaskKill(mType);
}
void PlayerCombatEventComponent::addBestiaryKill(const std::shared_ptr<MonsterType> &mType) {
	m_player.m_preyComponent.addBestiaryKill(mType);
}
void PlayerCombatEventComponent::addBosstiaryKill(const std::shared_ptr<MonsterType> &mType) {
	m_player.m_preyComponent.addBosstiaryKill(mType);
}
bool PlayerCombatEventComponent::onKilledMonster(const std::shared_ptr<Monster> &monster) {
	if (m_player.hasFlag(PlayerFlags_t::NotGenerateLoot)) {
		monster->setDropLoot(false);
	}
	if (monster->hasBeenSummoned()) {
		return false;
	}
	const auto &mType = monster->getMonsterType();
	if (mType == nullptr) {
		g_logger().error("[{}] Monster type is null.", __FUNCTION__);
		return false;
	}
	if (!monster->getSoulPit()) {
		addHuntingTaskKill(mType);
		addBestiaryKill(mType);
		addBosstiaryKill(mType);
	}
	return false;
}
void PlayerCombatEventComponent::gainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target)  {
	m_player.m_experienceComponent.gainExperience(gainExp, target);
}
void PlayerCombatEventComponent::onGainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target) {
	if (m_player.hasFlag(PlayerFlags_t::NotGainExperience)) {
		return;
	}

	if (target && !target->getPlayer() && m_player.m_party && m_player.m_party->isSharedExperienceActive() && m_player.m_party->isSharedExperienceEnabled()) {
		m_player.m_party->shareExperience(gainExp, target);
		// We will get a share of the experience through the sharing mechanism
		return;
	}

	m_player.Creature::onGainExperience(gainExp, target);
	gainExperience(gainExp, target);
}
void PlayerCombatEventComponent::onGainSharedExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target) {
	gainExperience(gainExp, target);
}
void PlayerCombatEventComponent::parseAttackRecvHazardSystem(CombatDamage &damage, const std::shared_ptr<Monster> &monster) {
	if (!monster || !monster->getHazard()) {
		return;
	}

	if (!g_configManager().getBoolean(TOGGLE_HAZARDSYSTEM)) {
		return;
	}

	if (damage.primary.type == COMBAT_HEALING) {
		return;
	}

	auto points = getHazardSystemPoints();
	if (m_player.m_party) {
		for (const auto &partyMember : m_player.m_party->getMembers()) {
			if (partyMember && partyMember->getHazardSystemPoints() < points) {
				points = partyMember->getHazardSystemPoints();
			}
		}

		if (m_player.m_party->getLeader() && m_player.m_party->getLeader()->getHazardSystemPoints() < points) {
			points = m_player.m_party->getLeader()->getHazardSystemPoints();
		}
	}

	if (points == 0) {
		return;
	}

	uint16_t stage = 0;
	auto chance = static_cast<uint16_t>(normal_random(1, 10000));
	auto critChance = g_configManager().getNumber(HAZARD_CRITICAL_CHANCE);
	// Critical chance
	if (monster->getHazardSystemCrit() && (m_player.lastHazardSystemCriticalHit + g_configManager().getNumber(HAZARD_CRITICAL_INTERVAL)) <= OTSYS_TIME() && chance <= critChance && !damage.critical) {
		damage.critical = true;
		damage.extension = true;
		damage.exString = "(Hazard)";

		stage = (points - 1) * static_cast<uint16_t>(g_configManager().getNumber(HAZARD_CRITICAL_MULTIPLIER));
		damage.primary.value += static_cast<int32_t>(std::ceil((static_cast<double>(damage.primary.value) * (5000 + stage)) / 10000));
		damage.secondary.value += static_cast<int32_t>(std::ceil((static_cast<double>(damage.secondary.value) * (5000 + stage)) / 10000));
		m_player.lastHazardSystemCriticalHit = OTSYS_TIME();
	}

	// To prevent from punish the player twice with critical + damage boost, just uncomment code from the if
	if (monster->getHazardSystemDamageBoost() /* && !damage.critical*/) {
		stage = points * static_cast<uint16_t>(g_configManager().getNumber(HAZARD_DAMAGE_MULTIPLIER));
		if (stage != 0) {
			damage.extension = true;
			damage.exString = "(Hazard)";
			damage.primary.value += static_cast<int32_t>(std::ceil((static_cast<double>(damage.primary.value) * stage) / 10000));
			if (damage.secondary.value != 0) {
				damage.secondary.value += static_cast<int32_t>(std::ceil((static_cast<double>(damage.secondary.value) * stage) / 10000));
			}
		}
	}
}

void PlayerCombatEventComponent::parseAttackDealtHazardSystem(CombatDamage &damage, const std::shared_ptr<Monster> &monster) const {
	if (!g_configManager().getBoolean(TOGGLE_HAZARDSYSTEM)) {
		return;
	}

	if (!monster || !monster->getHazard()) {
		return;
	}

	if (damage.primary.type == COMBAT_HEALING) {
		return;
	}

	auto points = getHazardSystemPoints();
	if (m_player.m_party) {
		for (const auto &partyMember : m_player.m_party->getMembers()) {
			if (partyMember && partyMember->getHazardSystemPoints() < points) {
				points = partyMember->getHazardSystemPoints();
			}
		}

		if (m_player.m_party->getLeader() && m_player.m_party->getLeader()->getHazardSystemPoints() < points) {
			points = m_player.m_party->getLeader()->getHazardSystemPoints();
		}
	}

	if (points == 0) {
		return;
	}

	// Dodge chance
	uint16_t stage;
	if (monster->getHazardSystemDodge()) {
		stage = points * g_configManager().getNumber(HAZARD_DODGE_MULTIPLIER);
		auto chance = static_cast<uint16_t>(normal_random(1, 10000));
		if (chance <= stage) {
			damage.primary.value = 0;
			damage.secondary.value = 0;
			damage.hazardDodge = true;
			return;
		}
	}
	if (monster->getHazardSystemDefenseBoost()) {
		stage = points * static_cast<uint16_t>(g_configManager().getNumber(HAZARD_DEFENSE_MULTIPLIER));
		if (stage != 0) {
			damage.exString = fmt::format("(hazard -{}%)", stage / 100.);
			damage.primary.value -= static_cast<int32_t>(std::ceil((static_cast<double>(damage.primary.value) * stage) / 10000));
			if (damage.secondary.value != 0) {
				damage.secondary.value -= static_cast<int32_t>(std::ceil((static_cast<double>(damage.secondary.value) * stage) / 10000));
			}
		}
	}
}

void PlayerCombatEventComponent::setHazardSystemPoints(int32_t count) {
	if (!g_configManager().getBoolean(TOGGLE_HAZARDSYSTEM)) {
		return;
	}
	m_player.addStorageValue(STORAGEVALUE_HAZARDCOUNT, std::max<int32_t>(0, std::min<int32_t>(0xFFFF, count)), true);
	m_player.reloadHazardSystemPointsCounter = true;
	if (count > 0) {
		m_player.setIcon("hazard", CreatureIcon(CreatureIconQuests_t::Hazard, count));
	} else {
		m_player.removeIcon("hazard");
	}
}

uint16_t PlayerCombatEventComponent::getHazardSystemPoints() const {
	const int32_t points = m_player.getStorageValue(STORAGEVALUE_HAZARDCOUNT);
	if (points <= 0) {
		return 0;
	}
	return static_cast<uint16_t>(std::max<int32_t>(0, std::min<int32_t>(0xFFFF, points)));
}
