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
#include "game/game.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	const Position kPos { 100, 100, 7 };
	const Position kFarPos { 200, 200, 7 };

	std::shared_ptr<Player> makePlacedPlayer(const Position &pos) {
		auto player = std::make_shared<Player>();
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	// Minimal rooted condition — no network side effects
	class TestRootedCondition final : public Condition {
	public:
		TestRootedCondition() :
			Condition(CONDITIONID_DEFAULT, CONDITION_ROOTED, -1) { }

		void endCondition(std::shared_ptr<Creature>) override { }
		void addCondition(std::shared_ptr<Creature>, std::shared_ptr<Condition>) override { }

		std::shared_ptr<Condition> clone() const override {
			return std::make_shared<TestRootedCondition>();
		}
	};

	class MovementServiceTest : public ::testing::Test {
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
		Game game;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- internalMoveCreature (direction overload) ---

// null creature → RETURNVALUE_NOTPOSSIBLE (guard at top of method)
TEST_F(MovementServiceTest, InternalMoveCreature_NullCreature_ReturnsNotPossible) {
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, game.internalMoveCreature(nullptr, DIRECTION_NORTH));
}

// creature with baseSpeed == 0 → RETURNVALUE_NOTMOVABLE
TEST_F(MovementServiceTest, InternalMoveCreature_ZeroBaseSpeed_ReturnsNotMovable) {
	auto player = makePlacedPlayer(kPos);
	player->setBaseSpeed(0);
	EXPECT_EQ(RETURNVALUE_NOTMOVABLE, game.internalMoveCreature(player, DIRECTION_NORTH));
}

// --- internalMoveCreature (tile overload) ---

// CONDITION_ROOTED → RETURNVALUE_NOTPOSSIBLE (checked before tile queryAdd)
TEST_F(MovementServiceTest, InternalMoveCreature_RootedCreature_ReturnsNotPossible) {
	auto player = makePlacedPlayer(kPos);
	ASSERT_TRUE(player->addCondition(std::make_shared<TestRootedCondition>()));
	ASSERT_TRUE(player->hasCondition(CONDITION_ROOTED));

	auto destTile = std::make_shared<DynamicTile>(kFarPos);
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, game.internalMoveCreature(player, destTile));
}

// --- internalTeleport ---

// nullptr thing → RETURNVALUE_NOTPOSSIBLE (null guard)
TEST_F(MovementServiceTest, InternalTeleport_NullThing_ReturnsNotPossible) {
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, game.internalTeleport(nullptr, kFarPos));
}

// teleport to same position → RETURNVALUE_CONTACTADMINISTRATOR
TEST_F(MovementServiceTest, InternalTeleport_SamePosition_ReturnsContactAdministrator) {
	auto player = makePlacedPlayer(kPos);
	EXPECT_EQ(RETURNVALUE_CONTACTADMINISTRATOR, game.internalTeleport(player, kPos));
}

// removed creature → RETURNVALUE_NOTPOSSIBLE (checked after same-pos check)
TEST_F(MovementServiceTest, InternalTeleport_RemovedCreature_ReturnsNotPossible) {
	auto player = makePlacedPlayer(kPos);
	player->setRemoved();
	// different position so same-pos check passes; isRemoved check fires
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, game.internalTeleport(player, kFarPos));
}
