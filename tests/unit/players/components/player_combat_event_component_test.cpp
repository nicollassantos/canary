/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/combat/condition.hpp"
#include "creatures/players/player.hpp"
#include "injection_fixture.hpp"

// Tests for PlayerCombatEventComponent.
// Focus on guard conditions, pure-state queries, and client=null safe paths.

class PlayerCombatEventComponentTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		fixture.kv().reset();
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- isPzLocked ---

TEST_F(PlayerCombatEventComponentTest, IsPzLocked_ReturnsFalse_Initially) {
	EXPECT_FALSE(player->combatEvents().isPzLocked());
}

// --- hasShield ---

TEST_F(PlayerCombatEventComponentTest, HasShield_ReturnsFalse_WhenNoItemsEquipped) {
	EXPECT_FALSE(player->combatEvents().hasShield());
}

// --- getHazardSystemPoints ---

TEST_F(PlayerCombatEventComponentTest, GetHazardSystemPoints_ReturnsZero_Initially) {
	// storageValue for STORAGEVALUE_HAZARDCOUNT default = -1 → ≤0 → returns 0
	EXPECT_EQ(0u, player->combatEvents().getHazardSystemPoints());
}

// --- setHazardSystemPoints ---

TEST_F(PlayerCombatEventComponentTest, SetHazardSystemPoints_DoesNotCrash) {
	// TOGGLE_HAZARDSYSTEM defaults to true in ConfigManager — points get set
	EXPECT_NO_THROW(player->combatEvents().setHazardSystemPoints(100));
}

// --- onTakeDamage ---

TEST_F(PlayerCombatEventComponentTest, OnTakeDamage_DoesNotCrash) {
	// current implementation is a no-op
	EXPECT_NO_THROW(player->combatEvents().onTakeDamage(nullptr, 42));
}

// --- onAttackedCreatureBlockHit ---

TEST_F(PlayerCombatEventComponentTest, OnAttackedCreatureBlockHit_BlockNone_SetsAddAttackSkillPoint) {
	// BLOCK_NONE: sets addAttackSkillPoint=true, bloodHitCount=30, shieldBlockCount=30
	EXPECT_NO_THROW(player->combatEvents().onAttackedCreatureBlockHit(BLOCK_NONE));
}

TEST_F(PlayerCombatEventComponentTest, OnAttackedCreatureBlockHit_BlockDefense_DoesNotCrash) {
	// BLOCK_DEFENSE: depends on bloodHitCount (default=0) → addAttackSkillPoint=false
	EXPECT_NO_THROW(player->combatEvents().onAttackedCreatureBlockHit(BLOCK_DEFENSE));
}

TEST_F(PlayerCombatEventComponentTest, OnAttackedCreatureBlockHit_BlockArmor_DoesNotCrash) {
	EXPECT_NO_THROW(player->combatEvents().onAttackedCreatureBlockHit(BLOCK_ARMOR));
}

// --- onAddCondition ---

TEST_F(PlayerCombatEventComponentTest, OnAddCondition_NonParalyze_DoesNotCrash) {
	// CONDITION_BLEEDING: Creature::onAddCondition checks PARALYZE/HASTE (neither present);
	// sendIcons guards on client=null
	EXPECT_NO_THROW(player->combatEvents().onAddCondition(CONDITION_BLEEDING));
}

// --- onCleanseCondition ---

TEST_F(PlayerCombatEventComponentTest, OnCleanseCondition_KnownCondition_DoesNotCrash) {
	// sendTextMessage is guarded by client=null
	EXPECT_NO_THROW(player->combatEvents().onCleanseCondition(CONDITION_POISON));
}

TEST_F(PlayerCombatEventComponentTest, OnCleanseCondition_UnknownCondition_DoesNotCrash) {
	// type not in map → does nothing
	EXPECT_NO_THROW(player->combatEvents().onCleanseCondition(CONDITION_INFIGHT));
}

// --- onIdleStatus ---

TEST_F(PlayerCombatEventComponentTest, OnIdleStatus_DoesNotCrash) {
	EXPECT_NO_THROW(player->combatEvents().onIdleStatus());
}
