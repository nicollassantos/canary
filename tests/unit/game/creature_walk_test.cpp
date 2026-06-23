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
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	const Position kPos { 100, 100, 7 };

	// Minimal rooted condition — no network side effects on start/end
	class TestRootedWalkCondition final : public Condition {
	public:
		TestRootedWalkCondition() :
			Condition(CONDITIONID_DEFAULT, CONDITION_ROOTED, -1) { }

		void endCondition(std::shared_ptr<Creature>) override { }
		void addCondition(std::shared_ptr<Creature>, std::shared_ptr<Condition>) override { }

		std::shared_ptr<Condition> clone() const override {
			return std::make_shared<TestRootedWalkCondition>();
		}
	};

	std::shared_ptr<Player> makePlacedPlayer() {
		auto player = std::make_shared<Player>();
		player->setID();
		auto tile = std::make_shared<DynamicTile>(kPos);
		tile->addThing(player);
		return player;
	}

	class CreatureWalkTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- startAutoWalk ---

// empty direction list → no step queued (getNextStep returns false)
TEST_F(CreatureWalkTest, StartAutoWalk_EmptyPath_GetNextStepReturnsFalse) {
	const auto player = makePlacedPlayer();
	player->startAutoWalk({}, false);

	Direction dir = DIRECTION_NONE;
	uint32_t flags = 0;
	EXPECT_FALSE(player->getNextStep(dir, flags));
}

// CONDITION_ROOTED + ignoreConditions=false → path cleared, no step queued
TEST_F(CreatureWalkTest, StartAutoWalk_RootedCondition_PathClearedNoStepQueued) {
	const auto player = makePlacedPlayer();
	ASSERT_TRUE(player->addCondition(std::make_shared<TestRootedWalkCondition>()));
	ASSERT_TRUE(player->hasCondition(CONDITION_ROOTED));

	player->startAutoWalk({ DIRECTION_NORTH }, false);

	Direction dir = DIRECTION_NONE;
	uint32_t flags = 0;
	EXPECT_FALSE(player->getNextStep(dir, flags)) << "ROOTED must prevent path from being populated";
}

// CONDITION_ROOTED + ignoreConditions=true → path populated, step returns true
TEST_F(CreatureWalkTest, StartAutoWalk_IgnoreConditions_PopulatesPathDespiteRooted) {
	const auto player = makePlacedPlayer();
	ASSERT_TRUE(player->addCondition(std::make_shared<TestRootedWalkCondition>()));
	ASSERT_TRUE(player->hasCondition(CONDITION_ROOTED));

	player->startAutoWalk({ DIRECTION_NORTH }, true);

	Direction dir = DIRECTION_NONE;
	uint32_t flags = 0;
	ASSERT_TRUE(player->getNextStep(dir, flags)) << "ignoreConditions=true must populate path even when ROOTED";
	EXPECT_EQ(DIRECTION_NORTH, dir);
}

// multi-step path → getNextStep returns each step in LIFO order
TEST_F(CreatureWalkTest, StartAutoWalk_MultiStep_StepsReturnedInReverseOrder) {
	const auto player = makePlacedPlayer();
	// listWalkDir is stored in push-back order; getNextStep reads from back
	player->startAutoWalk({ DIRECTION_NORTH, DIRECTION_EAST }, false);

	Direction dir1 = DIRECTION_NONE, dir2 = DIRECTION_NONE;
	uint32_t flags = 0;
	ASSERT_TRUE(player->getNextStep(dir1, flags));
	ASSERT_TRUE(player->getNextStep(dir2, flags));
	EXPECT_EQ(DIRECTION_EAST, dir1);
	EXPECT_EQ(DIRECTION_NORTH, dir2);
}
