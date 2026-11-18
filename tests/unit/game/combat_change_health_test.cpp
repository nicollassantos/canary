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
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "game/game.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"

// Game::combatChangeHealth (CombatService) on real players. Each transformation must read and write
// the matching damage component (root AGENTS.md, combat): unequal components, secondary-only damage
// and both together are checked where the code moves value from one to the other.
namespace {
	class CombatChangeHealthTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
			ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));
			target = place(7001, "Combat Target", Position(100, 100, 7));
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		std::shared_ptr<Player> place(uint32_t guid, const std::string &name, const Position &position) {
			auto player = std::make_shared<Player>();
			player->setName(name);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(guid);
			EXPECT_TRUE(player->setVocation(1));
			player->setID();
			auto tile = std::make_shared<DynamicTile>(position);
			tile->addThing(player);
			tiles.push_back(tile);
			game.addPlayer(player);
			return player;
		}

		static CombatDamage damage(int32_t primary, int32_t secondary, CombatOrigin origin = ORIGIN_NONE) {
			CombatDamage result;
			result.primary.type = COMBAT_PHYSICALDAMAGE;
			result.primary.value = -primary;
			result.secondary.type = COMBAT_FIREDAMAGE;
			result.secondary.value = -secondary;
			result.origin = origin;
			return result;
		}

		Game game;
		std::shared_ptr<Player> target;
		std::vector<std::shared_ptr<Tile>> tiles;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

TEST_F(CombatChangeHealthTest, DamageTakesBothComponentsFromHealth) {
	auto hit = damage(30, 20);
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	EXPECT_EQ(950, target->getHealth());
}

TEST_F(CombatChangeHealthTest, SecondaryOnlyDamageIsApplied) {
	auto hit = damage(0, 40);
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	EXPECT_EQ(960, target->getHealth());
}

TEST_F(CombatChangeHealthTest, HealingRaisesHealthUpToTheMaximum) {
	auto hit = damage(100, 0);
	ASSERT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	ASSERT_EQ(900, target->getHealth());

	CombatDamage heal;
	heal.primary.type = COMBAT_HEALING;
	heal.primary.value = 40;
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, heal));
	EXPECT_EQ(940, target->getHealth());
	heal.primary.value = 500;
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, heal));
	EXPECT_EQ(target->getMaxHealth(), target->getHealth());
}

// Mana shield takes the damage from mana: first the primary component, then what is left from the
// secondary; only the rest reaches health.
TEST_F(CombatChangeHealthTest, ManaShieldAbsorbsThePrimaryThenTheSecondaryComponent) {
	target->setVarStats(STAT_MAXMANAPOINTS, 100);
	target->changeMana(100);
	ASSERT_EQ(100U, target->getMana());
	ASSERT_TRUE(target->addCondition(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MANASHIELD, 60000, 0)));

	auto hit = damage(60, 70); // 130: 100 from mana (60 primary + 40 of the secondary), 30 from health
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	EXPECT_EQ(0U, target->getMana());
	EXPECT_EQ(970, target->getHealth());
	EXPECT_EQ(0, hit.primary.value);
	EXPECT_EQ(30, hit.secondary.value);
}

TEST_F(CombatChangeHealthTest, ManaShieldWithEnoughManaKeepsHealthWhole) {
	target->setVarStats(STAT_MAXMANAPOINTS, 500);
	target->changeMana(500);
	ASSERT_TRUE(target->addCondition(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MANASHIELD, 60000, 0)));

	auto hit = damage(0, 80); // secondary only
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	EXPECT_EQ(420U, target->getMana());
	EXPECT_EQ(1000, target->getHealth());
}

// A damage-received buff scales both components, not only the primary one.
TEST_F(CombatChangeHealthTest, ADamageReceivedBuffScalesBothComponents) {
	target->setBuff(BUFF_DAMAGERECEIVED, -50); // 100% -> 50%
	auto hit = damage(40, 20, ORIGIN_MELEE);
	EXPECT_TRUE(game.combatChangeHealth(nullptr, target, hit));
	EXPECT_EQ(970, target->getHealth());
}

TEST_F(CombatChangeHealthTest, ABlackSkullCanNeitherHurtNorHealAnUnmarkedPlayer) {
	const auto attacker = place(7002, "Combat Attacker", Position(101, 100, 7));
	attacker->setSkull(SKULL_BLACK);

	auto hit = damage(30, 20);
	EXPECT_FALSE(game.combatChangeHealth(attacker, target, hit));
	EXPECT_EQ(1000, target->getHealth());

	auto first = damage(100, 0);
	ASSERT_TRUE(game.combatChangeHealth(nullptr, target, first));
	CombatDamage heal;
	heal.primary.type = COMBAT_HEALING;
	heal.primary.value = 50;
	EXPECT_FALSE(game.combatChangeHealth(attacker, target, heal));
	EXPECT_EQ(900, target->getHealth());
}

// A damage over time condition hurts through the same path (combatBlockHit, then combatChangeHealth
// with origin condition): its rounds land one per interval and then stop. Mitigation may lower each
// round, so the check is on equal rounds rather than on the raw 10.
TEST_F(CombatChangeHealthTest, APoisonConditionDealsItsRoundsAndStops) {
	const auto poison = std::make_shared<ConditionDamage>(CONDITIONID_DEFAULT, CONDITION_POISON);
	ASSERT_TRUE(poison->addDamage(2, 2000, -10));
	ASSERT_TRUE(target->addCondition(poison)); // the first round lands when it starts
	const auto firstRound = 1000 - target->getHealth();
	ASSERT_GT(firstRound, 0);
	ASSERT_LE(firstRound, 10);

	poison->executeCondition(target, 2000);
	EXPECT_EQ(1000 - 2 * firstRound, target->getHealth());
	poison->executeCondition(target, 2000);
	poison->executeCondition(target, 2000);
	EXPECT_EQ(1000 - 2 * firstRound, target->getHealth()); // two rounds only
}
