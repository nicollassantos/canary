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
#include "injection_fixture.hpp"

class ConditionTest : public ::testing::Test {
protected:
	void SetUp() override {
		fixture.logger().reset();
	}
	InjectionFixture fixture;
};

// --- getId / getType / getTicks / getSubId ---

TEST_F(ConditionTest, GetType_ReturnsConstructedType) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_EQ(CONDITION_INFIGHT, cond->getType());
}

TEST_F(ConditionTest, GetId_ReturnsConstructedId) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_EQ(CONDITIONID_DEFAULT, cond->getId());
}

TEST_F(ConditionTest, GetTicks_ReturnsConstructedTicks) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 5000);
	ASSERT_NE(nullptr, cond);
	EXPECT_EQ(5000, cond->getTicks());
}

TEST_F(ConditionTest, GetSubId_ReturnsZero_WhenDefaultSubId) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 1000);
	ASSERT_NE(nullptr, cond);
	EXPECT_EQ(0u, cond->getSubId());
}

// --- isPersistent ---

TEST_F(ConditionTest, IsPersistent_ReturnsFalse_WhenTicksMinus1) {
	// ticks == -1: isPersistent returns false (infinite duration but not "persistent")
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, -1);
	ASSERT_NE(nullptr, cond);
	EXPECT_FALSE(cond->isPersistent());
}

TEST_F(ConditionTest, IsPersistent_ReturnsTrue_WhenDefaultIdAndPositiveTicks) {
	// CONDITIONID_DEFAULT + positive ticks → isPersistent returns true
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_TRUE(cond->isPersistent());
}

TEST_F(ConditionTest, IsPersistent_ReturnsFalse_WhenNonDefaultId) {
	// id != CONDITIONID_DEFAULT and != CONDITIONID_COMBAT → false
	auto cond = Condition::createCondition(CONDITIONID_HEAD, CONDITION_INFIGHT, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_FALSE(cond->isPersistent());
}

// --- isRemovableOnDeath ---

TEST_F(ConditionTest, IsRemovableOnDeath_ReturnsTrue_ForGeneralCondition) {
	// CONDITION_INFIGHT is not in nonRemovableConditions → returns true
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_TRUE(cond->isRemovableOnDeath());
}

TEST_F(ConditionTest, IsRemovableOnDeath_ReturnsFalse_WhenTicksMinus1) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, -1);
	ASSERT_NE(nullptr, cond);
	EXPECT_FALSE(cond->isRemovableOnDeath());
}

TEST_F(ConditionTest, IsRemovableOnDeath_ReturnsFalse_ForSpellCooldown) {
	auto cond = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLCOOLDOWN, 10000);
	ASSERT_NE(nullptr, cond);
	EXPECT_FALSE(cond->isRemovableOnDeath());
}
