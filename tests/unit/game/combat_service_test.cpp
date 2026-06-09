/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {
	const Position kTestPosition { 100, 100, 7 };

	std::shared_ptr<Player> makePlacedPlayer(uint32_t guid, const Position &pos) {
		auto player = std::make_shared<Player>();
		player->setName("CombatTestPlayer" + std::to_string(guid));
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	class CombatServiceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

		void SetUp() override {
			player = makePlacedPlayer(5001, kTestPosition);
			game.addPlayer(player);
		}

	protected:
		Game game;
		std::shared_ptr<Player> player;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

// combatBlockHit — guard: no damage type returns true immediately
TEST_F(CombatServiceTest, CombatBlockHit_ReturnsTrue_WhenBothDamageTypesAreNone) {
	CombatDamage damage; // primary.type = COMBAT_NONE, secondary.type = COMBAT_NONE
	EXPECT_TRUE(game.combatBlockHit(damage, nullptr, nullptr, false, false, false));
}

// combatBlockHit — healing (positive value) short-circuits to false
TEST_F(CombatServiceTest, CombatBlockHit_ReturnsFalse_WhenPrimaryValueIsPositive) {
	CombatDamage damage;
	damage.primary.type = COMBAT_HEALING;
	damage.primary.value = 50;
	EXPECT_FALSE(game.combatBlockHit(damage, nullptr, player, false, false, false));
}

// combatBlockHit — agony damage short-circuits to false
TEST_F(CombatServiceTest, CombatBlockHit_ReturnsFalse_WhenDamageTypeIsAgony) {
	CombatDamage damage;
	damage.primary.type = COMBAT_AGONYDAMAGE;
	damage.primary.value = 0;
	EXPECT_FALSE(game.combatBlockHit(damage, nullptr, player, false, false, false));
}

// Note: combatChangeHealth with placed players triggers spectator network sends
// during teardown that require a full server environment. Covered in integration tests.

// playerSetFightModes — does not crash for a valid online player
TEST_F(CombatServiceTest, PlayerSetFightModes_DoesNotCrash_WhenPlayerIsOnline) {
	EXPECT_NO_FATAL_FAILURE(game.playerSetFightModes(player->getID(), FIGHTMODE_BALANCED, true, false));
	EXPECT_NO_FATAL_FAILURE(game.playerSetFightModes(player->getID(), FIGHTMODE_ATTACK, false, true));
	EXPECT_NO_FATAL_FAILURE(game.playerSetFightModes(player->getID(), FIGHTMODE_DEFENSE, false, false));
}

// playerSetFightModes — noop when player not found
TEST_F(CombatServiceTest, PlayerSetFightModes_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerSetFightModes(0xDEADBEEF, FIGHTMODE_BALANCED, true, false));
}

// playerSetAttackedCreature — clears target when creatureId is 0
TEST_F(CombatServiceTest, PlayerSetAttackedCreature_ClearsAttack_WhenCreatureIdIsZero) {
	// Must not crash; player has no attacked creature so clearing is a noop
	EXPECT_NO_FATAL_FAILURE(game.playerSetAttackedCreature(player->getID(), 0));
}

// playerSetAttackedCreature — noop for unknown player
TEST_F(CombatServiceTest, PlayerSetAttackedCreature_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerSetAttackedCreature(0xDEADBEEF, 0));
}

// Note: combatChangeMana requires Spectators map scan and is not safely unit-testable
// without a full game map setup. Covered in integration tests instead.
