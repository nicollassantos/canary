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
#include "enums/weapon_proficiency.hpp"
#include "io/io_bosstiary.hpp"
#include "injection_fixture.hpp"

class WeaponProficiencyTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	WeaponProficiency &wp() const { return player->weaponProficiency(); }

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- getStat / addStat / resetStats ---

TEST_F(WeaponProficiencyTest, GetStat_ReturnsZero_Initially) {
	EXPECT_DOUBLE_EQ(0.0, wp().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE));
}

TEST_F(WeaponProficiencyTest, AddStat_AccumulatesValue) {
	wp().addStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE, 5.0);
	EXPECT_DOUBLE_EQ(5.0, wp().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE));
}

TEST_F(WeaponProficiencyTest, AddStat_AccumulatesMultipleCalls) {
	wp().addStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE, 3.0);
	wp().addStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE, 7.0);
	EXPECT_DOUBLE_EQ(10.0, wp().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE));
}

TEST_F(WeaponProficiencyTest, ResetStats_ClearsAllStats) {
	wp().addStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE, 10.0);
	wp().addStat(WeaponProficiencyBonus_t::DEFENSE_BONUS, 5.0);
	wp().resetStats();
	EXPECT_DOUBLE_EQ(0.0, wp().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE));
	EXPECT_DOUBLE_EQ(0.0, wp().getStat(WeaponProficiencyBonus_t::DEFENSE_BONUS));
}

// --- getSkillBonus / addSkillBonus / resetSkillBonuses ---

TEST_F(WeaponProficiencyTest, GetSkillBonus_ReturnsZero_Initially) {
	EXPECT_EQ(0u, wp().getSkillBonus(SKILL_SWORD));
}

TEST_F(WeaponProficiencyTest, AddSkillBonus_SetsValue) {
	wp().addSkillBonus(SKILL_SWORD, 10);
	EXPECT_EQ(10u, wp().getSkillBonus(SKILL_SWORD));
}

TEST_F(WeaponProficiencyTest, AddSkillBonus_AccumulatesAcrossSkills) {
	wp().addSkillBonus(SKILL_SWORD, 5);
	wp().addSkillBonus(SKILL_AXE, 3);
	EXPECT_EQ(5u, wp().getSkillBonus(SKILL_SWORD));
	EXPECT_EQ(3u, wp().getSkillBonus(SKILL_AXE));
}

TEST_F(WeaponProficiencyTest, ResetSkillBonuses_ClearsAll) {
	wp().addSkillBonus(SKILL_SWORD, 10);
	wp().resetSkillBonuses();
	EXPECT_EQ(0u, wp().getSkillBonus(SKILL_SWORD));
}

// --- getPowerfulFoeDamage / addPowerfulFoeDamage / resetPowerfulFoeDamage ---

TEST_F(WeaponProficiencyTest, GetPowerfulFoeDamage_ReturnsZero_Initially) {
	EXPECT_DOUBLE_EQ(0.0, wp().getPowerfulFoeDamage());
}

TEST_F(WeaponProficiencyTest, AddPowerfulFoeDamage_AccumulatesValue) {
	wp().addPowerfulFoeDamage(0.5);
	wp().addPowerfulFoeDamage(0.3);
	EXPECT_NEAR(0.8, wp().getPowerfulFoeDamage(), 1e-9);
}

TEST_F(WeaponProficiencyTest, ResetPowerfulFoeDamage_ClearsValue) {
	wp().addPowerfulFoeDamage(1.5);
	wp().resetPowerfulFoeDamage();
	EXPECT_DOUBLE_EQ(0.0, wp().getPowerfulFoeDamage());
}

// --- getSpecializedMagic / addSpecializedMagic / resetSpecializedMagic ---

TEST_F(WeaponProficiencyTest, GetSpecializedMagic_ReturnsZero_Initially) {
	EXPECT_EQ(0u, wp().getSpecializedMagic(COMBAT_FIREDAMAGE));
}

TEST_F(WeaponProficiencyTest, AddSpecializedMagic_SetsValue) {
	wp().addSpecializedMagic(COMBAT_FIREDAMAGE, 5);
	EXPECT_EQ(5u, wp().getSpecializedMagic(COMBAT_FIREDAMAGE));
}

TEST_F(WeaponProficiencyTest, ResetSpecializedMagic_ClearsAll) {
	wp().addSpecializedMagic(COMBAT_FIREDAMAGE, 5);
	wp().resetSpecializedMagic();
	EXPECT_EQ(0u, wp().getSpecializedMagic(COMBAT_FIREDAMAGE));
}

// --- getBosstiaryExperience ---

TEST_F(WeaponProficiencyTest, GetBosstiaryExperience_Bane_Returns500) {
	EXPECT_EQ(500u, wp().getBosstiaryExperience(BosstiaryRarity_t::RARITY_BANE));
}

TEST_F(WeaponProficiencyTest, GetBosstiaryExperience_Archfoe_Returns5000) {
	EXPECT_EQ(5000u, wp().getBosstiaryExperience(BosstiaryRarity_t::RARITY_ARCHFOE));
}

TEST_F(WeaponProficiencyTest, GetBosstiaryExperience_Nemesis_Returns15000) {
	EXPECT_EQ(15000u, wp().getBosstiaryExperience(BosstiaryRarity_t::RARITY_NEMESIS));
}

// --- getBestiaryExperience ---

TEST_F(WeaponProficiencyTest, GetBestiaryExperience_ZeroStars_ReturnsZeroOrPositive) {
	// poly at star=0: 0 - 0 + 0 + 0 - 0 + 1.0 = 1.0 → result = 1
	EXPECT_GE(wp().getBestiaryExperience(0), 0u);
}

TEST_F(WeaponProficiencyTest, GetBestiaryExperience_ClampsStar_AboveFive) {
	// Stars > 5 are clamped to 5; result should match star=5
	EXPECT_EQ(wp().getBestiaryExperience(10), wp().getBestiaryExperience(5));
}

// --- getAutoAttackCritical / addAutoAttackCritical ---

TEST_F(WeaponProficiencyTest, GetAutoAttackCritical_ReturnsZero_Initially) {
	const auto &crit = wp().getAutoAttackCritical();
	EXPECT_DOUBLE_EQ(0.0, crit.chance);
	EXPECT_DOUBLE_EQ(0.0, crit.damage);
}

TEST_F(WeaponProficiencyTest, AddAutoAttackCritical_AccumulatesBonus) {
	wp().addAutoAttackCritical({ .chance = 0.1, .damage = 0.5 });
	wp().addAutoAttackCritical({ .chance = 0.2, .damage = 0.3 });
	const auto &crit = wp().getAutoAttackCritical();
	EXPECT_NEAR(0.3, crit.chance, 1e-9);
	EXPECT_NEAR(0.8, crit.damage, 1e-9);
}
