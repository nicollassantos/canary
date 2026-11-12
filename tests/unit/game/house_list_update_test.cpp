/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/in_memory_config_manager.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/interaction/player_interaction_service.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "map/house/house.hpp"
#include "map/map_definitions.hpp"
#include "test_items.hpp"

namespace {

	// A house access list edit parses the whole text and may query the database once per name, so
	// oversized texts and rapid repeats are refused before parsing.
	class HouseListUpdateTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			injector = std::make_unique<di::extension::injector<>>();
			InMemoryLogger::install(*injector);
			InMemoryConfigManager::install(*injector);
			DI::setTestContainer(injector.get());
			TestItems::init();

			service = std::make_unique<PlayerInteractionService>(game, IConfigManager::getInstance());
			service->now = [this] { return clockMs; };

			player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(321);
			player->setName("House Editor");
			player->setID();
			game.addPlayer(player);
		}

		void TearDown() override {
			service.reset();
			DI::setTestContainer(previousContainer);
			injector.reset();
		}

		void update(const std::string &text) {
			service->playerUpdateHouseWindow(player->getID(), 0, 1, text);
		}

		Game game;
		std::unique_ptr<PlayerInteractionService> service;
		std::shared_ptr<Player> player;
		int64_t clockMs = 1'000'000;
		std::unique_ptr<di::extension::injector<>> injector;
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(HouseListUpdateTest, OversizedListIsRejectedBeforeParsing) {
	update(std::string(PlayerInteractionService::MaxHouseListLength + 1, 'a'));

	EXPECT_EQ(1U, service->rejectedHouseListUpdates());
}

TEST_F(HouseListUpdateTest, ListAtTheLimitIsAccepted) {
	update(std::string(PlayerInteractionService::MaxHouseListLength, 'a'));

	EXPECT_EQ(0U, service->rejectedHouseListUpdates());
}

TEST_F(HouseListUpdateTest, EditsOnePerSecond) {
	update("Friend");
	update("Friend");
	EXPECT_EQ(1U, service->rejectedHouseListUpdates());

	clockMs += PlayerInteractionService::HouseListCooldownMs;
	update("Friend");
	EXPECT_EQ(1U, service->rejectedHouseListUpdates());
}

TEST_F(HouseListUpdateTest, RejectedEditStillClosesTheEditWindow) {
	player->setEditHouse(std::make_shared<House>(7), GUEST_LIST);

	update(std::string(PlayerInteractionService::MaxHouseListLength + 1, 'a'));

	uint32_t windowTextId = 0;
	uint32_t listId = 0;
	EXPECT_EQ(nullptr, player->getEditHouse(windowTextId, listId));
}

// Bug reports are appended to a file per player by the datapack: one report per minute per player.
TEST_F(HouseListUpdateTest, BugReportsOncePerMinute) {
	const Position nowhere;
	service->playerReportBug(player->getID(), "first", nowhere, 0);
	service->playerReportBug(player->getID(), "spam", nowhere, 0);
	EXPECT_EQ(1U, service->rejectedBugReports());

	clockMs += PlayerInteractionService::BugReportCooldownMs;
	service->playerReportBug(player->getID(), std::string(PlayerInteractionService::MaxBugReportLength * 4, 'x'), nowhere, 0);
	EXPECT_EQ(1U, service->rejectedBugReports());
}
