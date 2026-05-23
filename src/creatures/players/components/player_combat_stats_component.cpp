#include "creatures/players/components/player_combat_stats_component.hpp"

#include "creatures/players/player.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "items/item.hpp"
#include "game/game.hpp"
#include "utils/tools.hpp"

std::string PlayerCombatStatsComponent::getDescription(int32_t lookDistance) {
	std::ostringstream s;
	std::string subjectPronoun = m_player.getSubjectPronoun();
	capitalizeWords(subjectPronoun);
	const auto playerTitle = m_player.title().getCurrentTitle() == 0 ? "" : (", " + m_player.title().getCurrentTitleName());

	if (lookDistance == -1) {
		s << "yourself" << playerTitle << ".";

		if (m_player.group->access) {
			s << " You are " << m_player.group->name << '.';
		} else if (m_player.vocation->getId() != VOCATION_NONE) {
			s << " You are " << m_player.vocation->getVocDescription() << '.';
		} else {
			s << " You have no vocation.";
		}

		if (!m_player.loyaltyTitle.empty()) {
			s << " You are a " << m_player.loyaltyTitle << ".";
		}

		if (m_player.isVip()) {
			s << " You are VIP.";
		}
	} else {
		s << m_player.name;
		if (!m_player.group->access) {
			s << " (Level " << m_player.level << ')';
		}

		s << playerTitle << ". " << subjectPronoun;

		if (m_player.group->access) {
			s << " " << m_player.getSubjectVerb() << " " << m_player.group->name << '.';
		} else if (m_player.vocation->getId() != VOCATION_NONE) {
			s << " " << m_player.getSubjectVerb() << " " << m_player.vocation->getVocDescription() << '.';
		} else {
			s << " has no vocation.";
		}

		if (!m_player.loyaltyTitle.empty()) {
			std::string article = "a";
			if (m_player.loyaltyTitle[0] == 'A' || m_player.loyaltyTitle[0] == 'E' || m_player.loyaltyTitle[0] == 'I' || m_player.loyaltyTitle[0] == 'O' || m_player.loyaltyTitle[0] == 'U') {
				article = "an";
			}
			s << " " << subjectPronoun << " " << m_player.getSubjectVerb() << " " << article << " " << m_player.loyaltyTitle << ".";
		}

		if (m_player.isVip()) {
			s << " " << subjectPronoun << " " << m_player.getSubjectVerb() << " VIP.";
		}
	}

	if (m_player.m_party) {
		if (lookDistance == -1) {
			s << " Your party has ";
		} else {
			s << " " << subjectPronoun << " " << m_player.getSubjectVerb() << " in a party with ";
		}

		const size_t memberCount = m_player.m_party->getMemberCount() + 1;
		if (memberCount == 1) {
			s << "1 member and ";
		} else {
			s << memberCount << " members and ";
		}

		const size_t invitationCount = m_player.m_party->getInvitationCount();
		if (invitationCount == 1) {
			s << "1 pending invitation.";
		} else {
			s << invitationCount << " pending invitations.";
		}
	}

	if (m_player.guild && m_player.guildRank) {
		const size_t memberCount = m_player.guild->getMemberCount();
		if (memberCount >= 1000) {
			s << "";
			return s.str();
		}

		if (lookDistance == -1) {
			s << " You are ";
		} else {
			s << " " << subjectPronoun << " " << m_player.getSubjectVerb() << " ";
		}

		s << m_player.guildRank->name << " of the " << m_player.guild->getName();
		if (!m_player.guildNick.empty()) {
			s << " (" << m_player.guildNick << ')';
		}

		if (memberCount == 1) {
			s << ", which has 1 member, " << m_player.guild->getMembersOnline().size() << " of them online.";
		} else {
			s << ", which has " << memberCount << " members, " << m_player.guild->getMembersOnline().size() << " of them online.";
		}
	}
	return s.str();
}

uint16_t PlayerCombatStatsComponent::getWeaponId(bool ignoreAmmo /* = false */) const {
	const auto &weapon = getWeapon(ignoreAmmo);
	if (!weapon) {
		return 0;
	}

	return weapon->getID();
}

std::shared_ptr<Item> PlayerCombatStatsComponent::getWeapon(Slots_t slot, bool ignoreAmmo) const {
	const auto &item = m_player.inventory[slot];
	if (!item) {
		return nullptr;
	}

	const WeaponType_t &weaponType = item->getWeaponType();
	if (weaponType == WEAPON_NONE || weaponType == WEAPON_SHIELD || weaponType == WEAPON_AMMO) {
		return nullptr;
	}

	if (!ignoreAmmo && (weaponType == WEAPON_DISTANCE || weaponType == WEAPON_MISSILE)) {
		const ItemType &it = Item::items[item->getID()];
		if (it.ammoType != AMMO_NONE) {
			return getQuiverAmmoOfType(it);
		}
	}

	return item;
}

bool PlayerCombatStatsComponent::hasQuiverEquipped() const {
	const auto &quiver = m_player.inventory[CONST_SLOT_RIGHT];
	return quiver && quiver->isQuiver() && quiver->getContainer();
}

bool PlayerCombatStatsComponent::hasWeaponDistanceEquipped() const {
	const auto &item = m_player.inventory[CONST_SLOT_LEFT];
	return item && item->getWeaponType() == WEAPON_DISTANCE;
}

std::shared_ptr<Item> PlayerCombatStatsComponent::getQuiverAmmoOfType(const ItemType &it) const {
	if (!hasQuiverEquipped()) {
		return nullptr;
	}

	const auto &quiver = m_player.inventory[CONST_SLOT_RIGHT];
	for (const auto &container = quiver->getContainer();
	     const auto &ammoItem : container->getItemList()) {
		if (ammoItem->getAmmoType() == it.ammoType) {
			if (m_player.level >= Item::items[ammoItem->getID()].minReqLevel) {
				return ammoItem;
			}
		}
	}
	return nullptr;
}

std::shared_ptr<Item> PlayerCombatStatsComponent::getWeapon(bool ignoreAmmo /* = false*/) const {
	const auto &itemLeft = getWeapon(CONST_SLOT_LEFT, ignoreAmmo);
	if (itemLeft) {
		return itemLeft;
	}

	const auto &itemRight = getWeapon(CONST_SLOT_RIGHT, ignoreAmmo);
	if (itemRight) {
		return itemRight;
	}
	return nullptr;
}

WeaponType_t PlayerCombatStatsComponent::getWeaponType() const {
	const auto &item = getWeapon();
	if (!item) {
		return WEAPON_NONE;
	}
	return item->getWeaponType();
}

int32_t PlayerCombatStatsComponent::getWeaponSkill(const std::shared_ptr<Item> &item) const {
	if (!item) {
		return m_player.getSkillLevel(SKILL_FIST);
	}

	int32_t attackSkill;

	const WeaponType_t &weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_FIST: {
			attackSkill = m_player.getSkillLevel(SKILL_FIST);
			break;
		}
		case WEAPON_SWORD: {
			attackSkill = m_player.getSkillLevel(SKILL_SWORD);
			break;
		}

		case WEAPON_CLUB: {
			attackSkill = m_player.getSkillLevel(SKILL_CLUB);
			break;
		}

		case WEAPON_AXE: {
			attackSkill = m_player.getSkillLevel(SKILL_AXE);
			break;
		}

		case WEAPON_MISSILE:
		case WEAPON_DISTANCE: {
			attackSkill = m_player.getSkillLevel(SKILL_DISTANCE);
			break;
		}

		default: {
			attackSkill = 0;
			break;
		}
	}
	return attackSkill;
}

uint16_t PlayerCombatStatsComponent::getDistanceAttackSkill(const int32_t attackSkill, const int32_t weaponAttack) const {
	// Correct calculation of getWeaponSkill function (getMaxWeaponDamage) for Paladins
	const double skillFactor = (attackSkill + 4) / 28.;
	return weaponAttack * skillFactor - weaponAttack;
}

uint16_t PlayerCombatStatsComponent::getAttackSkill(const std::shared_ptr<Item> &item) const {
	if (!item) {
		return m_player.getSkillLevel(SKILL_FIST);
	}

	int32_t attackSkill;

	const WeaponType_t &weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_FIST: {
			attackSkill = m_player.getSkillLevel(SKILL_FIST);
			break;
		}
		case WEAPON_SWORD: {
			attackSkill = m_player.getSkillLevel(SKILL_SWORD);
			break;
		}

		case WEAPON_CLUB: {
			attackSkill = m_player.getSkillLevel(SKILL_CLUB);
			break;
		}

		case WEAPON_AXE: {
			attackSkill = m_player.getSkillLevel(SKILL_AXE);
			break;
		}

		case WEAPON_MISSILE:
		case WEAPON_DISTANCE: {
			attackSkill = m_player.getSkillLevel(SKILL_DISTANCE);
			break;
		}

		default: {
			attackSkill = 0;
			break;
		}
	}

	// Correct calculation of getWeaponSkill function (getMaxWeaponDamage)
	const double skillFactor = (attackSkill + 4) / 28.;
	const auto weaponAttack = item->getAttack();
	return weaponAttack * skillFactor - weaponAttack;
}

uint8_t PlayerCombatStatsComponent::getWeaponSkillId(const std::shared_ptr<Item> &item) const {
	uint8_t skillId;
	const WeaponType_t &weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_SWORD: {
			skillId = 8;
			break;
		}

		case WEAPON_CLUB: {
			skillId = 9;
			break;
		}

		case WEAPON_AXE: {
			skillId = 10;
			break;
		}

		default: {
			skillId = 11;
			break;
		}
	}

	return skillId;
}

uint16_t PlayerCombatStatsComponent::calculateFlatDamageHealing() const {
	double previousLevelsAggregatedBaseline = 0.0;
	uint32_t currentLevelBaseline = 0;
	double currentLevelFactor = 1.0 / 5.0;

	// Starting threshold and increment steps
	uint32_t threshold = 500;
	uint32_t thresholdStep = 600;
	uint32_t tierIndex = 1;

	// Progressively reduce the scaling factor as the level increases
	while (m_player.level >= threshold) {
		currentLevelBaseline = threshold;
		currentLevelFactor = 1.0 / (5.0 + tierIndex);
		previousLevelsAggregatedBaseline += threshold * (1.0 / (5.0 + tierIndex - 1));

		++tierIndex;
		threshold += thresholdStep;
		thresholdStep += 100;
	}

	// Final value includes all completed tiers plus partial progression into the next
	uint32_t computed = std::ceil(previousLevelsAggregatedBaseline + (m_player.level - currentLevelBaseline) * currentLevelFactor);
	return std::min<uint32_t>(computed, std::numeric_limits<uint16_t>::max());
}

uint16_t PlayerCombatStatsComponent::attackTotal(uint16_t flatBonus, uint16_t equipment, uint16_t skill) const {
	double fightFactor = 0;
	switch (m_player.fightMode) {
		case FIGHTMODE_ATTACK: {
			fightFactor = 1.2f * equipment;
			break;
		}

		case FIGHTMODE_BALANCED: {
			fightFactor = 1.0f * equipment;
			break;
		}

		case FIGHTMODE_DEFENSE: {
			fightFactor = 0.6f * equipment;
			break;
		}

		default: {
			fightFactor = 1.0f * equipment;
			break;
		}
	}

	fightFactor = std::floor(fightFactor);

	const double skillFactor = (skill + 4) / 28.;

	return flatBonus + (fightFactor * skillFactor);
}

uint16_t PlayerCombatStatsComponent::attackRawTotal(uint16_t flatBonus, uint16_t equipment, uint16_t skill) const {
	const double skillFactor = (skill + 4) / 28.;
	return flatBonus + (equipment * skillFactor);
}

int32_t PlayerCombatStatsComponent::getArmor() const {
	int32_t armor = 0;

	static constexpr std::array<Slots_t, 7> armorSlots = { CONST_SLOT_HEAD, CONST_SLOT_NECKLACE, CONST_SLOT_ARMOR, CONST_SLOT_LEGS, CONST_SLOT_FEET, CONST_SLOT_RING, CONST_SLOT_AMMO };
	for (const Slots_t &slot : armorSlots) {
		const auto &inventoryItem = m_player.inventory[slot];
		if (inventoryItem) {
			armor += inventoryItem->getArmor();
		}
	}
	return armor * static_cast<int32_t>(m_player.vocation->armorMultiplier);
}

int32_t PlayerCombatStatsComponent::getMantra() const {
	int32_t mantra = 0;

	// Define which equipment slots contribute to mantra
	static constexpr std::array<Slots_t, 7> armorSlots = {
		CONST_SLOT_HEAD,
		CONST_SLOT_NECKLACE,
		CONST_SLOT_ARMOR,
		CONST_SLOT_LEGS,
		CONST_SLOT_FEET,
		CONST_SLOT_RING,
		CONST_SLOT_AMMO
	};

	// Sum mantra values from equipped items in those slots
	for (const Slots_t &slot : armorSlots) {
		const auto &inventoryItem = m_player.inventory[slot];
		if (inventoryItem) {
			mantra += inventoryItem->getMantra();
		}
	}

	return mantra;
}

int32_t PlayerCombatStatsComponent::getPartyMantra() const {
	int32_t itemMantra = getMantra();
	if (itemMantra > 0) {
		return itemMantra;
	}

	const auto &party = m_player.getParty();
	if (party) {
		auto buff = m_player.getBuff(BUFF_MANTRA);
		if (buff) {
			return buff - 100;
		}
	}

	return 0;
}

void PlayerCombatStatsComponent::updatePartyMantra() const {
	if (m_player.getPlayerVocationEnum() == VOCATION_MONK_CIP) {
		if (const auto &party = m_player.getParty()) {
			party->updateMantraHolder();
		}
	}
}

void PlayerCombatStatsComponent::getShieldAndWeapon(std::shared_ptr<Item> &shield, std::shared_ptr<Item> &weapon) const {
	shield = nullptr;
	weapon = nullptr;

	for (uint32_t slot = CONST_SLOT_RIGHT; slot <= CONST_SLOT_LEFT; slot++) {
		const auto &item = m_player.inventory[slot];
		if (!item) {
			continue;
		}

		switch (item->getWeaponType()) {
			case WEAPON_NONE:
				break;

			case WEAPON_SHIELD: {
				if (!shield || (shield && item->getDefense() > shield->getDefense())) {
					shield = item;
				}
				break;
			}

			default: { // weapons that are not shields
				weapon = item;
				break;
			}
		}
	}
}

float PlayerCombatStatsComponent::getMitigation() const {
	return m_player.wheel().calculateMitigation();
}

double PlayerCombatStatsComponent::getCombatTacticsMitigation() const {
	double fightFactor = 0.0;
	switch (m_player.fightMode) {
		case FIGHTMODE_ATTACK: {
			fightFactor = 0.8f;
			break;
		}
		case FIGHTMODE_BALANCED: {
			fightFactor = 1.0f;
			break;
		}
		case FIGHTMODE_DEFENSE: {
			fightFactor = 1.2f;
			break;
		}
		default:
			break;
	}

	return fightFactor;
}

int32_t PlayerCombatStatsComponent::getDefense(bool sendToClient /* = false*/) const {
	int32_t defenseSkill = m_player.getSkillLevel(SKILL_FIST);
	int32_t defenseValue = 7;
	std::shared_ptr<Item> weapon;
	std::shared_ptr<Item> shield;
	getShieldAndWeapon(shield, weapon);

	if (weapon) {
		defenseValue = weapon->getDefense() + weapon->getExtraDefense();
		defenseSkill = getWeaponSkill(weapon);
	}

	if (shield) {
		defenseValue = (weapon != nullptr)
			? shield->getDefense() + weapon->getExtraDefense()
			: shield->getDefense();
		// Wheel of destiny - Combat Mastery
		if (shield->getDefense() > 0) {
			defenseValue += m_player.wheel().getMajorStatConditional("Combat Mastery", WheelMajor_t::DEFENSE);
		}
		defenseSkill = m_player.getSkillLevel(SKILL_SHIELD);
	}

	defenseValue += m_player.weaponProficiency().getStat(WeaponProficiencyBonus_t::DEFENSE_BONUS);
	defenseValue += m_player.weaponProficiency().getStat(WeaponProficiencyBonus_t::WEAPON_SHIELD_MODIFIER);

	if (defenseSkill == 0) {
		switch (m_player.fightMode) {
			case FIGHTMODE_ATTACK:
			case FIGHTMODE_BALANCED:
				return 1;
			case FIGHTMODE_DEFENSE:
				return 2;
		}
	}

	auto defenseScalingFactor = shield ? 0.16f : (weapon && weapon->getDefense() > 0 ? 0.146f : 0.15f);

	return ((defenseSkill / 4.0 + 2.23) * defenseValue * getDefenseFactor(sendToClient) * defenseScalingFactor) * m_player.vocation->defenseMultiplier;
}

uint16_t PlayerCombatStatsComponent::getDefenseEquipment() const {
	uint16_t defenseValue = 6;
	std::shared_ptr<Item> weapon;
	std::shared_ptr<Item> shield;
	getShieldAndWeapon(shield, weapon);

	if (weapon) {
		defenseValue = weapon->getDefense() + weapon->getExtraDefense();
	}

	if (shield) {
		defenseValue = weapon != nullptr ? shield->getDefense() + weapon->getExtraDefense() : shield->getDefense();
		if (shield->getDefense() > 0) {
			defenseValue += m_player.wheel().getMajorStatConditional("Combat Mastery", WheelMajor_t::DEFENSE);
		}
	}

	defenseValue += m_player.weaponProficiency().getStat(WeaponProficiencyBonus_t::DEFENSE_BONUS);
	defenseValue += m_player.weaponProficiency().getStat(WeaponProficiencyBonus_t::WEAPON_SHIELD_MODIFIER);

	return defenseValue;
}

float PlayerCombatStatsComponent::getAttackFactor() const {
	switch (m_player.fightMode) {
		case FIGHTMODE_ATTACK:
			return 1.0f;
		case FIGHTMODE_BALANCED:
			return 0.75f;
		case FIGHTMODE_DEFENSE:
			return 0.5f;
		default:
			return 1.0f;
	}
}

float PlayerCombatStatsComponent::getDefenseFactor(bool sendToClient /* = false*/) const {
	switch (m_player.fightMode) {
		case FIGHTMODE_ATTACK:
			if (sendToClient) {
				return 0.5f;
			}

			return (OTSYS_TIME() - m_player.lastAttack) < m_player.getAttackSpeed() ? 0.5f : 1.0f;
		case FIGHTMODE_BALANCED:
			if (sendToClient) {
				return 0.75f;
			}

			return (OTSYS_TIME() - m_player.lastAttack) < m_player.getAttackSpeed() ? 0.75f : 1.0f;
		case FIGHTMODE_DEFENSE:
			return 1.0f;
		default:
			return 1.0f;
	}
}

std::vector<double> PlayerCombatStatsComponent::getDamageAccuracy(const ItemType &it) const {
	std::vector<double> accuracy = {};
	const auto distanceValue = m_player.getSkillLevel(SKILL_DISTANCE);
	if (it.ammoType == AMMO_BOLT || it.ammoType == AMMO_ARROW) {
		accuracy.push_back(std::min<double>(90, (1.20f * (distanceValue + 1))));
		accuracy.push_back(std::min<double>(90, (3.20f * distanceValue)));
		accuracy.push_back(std::min<double>(90, (2.00f * distanceValue)));
		accuracy.push_back(std::min<double>(90, (1.55f * distanceValue)));
		accuracy.push_back(std::min<double>(90, (1.20f * (distanceValue + 1))));
		accuracy.push_back(std::min<double>(90, distanceValue));
	} else {
		accuracy.push_back(std::min<double>(75, distanceValue + 1));
		accuracy.push_back(std::min<double>(75, 2.40f * (distanceValue + 8)));
		accuracy.push_back(std::min<double>(75, 1.55f * (distanceValue + 6)));
		accuracy.push_back(std::min<double>(75, 1.25f * (distanceValue + 3)));
		accuracy.push_back(std::min<double>(75, distanceValue + 1));
		accuracy.push_back(std::min<double>(75, 0.80f * (distanceValue + 3)));
		accuracy.push_back(std::min<double>(75, 0.70f * (distanceValue + 2)));
	}

	return accuracy;
}

SoundEffect_t PlayerCombatStatsComponent::getAttackSoundEffect() const {
	const auto &tool = getWeapon();
	if (tool == nullptr) {
		return SoundEffect_t::HUMAN_CLOSE_ATK_FIST;
	}

	const ItemType &it = Item::items[tool->getID()];
	if (it.weaponType == WEAPON_NONE || it.weaponType == WEAPON_SHIELD) {
		return SoundEffect_t::HUMAN_CLOSE_ATK_FIST;
	}

	switch (it.weaponType) {
		case WEAPON_AXE: {
			return SoundEffect_t::MELEE_ATK_AXE;
		}
		case WEAPON_SWORD: {
			return SoundEffect_t::MELEE_ATK_SWORD;
		}
		case WEAPON_CLUB: {
			return SoundEffect_t::MELEE_ATK_CLUB;
		}
		case WEAPON_AMMO:
		case WEAPON_DISTANCE: {
			if (tool->getAmmoType() == AMMO_BOLT) {
				return SoundEffect_t::DIST_ATK_CROSSBOW;
			}
			if (tool->getAmmoType() == AMMO_ARROW) {
				return SoundEffect_t::DIST_ATK_BOW;
			}
			return SoundEffect_t::DIST_ATK_THROW;

			break;
		}
		case WEAPON_WAND: {
			return SoundEffect_t::MAGICAL_RANGE_ATK;
		}
		default: {
			return SoundEffect_t::SILENCE;
		}
	}

	return SoundEffect_t::SILENCE;
}

SoundEffect_t PlayerCombatStatsComponent::getHitSoundEffect() const {
	// Distance sound effects
	const auto &tool = getWeapon();
	if (tool == nullptr) {
		return SoundEffect_t::SILENCE;
	}

	switch (const auto &it = Item::items[tool->getID()]; it.weaponType) {
		case WEAPON_AMMO: {
			if (it.ammoType == AMMO_BOLT) {
				return SoundEffect_t::DIST_ATK_CROSSBOW_SHOT;
			}
			if (it.ammoType == AMMO_ARROW) {
				if (it.shootType == CONST_ANI_BURSTARROW) {
					return SoundEffect_t::BURST_ARROW_EFFECT;
				}
				if (it.shootType == CONST_ANI_DIAMONDARROW) {
					return SoundEffect_t::DIAMOND_ARROW_EFFECT;
				}
			} else {
				return SoundEffect_t::DIST_ATK_THROW_SHOT;
			}
		}
		case WEAPON_DISTANCE: {
			if (tool->getAmmoType() == AMMO_BOLT) {
				return SoundEffect_t::DIST_ATK_CROSSBOW_SHOT;
			}
			if (tool->getAmmoType() == AMMO_ARROW) {
				return SoundEffect_t::DIST_ATK_BOW_SHOT;
			}
			return SoundEffect_t::DIST_ATK_THROW_SHOT;
		}
		case WEAPON_WAND: {
			// Separate between wand and rod here
			// return SoundEffect_t::DIST_ATK_ROD_SHOT;
			return SoundEffect_t::DIST_ATK_WAND_SHOT;
		}
		default: {
			return SoundEffect_t::SILENCE;
		}
	} // switch

	return SoundEffect_t::SILENCE;
}

// event methods
