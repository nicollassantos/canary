/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/iobestiary.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	// sendBuyCharmRune actions, as the client sends them.
	constexpr uint8_t UnlockCharm = 0;
	constexpr uint8_t AssignCreature = 1;
	constexpr uint16_t RaceId = 25;

	class CharmRuneTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);

			savedCharms = g_game().getCharmList();
			auto gut = std::make_shared<Charm>();
			gut->id = CHARM_GUT;
			gut->category = CHARM_MINOR;
			gut->binary = 1 << CHARM_GUT;
			gut->points = { 100, 200, 300 };
			g_game().getCharmList() = { gut };
			charm = gut;

			player = std::make_shared<Player>();
			player->setGroup(std::make_shared<Group>());
		}

		void TearDown() override {
			g_game().getCharmList() = savedCharms;
			DI::setTestContainer(previousContainer);
		}

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
		std::vector<std::shared_ptr<Charm>> savedCharms;
		std::shared_ptr<Charm> charm;
		std::shared_ptr<Player> player;
	};

} // namespace

// Regression: the assign action never checked that the rune was unlocked, so a crafted
// packet placed a locked charm on a creature (Gut needs no tier and paid out for free).
TEST_F(CharmRuneTest, LockedCharmCannotBeAssignedToACreature) {
	g_iobestiary().sendBuyCharmRune(player, AssignCreature, CHARM_GUT, RaceId);

	EXPECT_EQ(0, player->parseRacebyCharm(CHARM_GUT));
	EXPECT_EQ(0, player->getUsedRunesBit());
}

TEST_F(CharmRuneTest, UnlockedCharmIsAssignedToTheCreature) {
	player->setUnlockedRunesBit(charm->binary);

	g_iobestiary().sendBuyCharmRune(player, AssignCreature, CHARM_GUT, RaceId);

	EXPECT_EQ(RaceId, player->parseRacebyCharm(CHARM_GUT));
	EXPECT_TRUE(g_iobestiary().hasCharmUnlockedRuneBit(charm, player->getUsedRunesBit()));
}

TEST_F(CharmRuneTest, UnknownCharmIdFromThePacketIsIgnored) {
	g_iobestiary().sendBuyCharmRune(player, UnlockCharm, static_cast<charmRune_t>(-56), RaceId);
	g_iobestiary().sendBuyCharmRune(player, AssignCreature, static_cast<charmRune_t>(120), RaceId);

	EXPECT_EQ(0, player->getUnlockedRunesBit());
	EXPECT_EQ(0, player->getUsedRunesBit());
}

TEST(CharmTierTest, OutOfRangeCharmIdHasNoTierAndCannotBeSet) {
	auto player = std::make_shared<Player>();
	player->setGroup(std::make_shared<Group>());
	const auto pastTheTable = static_cast<charmRune_t>(magic_enum::enum_count<charmRune_t>() + 1);

	player->setCharmTier(pastTheTable, 3);
	player->setCharmTier(CHARM_NONE, 3);

	EXPECT_EQ(0, player->getCharmTier(pastTheTable));
	EXPECT_EQ(0, player->getCharmTier(CHARM_NONE));
}
