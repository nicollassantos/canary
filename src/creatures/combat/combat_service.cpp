/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/combat/combat_service.hpp"

#include "config/configmanager.hpp"
#include "lua/creature/creatureevent.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/iobestiary.hpp"
#include "items/item.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "lua/creature/events.hpp"
#include "map/spectators.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "utils/tools.hpp"

void CombatService::sendBlockEffect(BlockType_t blockType, CombatType_t combatType, const Position &targetPos, const std::shared_ptr<Creature> &source) {
	if (blockType == BLOCK_DEFENSE) {
		game_.addMagicEffect(targetPos, CONST_ME_POFF);
	} else if (blockType == BLOCK_ARMOR) {
		game_.addMagicEffect(targetPos, CONST_ME_BLOCKHIT);
	} else if (blockType == BLOCK_DODGE) {
		game_.addMagicEffect(targetPos, CONST_ME_DODGE);
	} else if (blockType == BLOCK_IMMUNITY) {
		uint8_t hitEffect = 0;
		switch (combatType) {
			case COMBAT_UNDEFINEDDAMAGE: {
				return;
			}
			case COMBAT_ENERGYDAMAGE:
			case COMBAT_FIREDAMAGE:
			case COMBAT_PHYSICALDAMAGE:
			case COMBAT_ICEDAMAGE:
			case COMBAT_DEATHDAMAGE: {
				hitEffect = CONST_ME_BLOCKHIT;
				break;
			}
			case COMBAT_EARTHDAMAGE: {
				hitEffect = CONST_ME_GREEN_RINGS;
				break;
			}
			case COMBAT_HOLYDAMAGE: {
				hitEffect = CONST_ME_HOLYDAMAGE;
				break;
			}
			default: {
				hitEffect = CONST_ME_POFF;
				break;
			}
		}
		game_.addMagicEffect(targetPos, hitEffect);
	}

	if (blockType != BLOCK_NONE) {
		game_.sendSingleSoundEffect(targetPos, SoundEffect_t::NO_DAMAGE, source);
	}
}

bool CombatService::combatBlockHit(CombatDamage &damage, const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, bool checkDefense, bool checkArmor, bool field, bool condition /* = false */) {
	if (damage.primary.type == COMBAT_NONE && damage.secondary.type == COMBAT_NONE) {
		return true;
	}

	const auto &targetPlayer = target->getPlayer();
	if (targetPlayer && targetPlayer->isInGhostMode()) {
		return true;
	}

	if (damage.primary.value > 0 || damage.primary.type == COMBAT_AGONYDAMAGE) {
		return false;
	}

	// Skill dodge (ruse)
	if (targetPlayer) {
		auto chance = targetPlayer->getDodgeChance();
		if ((chance > 0 && uniform_random(0, 10000) < chance) || damage.hazardDodge) {
			sendBlockEffect(BLOCK_DODGE, damage.primary.type, target->getPosition(), attacker);
			targetPlayer->sendTextMessage(MESSAGE_ATTENTION, "You dodged an attack.");
			return true;
		}
	}

	bool canHeal = false;
	CombatDamage damageHeal;
	damageHeal.primary.type = COMBAT_HEALING;

	bool damageAbsorbMessage = false;
	bool damageIncreaseMessage = false;

	bool canReflect = false;
	CombatDamage damageReflected;
	CombatParams damageReflectedParams;

	BlockType_t primaryBlockType, secondaryBlockType;
	if (damage.primary.type != COMBAT_NONE) {
		damage.primary.value = -damage.primary.value;
		// Damage healing primary
		if (attacker) {
			if (target->getMonster()) {
				uint32_t primaryHealing = target->getMonster()->getHealingCombatValue(damage.primary.type);
				if (primaryHealing > 0) {
					damageHeal.primary.value = std::ceil((damage.primary.value) * (primaryHealing / 100.));
					canHeal = true;
				}
			}
			if (targetPlayer && attacker->getAbsorbPercent(damage.primary.type) != 0) {
				damageAbsorbMessage = true;
			}
			if (attacker->getPlayer() && attacker->getIncreasePercent(damage.primary.type) != 0) {
				damageIncreaseMessage = true;
			}
			damage.primary.value *= attacker->getBuff(BUFF_DAMAGEDEALT) / 100.;
		}
		damage.primary.value *= target->getBuff(BUFF_DAMAGERECEIVED) / 100.;

		if (!condition) {
			Combat::applyMantraAbsorb(targetPlayer, damage.primary.type, damage.primary.value);
			damage.primary.value = std::max<int32_t>(damage.primary.value, 0);
		}

		primaryBlockType = target->blockHit(attacker, damage.primary.type, damage.primary.value, checkDefense, checkArmor, field);

		damage.primary.value = -damage.primary.value;
		sendBlockEffect(primaryBlockType, damage.primary.type, target->getPosition(), attacker);
		// Damage reflection primary
		if (!damage.extension && attacker) {
			const auto &attackerMonster = attacker->getMonster();
			if (attackerMonster && targetPlayer && damage.primary.type != COMBAT_HEALING) {
				// Charm rune (target as player)
				const auto &mType = attackerMonster->getMonsterType();
				if (mType) {
					auto [activeCharm, _] = g_iobestiary().getCharmFromTarget(targetPlayer, mType);
					if (activeCharm == CHARM_PARRY) {
						const auto &charm = g_iobestiary().getBestiaryCharm(activeCharm);
						const auto charmTier = targetPlayer->getCharmTier(activeCharm);
						if (charm && charm->type == CHARM_DEFENSIVE && (charm->chance[charmTier] >= normal_random(1, 10000) / 100.0)) {
							g_iobestiary().parseCharmCombat(charm, targetPlayer, attacker, (damage.primary.value + damage.secondary.value));
						}
					}
				}
			}
			double_t primaryReflectPercent = target->getReflectPercent(damage.primary.type, true);
			int32_t primaryReflectFlat = target->getReflectFlat(damage.primary.type, true);
			if (primaryReflectPercent > 0 || primaryReflectFlat > 0) {
				int32_t distanceX = Position::getDistanceX(target->getPosition(), attacker->getPosition());
				int32_t distanceY = Position::getDistanceY(target->getPosition(), attacker->getPosition());
				if (target->getMonster() || damage.primary.type != COMBAT_PHYSICALDAMAGE || primaryReflectPercent > 0 || std::max(distanceX, distanceY) < 2) {
					int32_t reflectFlat = -static_cast<int32_t>(primaryReflectFlat);
					int32_t reflectPercent = std::ceil(damage.primary.value * primaryReflectPercent / 100.);
					int32_t reflectLimit = std::ceil(attacker->getMaxHealth() * 0.01);
					damageReflected.primary.value = std::max(-reflectLimit, reflectFlat + reflectPercent);
					if (targetPlayer) {
						damageReflected.primary.type = COMBAT_NEUTRALDAMAGE;
					} else {
						damageReflected.primary.type = damage.primary.type;
					}
					if (!damageReflected.exString.empty()) {
						damageReflected.exString += ", ";
					}
					damageReflected.extension = true;
					damageReflected.exString += " (damage reflection)";
					damageReflectedParams.combatType = damage.primary.type;
					damageReflectedParams.aggressive = true;
					canReflect = true;
				}
			}
		}
	} else {
		primaryBlockType = BLOCK_NONE;
	}

	if (damage.secondary.type != COMBAT_NONE) {
		damage.secondary.value = -damage.secondary.value;
		// Damage healing secondary
		if (attacker && target->getMonster()) {
			uint32_t secondaryHealing = target->getMonster()->getHealingCombatValue(damage.secondary.type);
			if (secondaryHealing > 0) {
				damageHeal.primary.value += std::ceil((damage.secondary.value) * (secondaryHealing / 100.));
				canHeal = true;
			}
			if (targetPlayer && attacker->getAbsorbPercent(damage.secondary.type) != 0) {
				damageAbsorbMessage = true;
			}
			if (attacker->getPlayer() && attacker->getIncreasePercent(damage.secondary.type) != 0) {
				damageIncreaseMessage = true;
			}
			damage.secondary.value *= attacker->getBuff(BUFF_DAMAGEDEALT) / 100.;
		}
		damage.secondary.value *= target->getBuff(BUFF_DAMAGERECEIVED) / 100.;

		if (!condition) {
			Combat::applyMantraAbsorb(targetPlayer, damage.secondary.type, damage.secondary.value);
			damage.secondary.value = std::max<int32_t>(damage.secondary.value, 0);
		}

		secondaryBlockType = target->blockHit(attacker, damage.secondary.type, damage.secondary.value, false, false, field);

		damage.secondary.value = -damage.secondary.value;
		sendBlockEffect(secondaryBlockType, damage.secondary.type, target->getPosition(), attacker);

		if (!damage.extension && attacker && target->getMonster()) {
			int32_t secondaryReflectPercent = target->getReflectPercent(damage.secondary.type, true);
			int32_t secondaryReflectFlat = target->getReflectFlat(damage.secondary.type, true);
			if (secondaryReflectPercent > 0 || secondaryReflectFlat > 0) {
				if (!canReflect) {
					int32_t reflectFlat = -static_cast<int32_t>(secondaryReflectFlat);
					int32_t reflectPercent = std::ceil(damage.primary.value * secondaryReflectPercent / 100.);
					int32_t reflectLimit = std::ceil(attacker->getMaxHealth() * 0.01);
					damageReflected.primary.value = std::max(-reflectLimit, reflectFlat + reflectPercent);
					damageReflected.primary.type = damage.secondary.type;
					if (!damageReflected.exString.empty()) {
						damageReflected.exString += ", ";
					}
					damageReflected.extension = true;
					damageReflected.exString += " (damage reflection)";
					damageReflectedParams.combatType = damage.primary.type;
					damageReflectedParams.aggressive = true;
					canReflect = true;
				} else {
					damageReflected.secondary.type = damage.secondary.type;
					damageReflected.primary.value = std::ceil(damage.secondary.value * secondaryReflectPercent / 100.) + std::max(-static_cast<int32_t>(std::ceil(attacker->getMaxHealth() * 0.01)), std::max(damage.secondary.value, -(static_cast<int32_t>(secondaryReflectFlat))));
				}
			}
		}
	} else {
		secondaryBlockType = BLOCK_NONE;
	}

	if (damage.primary.type == COMBAT_HEALING) {
		damage.primary.value *= target->getBuff(BUFF_HEALINGRECEIVED) / 100.;
	}

	if (damageAbsorbMessage) {
		if (!damage.exString.empty()) {
			damage.exString += ", ";
		}
		damage.exString += "active elemental resiliance";
	}

	if (damageIncreaseMessage) {
		if (!damage.exString.empty()) {
			damage.exString += ", ";
		}
		damage.exString += "active elemental amplification";
	}

	if (canReflect) {
		Combat::doCombatHealth(target, attacker, damageReflected, damageReflectedParams);
	}
	if (canHeal) {
		combatChangeHealth(nullptr, target, damageHeal);
	}
	return (primaryBlockType != BLOCK_NONE) && (secondaryBlockType != BLOCK_NONE);
}

void CombatService::combatGetTypeInfo(CombatType_t combatType, const std::shared_ptr<Creature> &target, TextColor_t &color, uint16_t &effect) {
	switch (combatType) {
		case COMBAT_PHYSICALDAMAGE: {
			std::shared_ptr<Item> splash = nullptr;
			switch (target->getRace()) {
				case RACE_VENOM:
					color = TEXTCOLOR_LIGHTGREEN;
					effect = CONST_ME_HITBYPOISON;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_SLIME);
					break;
				case RACE_BLOOD:
					color = TEXTCOLOR_RED;
					effect = CONST_ME_DRAWBLOOD;
					if (std::shared_ptr<Tile> tile = target->getTile()) {
						if (!tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
							splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_BLOOD);
						}
					}
					break;
				case RACE_INK:
					color = TEXTCOLOR_LIGHTGREY;
					effect = CONST_ME_HITAREA;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_INK);
					break;
				case RACE_CHOCOLATE:
					color = TEXTCOLOR_LIGHTGREY;
					effect = CONST_ME_CACAO;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_CHOCOLATE);
					break;
				case RACE_CANDY:
					color = TEXTCOLOR_DARKRED;
					effect = CONST_ME_SIRUP;
					splash = Item::CreateItem(ITEM_SMALLSPLASH, FLUID_CANDY);
					break;
				case RACE_UNDEAD:
					color = TEXTCOLOR_LIGHTGREY;
					effect = CONST_ME_HITAREA;
					break;
				case RACE_FIRE:
					color = TEXTCOLOR_ORANGE;
					effect = CONST_ME_DRAWBLOOD;
					break;
				case RACE_ENERGY:
					color = TEXTCOLOR_PURPLE;
					effect = CONST_ME_ENERGYHIT;
					break;
				default:
					color = TEXTCOLOR_NONE;
					effect = CONST_ME_NONE;
					break;
			}

			if (splash) {
				game_.internalAddItem(target->getTile(), splash, INDEX_WHEREEVER, FLAG_NOLIMIT);
				splash->startDecaying();
			}

			break;
		}

		case COMBAT_ENERGYDAMAGE: {
			color = TEXTCOLOR_PURPLE;
			effect = CONST_ME_ENERGYHIT;
			break;
		}

		case COMBAT_EARTHDAMAGE: {
			color = TEXTCOLOR_LIGHTGREEN;
			effect = CONST_ME_GREEN_RINGS;
			break;
		}

		case COMBAT_DROWNDAMAGE: {
			color = TEXTCOLOR_LIGHTBLUE;
			effect = CONST_ME_LOSEENERGY;
			break;
		}
		case COMBAT_FIREDAMAGE: {
			color = TEXTCOLOR_ORANGE;
			effect = CONST_ME_HITBYFIRE;
			break;
		}
		case COMBAT_ICEDAMAGE: {
			color = TEXTCOLOR_SKYBLUE;
			effect = CONST_ME_ICEATTACK;
			break;
		}
		case COMBAT_HOLYDAMAGE: {
			color = TEXTCOLOR_YELLOW;
			effect = CONST_ME_HOLYDAMAGE;
			break;
		}
		case COMBAT_DEATHDAMAGE: {
			color = TEXTCOLOR_DARKRED;
			effect = CONST_ME_SMALLCLOUDS;
			break;
		}
		case COMBAT_LIFEDRAIN: {
			color = TEXTCOLOR_RED;
			effect = CONST_ME_MAGIC_RED;
			break;
		}
		case COMBAT_AGONYDAMAGE: {
			color = TEXTCOLOR_DARKBROWN;
			effect = CONST_ME_AGONY;
			break;
		}
		case COMBAT_NEUTRALDAMAGE: {
			color = TEXTCOLOR_NEUTRALDAMAGE;
			effect = CONST_ME_REDSMOKE;
			break;
		}
		default: {
			color = TEXTCOLOR_NONE;
			effect = CONST_ME_NONE;
			break;
		}
	}
}

void CombatService::handleHazardSystemAttack(CombatDamage &damage, const std::shared_ptr<Player> &player, const std::shared_ptr<Monster> &monster, bool isPlayerAttacker) {
	if (damage.primary.value != 0 && monster->getHazard()) {
		if (isPlayerAttacker) {
			player->parseAttackDealtHazardSystem(damage, monster);
		} else {
			player->parseAttackRecvHazardSystem(damage, monster);
		}
	}
}

void CombatService::notifySpectators(const CreatureVector &spectators, const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster) {
	if (!spectators.empty()) {
		for (const auto &spectator : spectators) {
			if (!spectator) {
				continue;
			}

			const auto tmpPlayer = spectator->getPlayer();
			if (!tmpPlayer || tmpPlayer->getPosition().z != targetPos.z) {
				continue;
			}

			std::stringstream ss;
			ss << ucfirst(targetMonster->getNameDescription()) << " has dodged";
			if (tmpPlayer == attackerPlayer) {
				ss << " your attack.";
				attackerPlayer->sendCancelMessage(ss.str());
				ss << " (Hazard)";
				attackerPlayer->sendTextMessage(MESSAGE_DAMAGE_OTHERS, ss.str());
			} else {
				ss << " an attack by " << attackerPlayer->getName() << ". (Hazard)";
				tmpPlayer->sendTextMessage(MESSAGE_DAMAGE_OTHERS, ss.str());
			}
		}
		game_.addMagicEffect(targetPos, CONST_ME_DODGE);
	}
}

void CombatService::applyPvPDamage(CombatDamage &damage, const std::shared_ptr<Player> &attacker, const std::shared_ptr<Player> &target) {
	float targetDamageReceivedMultiplier = target->vocation->pvpDamageReceivedMultiplier;
	float attackerDamageDealtMultiplier = attacker->vocation->pvpDamageDealtMultiplier;
	float levelDifferenceDamageMultiplier = pvpLevelDifferenceDamageMultiplier(attacker, target);

	float pvpDamageMultiplier = targetDamageReceivedMultiplier * attackerDamageDealtMultiplier * levelDifferenceDamageMultiplier;

	damage.primary.value = std::round(damage.primary.value * pvpDamageMultiplier);
	damage.secondary.value = std::round(damage.secondary.value * pvpDamageMultiplier);
}

float CombatService::pvpLevelDifferenceDamageMultiplier(const std::shared_ptr<Player> &attacker, const std::shared_ptr<Player> &target) {
	int32_t levelDifference = target->getLevel() - attacker->getLevel();
	levelDifference = std::abs(levelDifference);
	bool isLowerLevel = target->getLevel() < attacker->getLevel();

	int32_t maxLevelDifference = g_configManager().getNumber(PVP_MAX_LEVEL_DIFFERENCE);
	levelDifference = std::min(levelDifference, maxLevelDifference);

	float levelDiffRate = 1.0;
	if (isLowerLevel) {
		float rateDamageTakenByLevel = g_configManager().getFloat(PVP_RATE_DAMAGE_TAKEN_PER_LEVEL) / 100;
		levelDiffRate += levelDifference * rateDamageTakenByLevel;
	} else {
		float rateDamageReductionByLevel = g_configManager().getFloat(PVP_RATE_DAMAGE_REDUCTION_PER_LEVEL) / 100;
		levelDiffRate -= levelDifference * rateDamageReductionByLevel;
	}

	return levelDiffRate;
}

void CombatService::applyWheelOfDestinyHealing(CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer, std::shared_ptr<Creature> target) {
	damage.primary.value += (damage.primary.value * damage.healingMultiplier) / 100.;

	if (attackerPlayer) {
		damage.primary.value += attackerPlayer->wheel().getStat(WheelStat_t::HEALING);

		if (damage.secondary.value != 0) {
			damage.secondary.value += attackerPlayer->wheel().getStat(WheelStat_t::HEALING);
		}

		if (damage.healingLink > 0) {
			CombatDamage tmpDamage;
			tmpDamage.primary.value = (damage.primary.value * damage.healingLink) / 100;
			tmpDamage.primary.type = COMBAT_HEALING;
			combatChangeHealth(attackerPlayer, attackerPlayer, tmpDamage);
		}

		if (attackerPlayer->wheel().getInstant("Blessing of the Grove")) {
			damage.primary.value += (damage.primary.value * attackerPlayer->wheel().checkBlessingGroveHealingByTarget(target)) / 100.;
		}
	}
}

void CombatService::applyWheelOfDestinyEffectsToDamage(CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Creature> &target) const {
	if (damage.primary.value == 0 && damage.secondary.value == 0) {
		return;
	}

	if (damage.damageMultiplier > 0) {
		damage.primary.value += (damage.primary.value * (damage.damageMultiplier)) / 100.;
		damage.secondary.value += (damage.secondary.value * (damage.damageMultiplier)) / 100.;
	}

	if (attackerPlayer) {
		damage.primary.value -= attackerPlayer->wheel().getStat(WheelStat_t::DAMAGE);
		if (damage.secondary.value != 0) {
			damage.secondary.value -= attackerPlayer->wheel().getStat(WheelStat_t::DAMAGE);
		}
		if (damage.instantSpellName == "Ice Burst" || damage.instantSpellName == "Terra Burst") {
			int32_t damageBonus = attackerPlayer->wheel().checkTwinBurstByTarget(target);
			if (damageBonus != 0) {
				damage.primary.value += (damage.primary.value * damageBonus) / 100.;
				damage.secondary.value += (damage.secondary.value * damageBonus) / 100.;
			}
		}
		if (damage.instantSpellName == "Executioner's Throw") {
			int32_t damageBonus = attackerPlayer->wheel().checkExecutionersThrow(target);
			if (damageBonus != 0) {
				damage.primary.value += (damage.primary.value * damageBonus) / 100.;
				damage.secondary.value += (damage.secondary.value * damageBonus) / 100.;
			}
		}
		if (damage.instantSpellName == "Divine Grenade") {
			int32_t damageBonus = attackerPlayer->wheel().checkDivineGrenade(target);
			if (damageBonus != 0) {
				damage.primary.value += (damage.primary.value * damageBonus) / 100.;
				damage.secondary.value += (damage.secondary.value * damageBonus) / 100.;
			}
		}
	}
}

int32_t CombatService::applyHealthChange(const CombatDamage &damage, const std::shared_ptr<Creature> &target) const {
	int32_t targetHealth = target->getHealth();

	if (std::shared_ptr<Player> targetPlayer = target->getPlayer()) {
		if (targetPlayer->wheel().getInstant("Gift of Life") && targetPlayer->wheel().getGiftOfCooldown() == 0 && (damage.primary.value + damage.secondary.value) >= targetHealth) {
			int32_t overkillMultiplier = (damage.primary.value + damage.secondary.value) - targetHealth;
			overkillMultiplier = (overkillMultiplier * 100) / targetPlayer->getMaxHealth();
			if (overkillMultiplier <= targetPlayer->wheel().getGiftOfLifeValue()) {
				targetPlayer->wheel().checkGiftOfLife();
				targetHealth = target->getHealth();
			}
		}
	}
	return targetHealth;
}

bool CombatService::combatChangeHealth(const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, CombatDamage &damage, bool isEvent /*= false*/) {
	using namespace std;
	const Position &targetPos = target->getPosition();
	if (damage.primary.value > 0) {
		if (target->getHealth() <= 0) {
			return false;
		}

		std::shared_ptr<Player> attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}

		auto targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (const auto &creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		// Wheel of destiny combat healing
		applyWheelOfDestinyHealing(damage, attackerPlayer, target);

		auto realHealthChange = target->getHealth();
		target->gainHealth(attacker, damage.primary.value);
		realHealthChange = target->getHealth() - realHealthChange;

		if (realHealthChange > 0 && !target->isInGhostMode()) {
			if (targetPlayer) {
				targetPlayer->updateImpactTracker(COMBAT_HEALING, realHealthChange);
			}

			// Party hunt analyzer
			if (auto party = attackerPlayer ? attackerPlayer->getParty() : nullptr) {
				party->addPlayerHealing(attackerPlayer, realHealthChange);
			}

			std::stringstream ss;

			ss << realHealthChange << (realHealthChange != 1 ? " hitpoints." : " hitpoint.");
			std::string damageString = ss.str();

			std::string spectatorMessage;

			TextMessage message;
			message.position = targetPos;
			message.primary.value = realHealthChange;
			message.primary.color = TEXTCOLOR_PASTELRED;

			for (const auto &spectator : Spectators().find<Player>(targetPos)) {
				const auto &tmpPlayer = spectator->getPlayer();
				if (!tmpPlayer) {
					continue;
				}

				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					ss.str({});
					ss << "You heal " << target->getNameDescription() << " for " << damageString;
					message.type = MESSAGE_HEALED;
					message.text = ss.str();
				} else if (tmpPlayer == targetPlayer) {
					ss.str({});
					if (!attacker) {
						ss << "You were healed";
					} else if (targetPlayer == attackerPlayer) {
						ss << "You heal yourself";
					} else {
						ss << "You were healed by " << attacker->getNameDescription();
					}
					ss << " for " << damageString;
					message.type = MESSAGE_HEALED;
					message.text = ss.str();
				} else {
					if (spectatorMessage.empty()) {
						ss.str({});
						if (!attacker && target) {
							ss << ucfirst(target->getNameDescription()) << " was healed";
						} else {
							ss << ucfirst(attacker->getNameDescription()) << " healed ";
							if (attacker == target) {
								ss << (targetPlayer ? targetPlayer->getReflexivePronoun() : "itself");
							} else if (target) {
								ss << target->getNameDescription();
							}
						}
						ss << " for " << damageString;
						spectatorMessage = ss.str();
					}
					message.type = MESSAGE_HEALED_OTHERS;
					message.text = spectatorMessage;
				}
				tmpPlayer->sendTextMessage(message);
			}
		}
	} else {
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				game_.addMagicEffect(targetPos, CONST_ME_POFF);
			}
			return true;
		}

		const auto &attackerPlayer = attacker ? attacker->getPlayer() : nullptr;

		const auto &targetPlayer = target->getPlayer();
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto events = target->getCreatureEvents(CREATURE_EVENT_HEALTHCHANGE);
			if (!events.empty()) {
				for (const auto &creatureEvent : events) {
					creatureEvent->executeHealthChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeHealth(attacker, target, damage);
			}
		}

		// Wheel of destiny apply combat effects
		applyWheelOfDestinyEffectsToDamage(damage, attackerPlayer, target);

		damage.primary.value = std::abs(damage.primary.value);
		damage.secondary.value = std::abs(damage.secondary.value);

		std::shared_ptr<Monster> targetMonster;
		if (target && target->getMonster()) {
			targetMonster = target->getMonster();
		} else {
			targetMonster = nullptr;
		}

		std::shared_ptr<Monster> attackerMonster;
		if (attacker && attacker->getMonster()) {
			attackerMonster = attacker->getMonster();
		} else {
			attackerMonster = nullptr;
		}

		if (attacker && attackerPlayer && damage.extension == false && damage.origin == ORIGIN_RANGED && target == attackerPlayer->getAttackedCreature()) {
			const Position &attackerPos = attacker->getPosition();
			if (targetPos.z == attackerPos.z) {
				int32_t distanceX = Position::getDistanceX(targetPos, attackerPos);
				int32_t distanceY = Position::getDistanceY(targetPos, attackerPos);
				int32_t damageX = attackerPlayer->getPerfectShotDamage(distanceX, true);
				int32_t damageY = attackerPlayer->getPerfectShotDamage(distanceY, true);
				const auto &item = attackerPlayer->getWeapon();
				if (item && item->getWeaponType() == WEAPON_DISTANCE) {
					const auto &quiver = attackerPlayer->getInventoryItem(CONST_SLOT_RIGHT);
					if (quiver && quiver->getWeaponType()) {
						if (quiver->getPerfectShotRange() == distanceX) {
							damageX -= quiver->getPerfectShotDamage();
						} else if (quiver->getPerfectShotRange() == distanceY) {
							damageY -= quiver->getPerfectShotDamage();
						}
					}
				}
				if (damageX != 0 || damageY != 0) {
					int32_t totalDamage = damageX;
					if (distanceX != distanceY) {
						totalDamage += damageY;
					}
					damage.primary.value += totalDamage;
					if (!damage.exString.empty()) {
						damage.exString += ", ";
					}
					damage.exString += "perfect shot";
				}
			}
		}

		TextMessage message;
		message.position = targetPos;

		if (!isEvent) {
			g_events().eventCreatureOnDrainHealth(target, attacker, damage.primary.type, damage.primary.value, damage.secondary.type, damage.secondary.value, message.primary.color, message.secondary.color);
			g_callbacks().executeCallback(EventCallback_t::creatureOnDrainHealth, target, attacker, std::ref(damage.primary.type), std::ref(damage.primary.value), std::ref(damage.secondary.type), std::ref(damage.secondary.value), std::ref(message.primary.color), std::ref(message.secondary.color));
		}
		if (damage.origin != ORIGIN_NONE && attacker && damage.primary.type != COMBAT_HEALING) {
			damage.primary.value *= attacker->getBuff(BUFF_DAMAGEDEALT) / 100.;
			damage.secondary.value *= attacker->getBuff(BUFF_DAMAGEDEALT) / 100.;
		}
		if (damage.origin != ORIGIN_NONE && target && damage.primary.type != COMBAT_HEALING) {
			damage.primary.value *= target->getBuff(BUFF_DAMAGERECEIVED) / 100.;
			damage.secondary.value *= target->getBuff(BUFF_DAMAGERECEIVED) / 100.;
		}
		auto healthChange = damage.primary.value + damage.secondary.value;
		if (healthChange == 0) {
			return true;
		}

		auto spectators = Spectators().find<Player>(targetPos, true);

		if (targetPlayer && attackerMonster) {
			handleHazardSystemAttack(damage, targetPlayer, attackerMonster, false);
		} else if (attackerPlayer && targetMonster) {
			handleHazardSystemAttack(damage, attackerPlayer, targetMonster, true);

			if ((damage.primary.value == 0 && damage.secondary.value == 0) || damage.hazardDodge) {
				notifySpectators(spectators.data(), targetPos, attackerPlayer, targetMonster);
				return true;
			}
		}

		if (damage.fatal) {
			game_.addMagicEffect(spectators.data(), targetPos, CONST_ME_FATAL);
		} else if (damage.critical) {
			game_.addMagicEffect(spectators.data(), targetPos, CONST_ME_CRITICAL_DAMAGE);
		}

		if (!damage.extension && attackerMonster && targetPlayer) {
			// Charm rune (target as player)
			auto [major, minor] = g_iobestiary().getCharmFromTarget(targetPlayer, attackerMonster->getMonsterType());
			if (minor != CHARM_NONE && minor != CHARM_CLEANSE) {
				const auto &charm = g_iobestiary().getBestiaryCharm(minor);
				const auto charmTier = targetPlayer->getCharmTier(minor);
				if (charm && charm->type == CHARM_DEFENSIVE && charm->chance[charmTier] >= normal_random(1, 10000) / 100.0) {
					g_iobestiary().parseCharmCombat(charm, targetPlayer, attacker, (damage.primary.value + damage.secondary.value));
				}
			}

			if (major != CHARM_NONE) {
				const auto &charm = g_iobestiary().getBestiaryCharm(major);
				const auto charmTier = targetPlayer->getCharmTier(major);
				if (charm && charm->type == CHARM_DEFENSIVE && charm->chance[charmTier] >= normal_random(1, 10000) / 100.0) {
					g_iobestiary().parseCharmCombat(charm, targetPlayer, attacker, (damage.primary.value + damage.secondary.value));
					if (charm->id == CHARM_DODGE) {
						return true;
					}
				}
			}
		}

		std::string attackMsg = fmt::format("{} attack", damage.critical ? "critical " : " ");
		std::stringstream ss;

		if (target->hasCondition(CONDITION_MANASHIELD) && damage.primary.type != COMBAT_UNDEFINEDDAMAGE) {
			int32_t manaDamage = std::min<int32_t>(target->getMana(), healthChange);
			uint32_t manaShield = target->getManaShield();
			if (manaShield > 0) {
				if (manaShield > manaDamage) {
					target->setManaShield(manaShield - manaDamage);
					manaShield = manaShield - manaDamage;
				} else {
					manaDamage = manaShield;
					target->removeCondition(CONDITION_MANASHIELD);
					manaShield = 0;
				}
			}
			if (manaDamage != 0) {
				if (damage.origin != ORIGIN_NONE) {
					const auto events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
					if (!events.empty()) {
						for (const auto &creatureEvent : events) {
							creatureEvent->executeManaChange(target, attacker, damage);
						}
						healthChange = damage.primary.value + damage.secondary.value;
						if (healthChange == 0) {
							return true;
						}
						manaDamage = std::min<int32_t>(target->getMana(), healthChange);
					}
				}

				target->drainMana(attacker, manaDamage);

				if (target->getMana() == 0 && manaShield > 0) {
					target->removeCondition(CONDITION_MANASHIELD);
				}

				game_.addMagicEffect(spectators.data(), targetPos, CONST_ME_LOSEENERGY);

				std::string damageString = std::to_string(manaDamage);

				std::string spectatorMessage;

				message.primary.value = manaDamage;
				message.primary.color = TEXTCOLOR_BLUE;

				for (const auto &spectator : spectators) {
					const auto &tmpPlayer = spectator->getPlayer();
					if (!tmpPlayer || tmpPlayer->getPosition().z != targetPos.z) {
						continue;
					}

					if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
						ss.str({});
						ss << ucfirst(target->getNameDescription()) << " loses " << damageString + " mana due to your " << attackMsg << ".";

						if (!damage.exString.empty()) {
							ss << " (" << damage.exString << ")";
						}
						message.type = MESSAGE_DAMAGE_DEALT;
						message.text = ss.str();
					} else if (tmpPlayer == targetPlayer) {
						ss.str({});
						ss << "You lose " << damageString << " mana";
						if (!attacker) {
							ss << '.';
						} else if (targetPlayer == attackerPlayer) {
							ss << " due to your own " << attackMsg << ".";
						} else {
							ss << " due to an " << attackMsg << " by " << attacker->getNameDescription() << '.';
						}
						message.type = MESSAGE_DAMAGE_RECEIVED;
						message.text = ss.str();
					} else {
						if (spectatorMessage.empty()) {
							ss.str({});
							ss << ucfirst(target->getNameDescription()) << " loses " << damageString + " mana";
							if (attacker) {
								ss << " due to ";
								if (attacker == target) {
									ss << (targetPlayer ? targetPlayer->getPossessivePronoun() : "its") << " own attack";
								} else {
									ss << "an " << attackMsg << " by " << attacker->getNameDescription();
								}
							}
							ss << '.';
							spectatorMessage = ss.str();
						}
						message.type = MESSAGE_DAMAGE_OTHERS;
						message.text = spectatorMessage;
					}
					tmpPlayer->sendTextMessage(message);
				}

				damage.primary.value -= manaDamage;
				if (damage.primary.value < 0) {
					damage.secondary.value = std::max<int32_t>(0, damage.secondary.value + damage.primary.value);
					damage.primary.value = 0;
				}

				if (attackerPlayer) {
					attackerPlayer->updateImpactTracker(damage.primary.type, damage.primary.value);
					if (damage.secondary.type != COMBAT_NONE) {
						attackerPlayer->updateImpactTracker(damage.secondary.type, damage.secondary.value);
					}
				}

				if (targetPlayer) {
					targetPlayer->updateImpactTracker(damage.primary.type, manaDamage);
					if (damage.secondary.type != COMBAT_NONE) {
						targetPlayer->updateImpactTracker(damage.secondary.type, damage.secondary.value);
					}
				}
			}
		}

		auto realDamage = damage.primary.value + damage.secondary.value;
		if (realDamage == 0) {
			return true;
		}

		// Apply Custom PvP Damage (must be placed here to avoid recursive calls)
		if (attackerPlayer && targetPlayer) {
			applyPvPDamage(damage, attackerPlayer, targetPlayer);
		}

		auto targetHealth = target->getHealth();
		realDamage = std::min<int32_t>(targetHealth, damage.primary.value + damage.secondary.value);
		if (realDamage == 0) {
			return true;
		} else if (realDamage >= targetHealth) {
			for (const auto &creatureEvent : target->getCreatureEvents(CREATURE_EVENT_PREPAREDEATH)) {
				if (!creatureEvent->executeOnPrepareDeath(target, attacker, std::ref(realDamage))) {
					return false;
				}
			}
		}

		targetHealth = applyHealthChange(damage, target);
		if (damage.primary.value >= targetHealth) {
			damage.primary.value = targetHealth;
			damage.secondary.value = 0;
		} else if (damage.secondary.value) {
			damage.secondary.value = std::min<int32_t>(damage.secondary.value, targetHealth - damage.primary.value);
		}

		target->drainHealth(attacker, realDamage);
		if (realDamage > 0 && targetMonster) {
			if (targetMonster->israndomStepping()) {
				targetMonster->setIgnoreFieldDamage(true);
			}
		}

		if (spectators.empty()) {
			spectators.find<Player>(targetPos, true);
		}

		Game::addCreatureHealth(spectators.data(), target);

		sendDamageMessageAndEffects(
			attacker,
			target,
			damage,
			targetPos,
			attackerPlayer,
			targetPlayer,
			message,
			spectators.data(),
			realDamage
		);

		if (attackerPlayer) {
			if (!damage.extension && damage.origin != ORIGIN_CONDITION) {
				applyCharmRune(targetMonster, attackerPlayer, target, realDamage);
				applyLifeLeech(attackerPlayer, targetMonster, target, damage, realDamage);
				applyManaLeech(attackerPlayer, targetMonster, target, damage, realDamage);
			}
			updatePlayerPartyHuntAnalyzer(damage, attackerPlayer);
		}
	}

	return true;
}

void CombatService::updatePlayerPartyHuntAnalyzer(const CombatDamage &damage, const std::shared_ptr<Player> &player) const {
	if (!player) {
		return;
	}

	if (auto party = player->getParty()) {
		if (damage.primary.value != 0) {
			party->addPlayerDamage(player, damage.primary.value);
		}
		if (damage.secondary.value != 0) {
			party->addPlayerDamage(player, damage.secondary.value);
		}
	}
}

void CombatService::sendDamageMessageAndEffects(
	const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
	const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Player> &targetPlayer,
	TextMessage &message, const CreatureVector &spectators, int32_t realDamage
) {
	message.primary.value = damage.primary.value;
	message.secondary.value = damage.secondary.value;

	sendEffects(target, damage, targetPos, message, spectators);

	if (shouldSendMessage(message)) {
		sendMessages(attacker, target, damage, targetPos, attackerPlayer, targetPlayer, message, spectators, realDamage);
	}
}

bool CombatService::shouldSendMessage(const TextMessage &message) const {
	return message.primary.color != TEXTCOLOR_NONE || message.secondary.color != TEXTCOLOR_NONE;
}

void CombatService::sendMessages(
	const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
	const Position &targetPos, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Player> &targetPlayer,
	TextMessage &message, const CreatureVector &spectators, int32_t realDamage
) const {
	if (attackerPlayer) {
		attackerPlayer->updateImpactTracker(damage.primary.type, damage.primary.value);
		if (damage.secondary.type != COMBAT_NONE) {
			attackerPlayer->updateImpactTracker(damage.secondary.type, damage.secondary.value);
		}
	}
	if (targetPlayer) {
		std::string cause = "(other)";
		if (attacker) {
			cause = attacker->getName();
		}

		targetPlayer->updateInputAnalyzer(damage.primary.type, damage.primary.value, cause);
		if (attackerPlayer) {
			if (damage.secondary.type != COMBAT_NONE) {
				attackerPlayer->updateInputAnalyzer(damage.secondary.type, damage.secondary.value, cause);
			}
		}
	}
	std::stringstream ss;

	ss << realDamage << (realDamage != 1 ? " hitpoints" : " hitpoint");
	std::string damageString = ss.str();

	std::string spectatorMessage;

	for (const std::shared_ptr<Creature> &spectator : spectators) {
		std::shared_ptr<Player> tmpPlayer = spectator->getPlayer();
		if (!tmpPlayer || tmpPlayer->getPosition().z != targetPos.z) {
			continue;
		}

		if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
			const auto &boots = tmpPlayer->getInventoryItem(CONST_SLOT_FEET);
			bool amplifiedFatal = boots ? boots->getTier() > 0 : false;
			buildMessageAsAttacker(target, damage, message, ss, damageString, amplifiedFatal, attackerPlayer);
		} else if (tmpPlayer == targetPlayer) {
			buildMessageAsTarget(attacker, damage, attackerPlayer, targetPlayer, message, ss, damageString);
		} else {
			buildMessageAsSpectator(attacker, target, damage, targetPlayer, message, ss, damageString, spectatorMessage);
		}
		tmpPlayer->sendTextMessage(message);
	}
}

void CombatService::buildMessageAsSpectator(
	const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, const CombatDamage &damage,
	const std::shared_ptr<Player> &targetPlayer, TextMessage &message, std::stringstream &ss,
	const std::string &damageString, std::string &spectatorMessage
) const {
	if (spectatorMessage.empty()) {
		ss.str({});
		auto attackMsg = damage.critical ? "critical " : "";
		auto article = damage.critical ? "a" : "an";
		ss << ucfirst(target->getNameDescription()) << " loses " << damageString;
		if (attacker) {
			ss << " due to ";
			if (attacker == target) {
				if (targetPlayer) {
					ss << targetPlayer->getPossessivePronoun() << " own " << attackMsg << "attack";
				} else {
					ss << "its own " << attackMsg << "attack";
				}
			} else {
				ss << article << " " << attackMsg << "attack by " << attacker->getNameDescription();
			}
		}
		ss << '.';
		if (damage.extension) {
			ss << " " << damage.exString;
		}
		spectatorMessage = ss.str();
	}

	message.type = MESSAGE_DAMAGE_OTHERS;
	message.text = spectatorMessage;
}

void CombatService::buildMessageAsTarget(
	const std::shared_ptr<Creature> &attacker, const CombatDamage &damage, const std::shared_ptr<Player> &attackerPlayer,
	const std::shared_ptr<Player> &targetPlayer, TextMessage &message, std::stringstream &ss,
	const std::string &damageString
) const {
	ss.str({});
	const auto &monster = attacker ? attacker->getMonster() : nullptr;
	bool handleSoulPit = monster ? monster->getSoulPit() && monster->getForgeStack() == 40 : false;

	std::string attackMsg = damage.critical && !handleSoulPit ? "critical " : "";
	std::string article = damage.critical && !handleSoulPit ? "a" : "an";

	ss << "You lose " << damageString;
	if (!attacker) {
		ss << '.';
	} else if (targetPlayer == attackerPlayer) {
		ss << " due to your own " << attackMsg << "attack.";
	} else {
		ss << " due to " << article << " " << attackMsg << "attack by " << attacker->getNameDescription() << '.';
	}
	if (damage.extension) {
		ss << " " << damage.exString;
	}
	if (handleSoulPit && damage.critical) {
		ss << " (Soulpit Crit)";
	}
	message.type = MESSAGE_DAMAGE_RECEIVED;
	message.text = ss.str();
}

void CombatService::buildMessageAsAttacker(
	const std::shared_ptr<Creature> &target, const CombatDamage &damage, TextMessage &message,
	std::stringstream &ss, const std::string &damageString, bool amplified, const std::shared_ptr<Player> &attackerPlayer
) const {
	ss.str({});
	ss << ucfirst(target->getNameDescription()) << " loses " << damageString << " due to your " << (damage.critical ? "critical " : " ") << "attack.";

	if (damage.critical && target->getMonster() && attackerPlayer) {
		const auto &targetMonster = target->getMonster();
		static const std::pair<charmRune_t, std::string_view> charms[] = {
			{ CHARM_LOW, " (low blow charm)" },
			{ CHARM_SAVAGE, " (savage blow charm)" }
		};

		for (const auto &[charmType, charmText] : charms) {
			if (targetMonster->checkCanApplyCharm(attackerPlayer, charmType)) {
				ss << charmText;
				break;
			}
		}
	}

	if (damage.extension) {
		ss << " " << damage.exString;
	}

	if (damage.fatal) {
		ss << (amplified ? " (Amplified Onslaught)" : " (Onslaught)");
	}
	message.type = MESSAGE_DAMAGE_DEALT;
	message.text = ss.str();
}

void CombatService::sendEffects(
	const std::shared_ptr<Creature> &target, const CombatDamage &damage, const Position &targetPos, TextMessage &message,
	const CreatureVector &spectators
) {
	uint16_t hitEffect;
	if (message.primary.value) {
		combatGetTypeInfo(damage.primary.type, target, message.primary.color, hitEffect);
		if (hitEffect != CONST_ME_NONE) {
			game_.addMagicEffect(spectators, targetPos, hitEffect);
		}
	}

	if (message.secondary.value) {
		combatGetTypeInfo(damage.secondary.type, target, message.secondary.color, hitEffect);
		if (hitEffect != CONST_ME_NONE) {
			game_.addMagicEffect(spectators, targetPos, hitEffect);
		}
	}
}

void CombatService::applyCharmRune(
	const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Creature> &target, const int32_t &realDamage
) const {
	if (!targetMonster || !attackerPlayer) {
		return;
	}

	auto [major, minor] = g_iobestiary().getCharmFromTarget(attackerPlayer, targetMonster->getMonsterType());
	for (auto charmType : { major, minor }) {
		if (charmType == CHARM_NONE) {
			continue;
		}

		const auto &charm = g_iobestiary().getBestiaryCharm(charmType);
		const auto charmTier = attackerPlayer->getCharmTier(charmType);
		int8_t chance = charm->chance[charmTier] + (charm->id == CHARM_CRIPPLE ? 0 : attackerPlayer->getCharmChanceModifier());

		auto rng = uniform_random(1, 100);
		if (charm->type == CHARM_OFFENSIVE && (chance >= rng)) {
			g_iobestiary().parseCharmCombat(charm, attackerPlayer, target, realDamage);
		}
	}
}

void CombatService::applyManaLeech(
	const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Creature> &target, const CombatDamage &damage, const int32_t &realDamage
) const {
	// Wheel of destiny bonus - mana leech chance and amount
	auto wheelLeechAmount = attackerPlayer->wheel().checkDrainBodyLeech(target, SKILL_MANA_LEECH_AMOUNT);
	uint16_t manaSkill = attackerPlayer->getSkillLevel(SKILL_MANA_LEECH_AMOUNT) + wheelLeechAmount + damage.manaLeech;

	// Void charm rune
	if (targetMonster && attackerPlayer->parseRacebyCharm(CHARM_VOID) == targetMonster->getRaceId()) {
		if (const auto &charm = g_iobestiary().getBestiaryCharm(CHARM_VOID)) {
			manaSkill += charm->chance[attackerPlayer->getCharmTier(CHARM_VOID)] * 100;
		}
	}

	if (manaSkill == 0) {
		return;
	}

	CombatParams tmpParams;
	CombatDamage tmpDamage;

	int affected = damage.affected;
	tmpDamage.origin = ORIGIN_SPELL;
	tmpDamage.primary.type = COMBAT_MANADRAIN;
	tmpDamage.primary.value = calculateLeechAmount(realDamage, manaSkill, affected);

	Combat::doCombatMana(nullptr, attackerPlayer, tmpDamage, tmpParams);
}

void CombatService::applyLifeLeech(
	const std::shared_ptr<Player> &attackerPlayer, const std::shared_ptr<Monster> &targetMonster, const std::shared_ptr<Creature> &target, const CombatDamage &damage, const int32_t &realDamage
) const {
	// Wheel of destiny bonus - life leech chance and amount
	auto wheelLeechAmount = attackerPlayer->wheel().checkDrainBodyLeech(target, SKILL_LIFE_LEECH_AMOUNT);
	uint16_t lifeSkill = attackerPlayer->getSkillLevel(SKILL_LIFE_LEECH_AMOUNT) + wheelLeechAmount + damage.lifeLeech;

	if (targetMonster && attackerPlayer->parseRacebyCharm(CHARM_VAMP) == targetMonster->getRaceId()) {
		if (const auto &charm = g_iobestiary().getBestiaryCharm(CHARM_VAMP)) {
			lifeSkill += charm->chance[attackerPlayer->getCharmTier(CHARM_VAMP)] * 100;
		}
	}

	if (lifeSkill == 0) {
		return;
	}

	CombatParams tmpParams;
	CombatDamage tmpDamage;

	int affected = damage.affected;
	tmpDamage.origin = ORIGIN_SPELL;
	tmpDamage.primary.type = COMBAT_HEALING;
	tmpDamage.primary.value = calculateLeechAmount(realDamage, lifeSkill, affected);

	Combat::doCombatHealth(nullptr, attackerPlayer, tmpDamage, tmpParams);
}

int32_t CombatService::calculateLeechAmount(const int32_t &realDamage, const uint16_t &skillAmount, int targetsAffected) const {
	auto intermediateResult = realDamage * (skillAmount / 10000.0) * (0.1 * targetsAffected + 0.9) / targetsAffected;
	return std::clamp<int32_t>(static_cast<int32_t>(std::lround(intermediateResult)), 0, realDamage);
}

bool CombatService::combatChangeMana(const std::shared_ptr<Creature> &attacker, const std::shared_ptr<Creature> &target, CombatDamage &damage) {
	const Position &targetPos = target->getPosition();
	auto manaChange = damage.primary.value + damage.secondary.value;
	auto spectators = Spectators().find<Player>(targetPos);
	if (manaChange > 0) {
		std::shared_ptr<Player> attackerPlayer;
		if (attacker) {
			attackerPlayer = attacker->getPlayer();
		} else {
			attackerPlayer = nullptr;
		}
	}

	const auto &targetPlayer = target ? target->getPlayer() : nullptr;
	const auto &attackerMonster = attacker ? attacker->getMonster() : nullptr;
	const auto &attackerPlayer = attacker ? attacker->getPlayer() : nullptr;
	if (targetPlayer && attackerMonster) {
		uint16_t playerCharmRaceid = targetPlayer->parseRacebyCharm(CHARM_VOIDINVERSION);
		if (playerCharmRaceid != 0) {
			const auto &mType = g_monsters().getMonsterType(attackerMonster->getName());
			if (mType && playerCharmRaceid == mType->info.raceid) {
				const auto &charm = g_iobestiary().getBestiaryCharm(CHARM_VOIDINVERSION);
				const auto charmTier = targetPlayer->getCharmTier(CHARM_VOIDINVERSION);
				if (charm && (charm->chance[charmTier] > normal_random(0, 100)) && manaChange < 0) {
					damage.primary.value = damage.primary.type == COMBAT_MANADRAIN ? -damage.primary.value : damage.primary.value;
					damage.secondary.value = damage.secondary.type == COMBAT_MANADRAIN ? -damage.secondary.value : damage.secondary.value;
					manaChange = damage.primary.value + damage.secondary.value;
				}
			}
		}
	}

	if (manaChange > 0) {
		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (const auto &creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		auto realManaChange = target->getMana();
		target->changeMana(manaChange);
		realManaChange = target->getMana() - realManaChange;

		if (realManaChange > 0 && !target->isInGhostMode()) {
			std::string damageString = fmt::format("{} mana", realManaChange);

			std::string spectatorMessage;
			if (!attacker) {
				spectatorMessage += ucfirst(target->getNameDescription());
				spectatorMessage += " was restored for " + damageString;
			} else {
				spectatorMessage += ucfirst(attacker->getNameDescription());
				spectatorMessage += " restored ";
				if (attacker == target) {
					spectatorMessage += (targetPlayer ? targetPlayer->getReflexivePronoun() : "itself");
				} else {
					spectatorMessage += target->getNameDescription();
				}
				spectatorMessage += " for " + damageString;
			}

			TextMessage message;
			message.position = targetPos;
			message.primary.value = realManaChange;
			message.primary.color = TEXTCOLOR_MAYABLUE;

			for (const auto &spectator : spectators) {
				const auto &tmpPlayer = spectator->getPlayer();
				if (!tmpPlayer) {
					continue;
				}

				if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
					message.type = MESSAGE_HEALED;
					message.text = "You restored " + target->getNameDescription() + " for " + damageString;
				} else if (tmpPlayer == targetPlayer) {
					message.type = MESSAGE_HEALED;
					if (!attacker) {
						message.text = "You were restored for " + damageString;
					} else if (targetPlayer == attackerPlayer) {
						message.text = "You restore yourself for " + damageString;
					} else {
						message.text = "You were restored by " + attacker->getNameDescription() + " for " + damageString;
					}
				} else {
					message.type = MESSAGE_HEALED_OTHERS;
					message.text = spectatorMessage;
				}
				tmpPlayer->sendTextMessage(message);
			}
		}
	} else {
		if (!target->isAttackable()) {
			if (!target->isInGhostMode()) {
				game_.addMagicEffect(targetPos, CONST_ME_POFF);
			}
			return false;
		}

		if (attackerPlayer && targetPlayer && attackerPlayer->getSkull() == SKULL_BLACK && attackerPlayer->getSkullClient(targetPlayer) == SKULL_NONE) {
			return false;
		}

		auto manaLoss = std::min<int32_t>(target->getMana(), -manaChange);
		BlockType_t blockType = target->blockHit(attacker, COMBAT_MANADRAIN, manaLoss);
		if (blockType != BLOCK_NONE) {
			game_.addMagicEffect(targetPos, CONST_ME_POFF);
			return false;
		}

		if (manaLoss <= 0) {
			return true;
		}

		if (damage.origin != ORIGIN_NONE) {
			const auto events = target->getCreatureEvents(CREATURE_EVENT_MANACHANGE);
			if (!events.empty()) {
				for (const auto &creatureEvent : events) {
					creatureEvent->executeManaChange(target, attacker, damage);
				}
				damage.origin = ORIGIN_NONE;
				return combatChangeMana(attacker, target, damage);
			}
		}

		std::shared_ptr<MonsterType> mType = nullptr;
		if (attackerMonster) {
			mType = g_monsters().getMonsterType(attackerMonster->getName());
		}
		if (targetPlayer && attacker && mType) {
			auto [major, minor] = g_iobestiary().getCharmFromTarget(targetPlayer, mType);
			for (auto charmType : { major, minor }) {
				if (charmType == CHARM_NONE || charmType == CHARM_CLEANSE) {
					continue;
				}

				const auto &charm = g_iobestiary().getBestiaryCharm(charmType);
				if (!charm || charm->type != CHARM_DEFENSIVE) {
					continue;
				}

				const auto charmTier = targetPlayer->getCharmTier(charmType);
				if (charm->chance[charmTier] < normal_random(1, 10000) / 100.0) {
					continue;
				}

				g_iobestiary().parseCharmCombat(charm, targetPlayer, attacker, manaChange);

				if (charm->id == CHARM_DODGE) {
					return false; // Dodge charm
				}
			}
		}

		target->drainMana(attacker, manaLoss);

		std::stringstream ss;

		std::string damageString = std::to_string(manaLoss);

		std::string spectatorMessage;

		TextMessage message;
		message.position = targetPos;
		message.primary.value = manaLoss;
		message.primary.color = TEXTCOLOR_BLUE;

		for (const auto &spectator : spectators) {
			const auto &tmpPlayer = spectator->getPlayer();
			if (!tmpPlayer) {
				continue;
			}

			if (tmpPlayer == attackerPlayer && attackerPlayer != targetPlayer) {
				ss.str({});
				ss << ucfirst(target->getNameDescription()) << " loses " << damageString << " mana due to your attack.";
				message.type = MESSAGE_DAMAGE_DEALT;
				message.text = ss.str();
			} else if (tmpPlayer == targetPlayer) {
				ss.str({});
				ss << "You lose " << damageString;
				if (!attacker) {
					ss << '.';
				} else if (targetPlayer == attackerPlayer) {
					ss << " due to your own attack.";
				} else {
					ss << " mana due to an attack by " << attacker->getNameDescription() << '.';
				}
				message.type = MESSAGE_DAMAGE_RECEIVED;
				message.text = ss.str();
			} else {
				if (spectatorMessage.empty()) {
					ss.str({});
					ss << ucfirst(target->getNameDescription()) << " loses " << damageString << " mana";
					if (attacker) {
						ss << " due to ";
						if (attacker == target) {
							ss << (targetPlayer ? targetPlayer->getPossessivePronoun() : "its") << " own attack";
						} else {
							ss << "an attack by " << attacker->getNameDescription();
						}
					}
					ss << '.';
					spectatorMessage = ss.str();
				}
				message.type = MESSAGE_DAMAGE_OTHERS;
				message.text = spectatorMessage;
			}
			tmpPlayer->sendTextMessage(message);
		}

		if (targetPlayer) {
			std::string cause = "(other)";
			if (attacker) {
				cause = attacker->getName();
			}

			targetPlayer->updateInputAnalyzer(damage.primary.type, -damage.primary.value, cause);
			if (attackerPlayer) {
				if (damage.secondary.type != COMBAT_NONE) {
					attackerPlayer->updateInputAnalyzer(damage.secondary.type, -damage.secondary.value, cause);
				}
			}
		}
	}

	return true;
}
