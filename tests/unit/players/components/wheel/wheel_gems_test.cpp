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
#include "creatures/players/components/wheel/wheel_gems.hpp"
#include "enums/player_wheel.hpp"

// --- WheelGemUtils::getHealthValue ---

TEST(WheelGemUtilsTest, GetHealthValue_Knight_Vocation_Health_Returns300) {
	EXPECT_EQ(300, WheelGemUtils::getHealthValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::Vocation_Health));
}

TEST(WheelGemUtilsTest, GetHealthValue_Paladin_Vocation_Health_Returns200) {
	EXPECT_EQ(200, WheelGemUtils::getHealthValue(VOCATION_PALADIN, WheelGemBasicModifier_t::Vocation_Health));
}

TEST(WheelGemUtilsTest, GetHealthValue_Sorcerer_Vocation_Health_Returns100) {
	EXPECT_EQ(100, WheelGemUtils::getHealthValue(VOCATION_SORCERER, WheelGemBasicModifier_t::Vocation_Health));
}

TEST(WheelGemUtilsTest, GetHealthValue_UnknownVocation_ReturnsZero) {
	// VOCATION_NONE is not in the map → returns 0
	EXPECT_EQ(0, WheelGemUtils::getHealthValue(VOCATION_NONE, WheelGemBasicModifier_t::Vocation_Health));
}

TEST(WheelGemUtilsTest, GetHealthValue_Knight_FireResistance_Returns150) {
	EXPECT_EQ(150, WheelGemUtils::getHealthValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::Vocation_Health_FireResistance));
}

TEST(WheelGemUtilsTest, GetHealthValue_UnknownModifier_ReturnsZero) {
	// General_PhysicalResistance is not a health modifier → returns 0
	EXPECT_EQ(0, WheelGemUtils::getHealthValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::General_PhysicalResistance));
}

// --- WheelGemUtils::getManaValue ---

TEST(WheelGemUtilsTest, GetManaValue_Knight_Vocation_Mana_ReturnsPositive) {
	// Knight has a mana value; exact value depends on the table
	EXPECT_GT(WheelGemUtils::getManaValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::Vocation_Mana), 0);
}

TEST(WheelGemUtilsTest, GetManaValue_Sorcerer_Vocation_Mana_GreaterThanKnight) {
	// Casters generally get more mana than melee
	EXPECT_GE(WheelGemUtils::getManaValue(VOCATION_SORCERER, WheelGemBasicModifier_t::Vocation_Mana),
	          WheelGemUtils::getManaValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::Vocation_Mana));
}

TEST(WheelGemUtilsTest, GetManaValue_UnknownModifier_ReturnsZero) {
	EXPECT_EQ(0, WheelGemUtils::getManaValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::General_PhysicalResistance));
}

// --- WheelGemUtils::getCapacityValue ---

TEST(WheelGemUtilsTest, GetCapacityValue_Knight_Vocation_Capacity_ReturnsPositive) {
	EXPECT_GT(WheelGemUtils::getCapacityValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::Vocation_Capacity), 0);
}

TEST(WheelGemUtilsTest, GetCapacityValue_UnknownModifier_ReturnsZero) {
	EXPECT_EQ(0, WheelGemUtils::getCapacityValue(VOCATION_KNIGHT, WheelGemBasicModifier_t::General_PhysicalResistance));
}
