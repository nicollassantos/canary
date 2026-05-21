/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// Characterization tests for player.cpp stats subsystem.
// Documents current Player health/mana/skill behavior to guard against
// regressions during Phase 5 (Player domain extraction).

#include <gtest/gtest.h>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/player.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PlayerStatsBehaviorTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector_);
		DI::setTestContainer(&injector_);
	}

	static void TearDownTestSuite() {
		if (DI::getTestContainer() == &injector_) {
			DI::setTestContainer(nullptr);
		}
	}

private:
	inline static di::extension::injector<> injector_ {};
};

// New Player starts with health > 0 (base health from creature defaults)
TEST_F(PlayerStatsBehaviorTest, NewPlayerHasPositiveHealth) {
	auto player = std::make_shared<Player>();
	EXPECT_GT(player->getHealth(), 0);
}

// New Player's current health equals max health at creation
TEST_F(PlayerStatsBehaviorTest, NewPlayerHealthEqualsMaxHealth) {
	auto player = std::make_shared<Player>();
	EXPECT_EQ(player->getMaxHealth(), static_cast<uint32_t>(player->getHealth()));
}

// New Player has mana >= 0
TEST_F(PlayerStatsBehaviorTest, NewPlayerHasNonNegativeMana) {
	auto player = std::make_shared<Player>();
	EXPECT_GE(player->getMana(), 0);
}

// getLevel returns 1 for a new player (default starting level)
TEST_F(PlayerStatsBehaviorTest, NewPlayerLevelIsOne) {
	auto player = std::make_shared<Player>();
	EXPECT_EQ(1u, player->getLevel());
}

// NOTE: getSkillLevel and getCapacity require a loaded vocation (game assets).
// Those tests live in tests/integration/game/game_items_it.cpp where
// full game state is available.

// Speed starts at a positive value (movement is possible by default)
TEST_F(PlayerStatsBehaviorTest, NewPlayerHasPositiveBaseSpeed) {
	auto player = std::make_shared<Player>();
	EXPECT_GT(player->getBaseSpeed(), 0);
}

// getID returns 0 before setID() is called
TEST_F(PlayerStatsBehaviorTest, PlayerIdIsZeroBeforeSetId) {
	auto player = std::make_shared<Player>();
	EXPECT_EQ(0u, player->getID());
}

// getName returns empty string for a nameless player
TEST_F(PlayerStatsBehaviorTest, NameIsEmptyByDefault) {
	auto player = std::make_shared<Player>();
	EXPECT_TRUE(player->getName().empty());
}

// setName + getName round-trip
TEST_F(PlayerStatsBehaviorTest, SetNameGetNameRoundTrip) {
	auto player = std::make_shared<Player>();
	player->setName("TestHero");
	EXPECT_EQ("TestHero", player->getName());
}

// setBankBalance + getBankBalance round-trip
TEST_F(PlayerStatsBehaviorTest, BankBalanceRoundTrip) {
	auto player = std::make_shared<Player>();
	player->setBankBalance(12345u);
	EXPECT_EQ(12345u, player->getBankBalance());
}
