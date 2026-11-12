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
#include "creatures/creatures_definitions.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/outfit/outfit_service.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

namespace {

	// Spam bots loop "open outfit window" (0xD2) and "set outfit" (0xD3); both are throttled per player.
	class OutfitThrottleTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			injector = std::make_unique<di::extension::injector<>>();
			InMemoryLogger::install(*injector);
			InMemoryConfigManager::install(*injector);
			DI::setTestContainer(injector.get());
			auto &config = dynamic_cast<InMemoryConfigManager &>(IConfigManager::getInstance());
			config.setBoolean(ALLOW_CHANGEOUTFIT, true);
			TestItems::init();

			service = std::make_unique<OutfitService>(game, IConfigManager::getInstance());
			service->now = [this] { return clockMs; };

			player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(91);
			player->setName("Outfit Spammer");
			player->setID();
			game.addPlayer(player);
		}

		void TearDown() override {
			service.reset();
			DI::setTestContainer(previousContainer);
			injector.reset();
		}

		Outfit_t anyOutfit() const {
			Outfit_t outfit;
			outfit.lookType = 128;
			return outfit;
		}

		Game game;
		std::unique_ptr<OutfitService> service;
		std::shared_ptr<Player> player;
		int64_t clockMs = 1'000'000;
		std::unique_ptr<di::extension::injector<>> injector;
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(OutfitThrottleTest, OutfitWindowOncePerSecond) {
	service->playerRequestOutfit(player->getID());
	service->playerRequestOutfit(player->getID());
	EXPECT_EQ(1U, service->throttledRequests());

	clockMs += OutfitService::OutfitWindowCooldownMs;
	service->playerRequestOutfit(player->getID());
	EXPECT_EQ(1U, service->throttledRequests());
}

TEST_F(OutfitThrottleTest, OutfitChangeOncePerHalfSecond) {
	service->playerChangeOutfit(player->getID(), anyOutfit(), false);
	clockMs += OutfitService::OutfitChangeCooldownMs - 1;
	service->playerChangeOutfit(player->getID(), anyOutfit(), false);
	EXPECT_EQ(1U, service->throttledRequests());

	clockMs += 1;
	service->playerChangeOutfit(player->getID(), anyOutfit(), false);
	EXPECT_EQ(1U, service->throttledRequests());
}

TEST_F(OutfitThrottleTest, WindowAndChangeHaveSeparateCooldowns) {
	service->playerRequestOutfit(player->getID());
	service->playerChangeOutfit(player->getID(), anyOutfit(), false);

	EXPECT_EQ(0U, service->throttledRequests());
}

TEST_F(OutfitThrottleTest, InternalPlayerOverloadIsNotThrottled) {
	service->playerChangeOutfit(player, anyOutfit(), false);
	service->playerChangeOutfit(player, anyOutfit(), false);

	EXPECT_EQ(0U, service->throttledRequests());
}
