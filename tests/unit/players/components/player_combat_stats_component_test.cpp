/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/player.hpp"
#include "items/cylinder.hpp"
#include "items/item.hpp"
#include "injection_fixture.hpp"
#include "test_items.hpp"

class PlayerCombatStatsComponentTest : public ::testing::Test {
public:
	static void SetUpTestSuite() {
		TestItems::init();
	}

protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	void equip(Slots_t slot, std::shared_ptr<Item> item) const {
		static_cast<Cylinder &>(*player).internalAddThing(static_cast<uint32_t>(slot), item);
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- getWeaponId ---

TEST_F(PlayerCombatStatsComponentTest, GetWeaponId_ReturnsZero_WhenNoWeapon) {
	EXPECT_EQ(0u, player->combatStats().getWeaponId());
}

// --- getWeaponType ---

TEST_F(PlayerCombatStatsComponentTest, GetWeaponType_ReturnsNone_WhenNoWeapon) {
	EXPECT_EQ(WEAPON_NONE, player->combatStats().getWeaponType());
}

// --- hasQuiverEquipped ---

TEST_F(PlayerCombatStatsComponentTest, HasQuiverEquipped_ReturnsFalse_WhenNoItemsEquipped) {
	EXPECT_FALSE(player->combatStats().hasQuiverEquipped());
}

// --- hasWeaponDistanceEquipped ---

TEST_F(PlayerCombatStatsComponentTest, HasWeaponDistanceEquipped_ReturnsFalse_WhenNoItemsEquipped) {
	EXPECT_FALSE(player->combatStats().hasWeaponDistanceEquipped());
}

// --- attackRawTotal ---

TEST_F(PlayerCombatStatsComponentTest, AttackRawTotal_ReturnsZero_WhenAllInputsZero) {
	EXPECT_EQ(0u, player->combatStats().attackRawTotal(0, 0, 0));
}

TEST_F(PlayerCombatStatsComponentTest, AttackRawTotal_AppliesFormula) {
	// flatBonus=10, equipment=28, skill=24 → 10 + 28*(28/28.0) = 10 + 28 = 38
	EXPECT_EQ(38u, player->combatStats().attackRawTotal(10, 28, 24));
}

TEST_F(PlayerCombatStatsComponentTest, AttackRawTotal_FlatBonusOnly) {
	// equipment=0, skill doesn't matter → just flatBonus
	EXPECT_EQ(5u, player->combatStats().attackRawTotal(5, 0, 100));
}

// --- attackTotal ---

TEST_F(PlayerCombatStatsComponentTest, AttackTotal_AttackMode_AppliesBoost) {
	// fightMode defaults to FIGHTMODE_ATTACK: fightFactor = floor(1.2 * equip)
	// flatBonus=0, equipment=10, skill=24 → floor(12) * (28/28) = 12
	EXPECT_EQ(12u, player->combatStats().attackTotal(0, 10, 24));
}

TEST_F(PlayerCombatStatsComponentTest, AttackTotal_DefenseMode_ReducesFactor) {
	player->setFightMode(FIGHTMODE_DEFENSE);
	// fightFactor = floor(0.6 * 10) = floor(6) = 6; skill=24 → 6 * (28/28) = 6
	EXPECT_EQ(6u, player->combatStats().attackTotal(0, 10, 24));
}

TEST_F(PlayerCombatStatsComponentTest, AttackTotal_BalancedMode_NeutralFactor) {
	player->setFightMode(FIGHTMODE_BALANCED);
	// fightFactor = floor(1.0 * 10) = 10; skill=24 → 10 * (28/28) = 10
	EXPECT_EQ(10u, player->combatStats().attackTotal(0, 10, 24));
}

// --- getDistanceAttackSkill ---

TEST_F(PlayerCombatStatsComponentTest, GetDistanceAttackSkill_WithZeroWeaponAttack) {
	// weaponAttack=0 → 0 * anything = 0, minus 0 = 0
	EXPECT_EQ(0u, player->combatStats().getDistanceAttackSkill(24, 0));
}

TEST_F(PlayerCombatStatsComponentTest, GetDistanceAttackSkill_Formula) {
	// skill=52, weaponAttack=28 → 28 * (56/28.0) - 28 = 56 - 28 = 28
	EXPECT_EQ(28u, player->combatStats().getDistanceAttackSkill(52, 28));
}

// --- calculateFlatDamageHealing ---

TEST_F(PlayerCombatStatsComponentTest, CalculateFlatDamageHealing_Level1_Returns1) {
	// level defaults to 1: while(1>=500) skips; ceil(0 + 1*0.2) = ceil(0.2) = 1
	EXPECT_EQ(1u, player->combatStats().calculateFlatDamageHealing());
}

TEST_F(PlayerCombatStatsComponentTest, CalculateFlatDamageHealing_Level100) {
	// level<500: ceil(0 + 100 * 0.2) = ceil(20) = 20
	player->setLevel(100);
	EXPECT_EQ(20u, player->combatStats().calculateFlatDamageHealing());
}

// --- getCombatTacticsMitigation ---

TEST_F(PlayerCombatStatsComponentTest, GetCombatTacticsMitigation_AttackMode) {
	// FIGHTMODE_ATTACK → 0.8f (float literal, approximate double comparison)
	EXPECT_NEAR(0.8, player->combatStats().getCombatTacticsMitigation(), 1e-4);
}

TEST_F(PlayerCombatStatsComponentTest, GetCombatTacticsMitigation_DefenseMode) {
	player->setFightMode(FIGHTMODE_DEFENSE);
	EXPECT_NEAR(1.2, player->combatStats().getCombatTacticsMitigation(), 1e-4);
}

TEST_F(PlayerCombatStatsComponentTest, GetCombatTacticsMitigation_BalancedMode) {
	player->setFightMode(FIGHTMODE_BALANCED);
	EXPECT_DOUBLE_EQ(1.0, player->combatStats().getCombatTacticsMitigation());
}

// --- getMantra ---

TEST_F(PlayerCombatStatsComponentTest, GetMantra_ReturnsZero_WhenNoItemsEquipped) {
	EXPECT_EQ(0, player->combatStats().getMantra());
}

// --- getPartyMantra ---

TEST_F(PlayerCombatStatsComponentTest, GetPartyMantra_ReturnsZero_WhenNoPartyAndNoItems) {
	EXPECT_EQ(0, player->combatStats().getPartyMantra());
}

// --- getShieldAndWeapon ---

TEST_F(PlayerCombatStatsComponentTest, GetShieldAndWeapon_BothNull_WhenNoItems) {
	std::shared_ptr<Item> shield;
	std::shared_ptr<Item> weapon;
	player->combatStats().getShieldAndWeapon(shield, weapon);
	EXPECT_EQ(nullptr, shield);
	EXPECT_EQ(nullptr, weapon);
}
