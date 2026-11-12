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

namespace {

	// The datapack writes each rule violation report to reports/players/<reporter>-<target>-<type>.txt,
	// with the target name taken from the packet.
	class RuleViolationReportTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			injector = std::make_unique<di::extension::injector<>>();
			InMemoryLogger::install(*injector);
			InMemoryConfigManager::install(*injector);
			DI::setTestContainer(injector.get());

			service = std::make_unique<PlayerInteractionService>(game, IConfigManager::getInstance());
			service->now = [this] { return clockMs; };

			player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(654);
			player->setName("Reporter");
			player->setID();
			game.addPlayer(player);
		}

		void TearDown() override {
			service.reset();
			DI::setTestContainer(previousContainer);
			injector.reset();
		}

		void report(const std::string &target, const std::string &comment = "botting") {
			service->playerReportRuleViolationReport(player->getID(), target, 0, 0, comment, "");
		}

		Game game;
		std::unique_ptr<PlayerInteractionService> service;
		std::shared_ptr<Player> player;
		int64_t clockMs = 1'000'000;
		std::unique_ptr<di::extension::injector<>> injector;
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST(RuleViolationReportName, AcceptsPlayerNames) {
	EXPECT_TRUE(PlayerInteractionService::isReportableName("Bob"));
	EXPECT_TRUE(PlayerInteractionService::isReportableName("Sir Bob Junior"));
	EXPECT_TRUE(PlayerInteractionService::isReportableName("D'artagnan"));
	EXPECT_TRUE(PlayerInteractionService::isReportableName("Bob-x"));
}

// Regression: the target name is part of a file path. With a directory such as
// reports/players/Bob-x (created for a reporter named "Bob-x"), "Bob" reporting
// "x/../../../file" appended attacker text to a file outside the reports folder.
TEST(RuleViolationReportName, RefusesPathAndControlCharacters) {
	EXPECT_FALSE(PlayerInteractionService::isReportableName("x/../../../etc/target"));
	EXPECT_FALSE(PlayerInteractionService::isReportableName("..\\..\\boot"));
	EXPECT_FALSE(PlayerInteractionService::isReportableName("a.b"));
	EXPECT_FALSE(PlayerInteractionService::isReportableName(std::string("nul\0byte", 8)));
	EXPECT_FALSE(PlayerInteractionService::isReportableName("line\nbreak"));
	EXPECT_FALSE(PlayerInteractionService::isReportableName(""));
	EXPECT_FALSE(PlayerInteractionService::isReportableName(std::string(NETWORKMESSAGE_PLAYERNAME_MAXLENGTH + 1, 'a')));
}

TEST_F(RuleViolationReportTest, UnsafeTargetIsRejected) {
	report("x/../../../etc/target");

	EXPECT_EQ(1U, service->rejectedRuleViolationReports());
}

// Each distinct target used to create a new file of up to ~64 KB, with no rate limit.
TEST_F(RuleViolationReportTest, ReportsAreThrottledPerPlayer) {
	report("Alice");
	report("Bob");
	EXPECT_EQ(1U, service->rejectedRuleViolationReports());

	clockMs += PlayerInteractionService::RuleViolationReportCooldownMs;
	report("Bob", std::string(PlayerInteractionService::MaxBugReportLength * 4, 'x'));
	EXPECT_EQ(1U, service->rejectedRuleViolationReports());
}
