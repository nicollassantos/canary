/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/combat/spells.hpp"
#include "creatures/players/player.hpp"
#include "lib/logging/in_memory_logger.hpp"

// The aim-at-target list (0xC8) brings up to 255 spell ids per packet; it used to grow without bound.
// A player can only have as many entries as there are instant spells.
class AimAtTargetSpellsTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		previousTestContainer = DI::getTestContainer();
		InMemoryLogger::install(injector);
		DI::setTestContainer(&injector);
	}

	static void TearDownTestSuite() {
		DI::setTestContainer(previousTestContainer);
	}

	void SetUp() override {
		g_spells().clear();
		g_spells().setInstantSpell("aim test one", std::make_shared<InstantSpell>());
		g_spells().setInstantSpell("aim test two", std::make_shared<InstantSpell>());
	}

	void TearDown() override {
		g_spells().clear();
	}

	inline static di::extension::injector<> injector {};
	inline static di::extension::injector<>* previousTestContainer = nullptr;
};

TEST_F(AimAtTargetSpellsTest, TheListHoldsAtMostAsManyIdsAsThereAreInstantSpells) {
	const auto player = std::make_shared<Player>(std::shared_ptr<ProtocolGame> {});
	for (uint16_t spellId = 1; spellId <= 255; ++spellId) {
		player->updateAimAtTargetSpells(spellId, 1);
	}
	EXPECT_EQ(2u, player->getAimAtTargetSpells().size());

	player->updateAimAtTargetSpells(1, 1); // already there: still accepted
	EXPECT_EQ(2u, player->getAimAtTargetSpells().size());

	player->updateAimAtTargetSpells(1, 0); // switching one off frees its place
	player->updateAimAtTargetSpells(300, 1);
	const auto ids = player->getAimAtTargetSpells();
	EXPECT_EQ(2u, ids.size());
	EXPECT_FALSE(ids.contains(1));
	EXPECT_TRUE(ids.contains(300));
}
