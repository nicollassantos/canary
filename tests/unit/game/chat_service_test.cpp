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
#include "game/chat/chat_service.hpp"
#include "game/game.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "utils/utils_definitions.hpp"
#include "config/in_memory_config_manager.hpp"

namespace {

	const Position kPosA { 100, 100, 7 };
	const Position kPosB { 101, 100, 7 };

	// No-op condition for YELLTICKS
	class TestYellTicksCondition final : public Condition {
	public:
		TestYellTicksCondition() :
			Condition(CONDITIONID_DEFAULT, CONDITION_YELLTICKS, 30000) { }

		void endCondition(std::shared_ptr<Creature>) override { }
		void addCondition(std::shared_ptr<Creature>, std::shared_ptr<Condition>) override { }

		std::shared_ptr<Condition> clone() const override {
			return std::make_shared<TestYellTicksCondition>();
		}
	};

	std::shared_ptr<Player> makeChatPlayer(const Position &pos, uint32_t guid = 9001) {
		auto player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	class ChatServiceTest : public ::testing::Test {
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
		InMemoryConfigManager config;
		ChatService chat { game, config };

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- playerBroadcastMessage ---

// player without CanBroadcast flag → false
TEST_F(ChatServiceTest, PlayerBroadcastMessage_NoCanBroadcastFlag_ReturnsFalse) {
	const auto player = makeChatPlayer(kPosA);
	ASSERT_FALSE(player->hasFlag(PlayerFlags_t::CanBroadcast));
	EXPECT_FALSE(chat.playerBroadcastMessage(player, "hello"));
}

// player with CanBroadcast flag, no other players in game → true (empty loop)
TEST_F(ChatServiceTest, PlayerBroadcastMessage_WithCanBroadcastFlag_ReturnsTrue) {
	const auto player = makeChatPlayer(kPosA);
	player->setFlag(PlayerFlags_t::CanBroadcast);
	ASSERT_TRUE(player->hasFlag(PlayerFlags_t::CanBroadcast));
	EXPECT_TRUE(chat.playerBroadcastMessage(player, "broadcast"));
}

// --- playerYell ---

// level 1 player → false (may not yell at level 1)
TEST_F(ChatServiceTest, PlayerYell_LevelOne_ReturnsFalse) {
	const auto player = makeChatPlayer(kPosA);
	ASSERT_EQ(1u, player->getLevel());
	EXPECT_FALSE(chat.playerYell(player, "YELL"));
}

// level > 1 with YELLTICKS condition → false (exhausted guard)
TEST_F(ChatServiceTest, PlayerYell_YellTicksCondition_ReturnsFalse) {
	const auto player = makeChatPlayer(kPosA);
	player->setLevel(10);
	ASSERT_TRUE(player->addCondition(std::make_shared<TestYellTicksCondition>()));
	ASSERT_TRUE(player->hasCondition(CONDITION_YELLTICKS));
	EXPECT_FALSE(chat.playerYell(player, "YELL"));
}

// level > 1, no YELLTICKS → true (yell proceeds; internalCreatureSay is no-op with no map tiles)
TEST_F(ChatServiceTest, PlayerYell_Level10NoCondition_ReturnsTrue) {
	const auto player = makeChatPlayer(kPosA);
	player->setLevel(10);
	ASSERT_FALSE(player->hasCondition(CONDITION_YELLTICKS));
	EXPECT_TRUE(chat.playerYell(player, "YELL"));
}

// --- playerSpeakTo ---

// receiver not registered in game → false
TEST_F(ChatServiceTest, PlayerSpeakTo_UnknownReceiver_ReturnsFalse) {
	const auto player = makeChatPlayer(kPosA);
	EXPECT_FALSE(chat.playerSpeakTo(player, TALKTYPE_PRIVATE_FROM, "GhostPlayer", "hello"));
}

// receiver is same as sender (registered) → sends to self, returns true
TEST_F(ChatServiceTest, PlayerSpeakTo_RegisteredReceiver_ReturnsTrue) {
	const auto playerA = makeChatPlayer(kPosA, 9001);
	const auto playerB = makeChatPlayer(kPosB, 9002);
	playerB->setName("ReceiverB");
	game.addPlayer(playerB);

	const bool result = chat.playerSpeakTo(playerA, TALKTYPE_PRIVATE_FROM, "ReceiverB", "hello");
	EXPECT_TRUE(result);

	game.removePlayer(playerB);
}
