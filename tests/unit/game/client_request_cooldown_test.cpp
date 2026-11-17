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
#include "test_items.hpp"

namespace {

	// Packets with a costly answer and no interval (Phase 23.5, values chosen by the user):
	// exiva restrictions resolve up to 4 x maxExivaWhitelist names in the database per edit, and
	// proficiency action 0x01 sends one window per trained weapon.
	class ClientRequestCooldownTest : public ::testing::Test {
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

			player = makePlayer(401);
			other = makePlayer(402);
		}

		void TearDown() override {
			service.reset();
			DI::setTestContainer(previousContainer);
			injector.reset();
		}

		static std::shared_ptr<Player> makePlayer(uint32_t guid) {
			auto created = std::make_shared<Player>(nullptr);
			created->setGroup(std::make_shared<Group>());
			created->setGUID(guid);
			created->setID();
			return created;
		}

		Game game;
		std::unique_ptr<PlayerInteractionService> service;
		std::shared_ptr<Player> player;
		std::shared_ptr<Player> other;
		int64_t clockMs = 1'000'000;
		std::unique_ptr<di::extension::injector<>> injector;
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(ClientRequestCooldownTest, ExivaRestrictionsEditOnceEveryTwoSeconds) {
	EXPECT_TRUE(service->admitExivaRestrictionsEdit(player));
	clockMs += PlayerInteractionService::ExivaRestrictionsCooldownMs - 1;
	EXPECT_FALSE(service->admitExivaRestrictionsEdit(player));
	EXPECT_TRUE(service->admitExivaRestrictionsEdit(other));
	clockMs += 1;
	EXPECT_TRUE(service->admitExivaRestrictionsEdit(player));
}

TEST_F(ClientRequestCooldownTest, ProficiencyListOncePerSecond) {
	EXPECT_TRUE(service->admitProficiencyListing(player));
	EXPECT_FALSE(service->admitProficiencyListing(player));
	clockMs += PlayerInteractionService::ProficiencyListCooldownMs;
	EXPECT_TRUE(service->admitProficiencyListing(player));
}

TEST_F(ClientRequestCooldownTest, TheTwoLimitsAreIndependent) {
	EXPECT_TRUE(service->admitExivaRestrictionsEdit(player));
	EXPECT_TRUE(service->admitProficiencyListing(player));
}

TEST_F(ClientRequestCooldownTest, NoPlayerIsRefused) {
	EXPECT_FALSE(service->admitExivaRestrictionsEdit(nullptr));
	EXPECT_FALSE(service->admitProficiencyListing(nullptr));
}
