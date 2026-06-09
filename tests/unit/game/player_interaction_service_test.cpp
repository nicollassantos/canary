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
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

namespace {
	const Position kTestPos { 150, 150, 7 };

	std::shared_ptr<Player> makePlacedPlayer(uint32_t guid, const Position &pos) {
		auto player = std::make_shared<Player>();
		player->setName("InteractionTest" + std::to_string(guid));
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	class PlayerInteractionServiceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			test_items::seedFallbackTestItems();
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

		void SetUp() override {
			player = makePlacedPlayer(7001, kTestPos);
			game.addPlayer(player);
		}

	protected:
		Game game;
		std::shared_ptr<Player> player;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

// playerCloseContainer — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerCloseContainer_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerCloseContainer(0xDEADBEEF, 0));
}

// playerCloseContainer — noop when container id is invalid for valid player
TEST_F(PlayerInteractionServiceTest, PlayerCloseContainer_DoesNothing_WhenContainerNotOpen) {
	EXPECT_NO_FATAL_FAILURE(game.playerCloseContainer(player->getID(), 99));
}

// playerMoveUpContainer — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerMoveUpContainer_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerMoveUpContainer(0xDEADBEEF, 0));
}

// playerUpdateContainer — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerUpdateContainer_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerUpdateContainer(0xDEADBEEF, 0));
}

// playerRotateItem — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerRotateItem_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerRotateItem(0xDEADBEEF, kTestPos, 0, 100));
}

// playerBrowseField — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerBrowseField_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerBrowseField(0xDEADBEEF, kTestPos));
}

// playerWriteItem — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerWriteItem_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerWriteItem(0xDEADBEEF, 0, "text"));
}

// playerStashWithdraw — noop when player not found
TEST_F(PlayerInteractionServiceTest, PlayerStashWithdraw_DoesNothing_WhenPlayerNotFound) {
	EXPECT_NO_FATAL_FAILURE(game.playerStashWithdraw(0xDEADBEEF, 100, 1, 0));
}
