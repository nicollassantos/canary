#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

class Player;
class Item;
struct ItemType;
enum Slots_t : uint8_t;
enum WeaponType_t : uint8_t;
enum SoundEffect_t : uint16_t;

class PlayerCombatStatsComponent {
public:
	PlayerCombatStatsComponent() = delete;
	explicit PlayerCombatStatsComponent(Player &player) :
		m_player(player) { }

	std::string getDescription(int32_t lookDistance);

	uint16_t getWeaponId(bool ignoreAmmo = false) const;
	std::shared_ptr<Item> getWeapon(Slots_t slot, bool ignoreAmmo) const;
	std::shared_ptr<Item> getWeapon(bool ignoreAmmo = false) const;
	bool hasQuiverEquipped() const;
	bool hasWeaponDistanceEquipped() const;
	std::shared_ptr<Item> getQuiverAmmoOfType(const ItemType &it) const;
	WeaponType_t getWeaponType() const;

	int32_t getWeaponSkill(const std::shared_ptr<Item> &item) const;
	uint16_t getDistanceAttackSkill(int32_t attackSkill, int32_t weaponAttack) const;
	uint16_t getAttackSkill(const std::shared_ptr<Item> &item) const;
	uint8_t getWeaponSkillId(const std::shared_ptr<Item> &item) const;

	uint16_t calculateFlatDamageHealing() const;
	uint16_t attackTotal(uint16_t flatBonus, uint16_t equipment, uint16_t skill) const;
	uint16_t attackRawTotal(uint16_t flatBonus, uint16_t equipment, uint16_t skill) const;

	int32_t getArmor() const;
	int32_t getMantra() const;
	int32_t getPartyMantra() const;
	void updatePartyMantra() const;
	void getShieldAndWeapon(std::shared_ptr<Item> &shield, std::shared_ptr<Item> &weapon) const;
	float getMitigation() const;
	double getCombatTacticsMitigation() const;
	int32_t getDefense(bool sendToClient = false) const;
	uint16_t getDefenseEquipment() const;
	float getAttackFactor() const;
	float getDefenseFactor(bool sendToClient = false) const;
	std::vector<double> getDamageAccuracy(const ItemType &it) const;

	SoundEffect_t getAttackSoundEffect() const;
	SoundEffect_t getHitSoundEffect() const;

private:
	Player &m_player;
};
