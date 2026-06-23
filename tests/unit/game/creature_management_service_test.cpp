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
#include "lib/logging/in_memory_logger.hpp"

namespace {

	class CreatureManagementServiceTest : public ::testing::Test {
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

// --- getMonsterByID ---

// id == 0 → nullptr
TEST_F(CreatureManagementServiceTest, GetMonsterByID_ZeroId_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getMonsterByID(0));
}

// id not registered in index → nullptr
TEST_F(CreatureManagementServiceTest, GetMonsterByID_UnknownId_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getMonsterByID(999999));
}

// --- getNpcByID ---

// id == 0 → nullptr
TEST_F(CreatureManagementServiceTest, GetNpcByID_ZeroId_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getNpcByID(0));
}

// id not registered in index → nullptr
TEST_F(CreatureManagementServiceTest, GetNpcByID_UnknownId_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getNpcByID(999999));
}

// --- getPlayerByID ---

// id not in players map, allowOffline=false → nullptr
TEST_F(CreatureManagementServiceTest, GetPlayerByID_NotRegistered_NoOffline_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getPlayerByID(12345, false));
}

// --- getCreatureByName ---

// empty name → nullptr
TEST_F(CreatureManagementServiceTest, GetCreatureByName_EmptyString_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getCreatureByName(""));
}

// name not in any index → nullptr
TEST_F(CreatureManagementServiceTest, GetCreatureByName_UnknownName_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getCreatureByName("NoSuchCreature"));
}

// --- getNpcByName ---

// empty name → nullptr
TEST_F(CreatureManagementServiceTest, GetNpcByName_EmptyString_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getNpcByName(""));
}

// name not registered → nullptr
TEST_F(CreatureManagementServiceTest, GetNpcByName_UnknownName_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getNpcByName("Nonexistent Npc"));
}

// --- getPlayerByName ---

// empty string → nullptr
TEST_F(CreatureManagementServiceTest, GetPlayerByName_EmptyString_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getPlayerByName("", false));
}

// name not registered, allowOffline=false → nullptr
TEST_F(CreatureManagementServiceTest, GetPlayerByName_NotRegistered_NoOffline_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getPlayerByName("GhostPlayer", false));
}

// player is registered → returned
TEST_F(CreatureManagementServiceTest, GetPlayerByName_RegisteredPlayer_ReturnsPlayer) {
	auto player = std::make_shared<Player>();
	player->setName("Tester");
	player->setID();
	game.addPlayer(player);

	const auto found = game.getPlayerByName("Tester", false);
	EXPECT_EQ(player, found);

	game.removePlayer(player);
}

// --- getPlayerByGUID ---

// guid == 0 → nullptr
TEST_F(CreatureManagementServiceTest, GetPlayerByGUID_ZeroGuid_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getPlayerByGUID(0, false));
}

// guid not in players map, allowOffline=false → nullptr
TEST_F(CreatureManagementServiceTest, GetPlayerByGUID_UnknownGuid_NoOffline_ReturnsNullptr) {
	EXPECT_EQ(nullptr, game.getPlayerByGUID(99999, false));
}

// --- getPlayerByNameWildcard ---

// empty string → PLAYERWITHTHISNAMEISNOTONLINE
TEST_F(CreatureManagementServiceTest, GetPlayerByNameWildcard_EmptyString_ReturnsNotOnline) {
	std::shared_ptr<Player> player;
	EXPECT_EQ(RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE, game.getPlayerByNameWildcard("", player));
}

// length > 29 → PLAYERWITHTHISNAMEISNOTONLINE
TEST_F(CreatureManagementServiceTest, GetPlayerByNameWildcard_TooLong_ReturnsNotOnline) {
	std::shared_ptr<Player> player;
	const std::string longName(30, 'a');
	EXPECT_EQ(RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE, game.getPlayerByNameWildcard(longName, player));
}

// exact name not registered → PLAYERWITHTHISNAMEISNOTONLINE
TEST_F(CreatureManagementServiceTest, GetPlayerByNameWildcard_ExactUnknown_ReturnsNotOnline) {
	std::shared_ptr<Player> player;
	EXPECT_EQ(RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE, game.getPlayerByNameWildcard("Unknown", player));
}

// wildcard prefix (~) not in tree → PLAYERWITHTHISNAMEISNOTONLINE
TEST_F(CreatureManagementServiceTest, GetPlayerByNameWildcard_WildcardUnknownPrefix_ReturnsNotOnline) {
	std::shared_ptr<Player> player;
	EXPECT_EQ(RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE, game.getPlayerByNameWildcard("xzz~", player));
}
