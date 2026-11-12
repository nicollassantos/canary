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
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "creatures/recording_creature_feedback.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {
	constexpr uint16_t kSorcerer = 1;
}

// Player components report visible state changes through the CreatureFeedback port.
class CreatureFeedbackTest : public ::testing::Test {
protected:
	void SetUp() override {
		previousContainer = DI::getTestContainer();
		injector = std::make_unique<di::extension::injector<>>();
		InMemoryLogger::install(*injector);
		InMemoryConfigManager::install(*injector);
		RecordingCreatureFeedback::install(*injector);
		DI::setTestContainer(injector.get());
		feedback = &RecordingCreatureFeedback::installed();
		ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));

		player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		ASSERT_TRUE(player->setVocation(kSorcerer));
	}

	void TearDown() override {
		player.reset();
		DI::setTestContainer(previousContainer);
		injector.reset();
	}

	std::unique_ptr<di::extension::injector<>> injector;
	di::extension::injector<>* previousContainer = nullptr;
	RecordingCreatureFeedback* feedback = nullptr;
	std::shared_ptr<Player> player;
};

TEST_F(CreatureFeedbackTest, UnboundTestContainerFallsBackToGameAdapter) {
	DI::setTestContainer(previousContainer);
	EXPECT_EQ(nullptr, dynamic_cast<RecordingCreatureFeedback*>(&CreatureFeedback::getInstance()));
	DI::setTestContainer(injector.get());
}

TEST_F(CreatureFeedbackTest, ImbuementSpeedIsAppliedAndReverted) {
	Imbuement imbuement(1, 1);
	imbuement.speed = 30;

	player->imbuementComponent().addItemImbuementStats(&imbuement);
	player->imbuementComponent().removeItemImbuementStats(&imbuement);

	ASSERT_EQ(2U, feedback->speedChanges.size());
	EXPECT_EQ(player, feedback->speedChanges[0].creature);
	EXPECT_EQ(30, feedback->speedChanges[0].delta);
	EXPECT_EQ(-30, feedback->speedChanges[1].delta);
}

TEST_F(CreatureFeedbackTest, ImbuementWithoutSpeedDoesNotTouchSpeed) {
	Imbuement imbuement(1, 1);

	player->imbuementComponent().addItemImbuementStats(&imbuement);

	EXPECT_TRUE(feedback->speedChanges.empty());
}

TEST_F(CreatureFeedbackTest, LevelUpRefreshesSpeedHealthAndManaBars) {
	const auto level = player->getLevel();

	player->experienceComponent().addExperience(nullptr, Player::getExpForLevel(level + 1) - player->getExperience());

	EXPECT_EQ(level + 1, player->getLevel());
	ASSERT_EQ(1U, feedback->speedChanges.size());
	EXPECT_EQ(0, feedback->speedChanges.front().delta);
	EXPECT_EQ(1U, feedback->healthUpdates);
	EXPECT_EQ(1U, feedback->manaUpdates);
}

TEST_F(CreatureFeedbackTest, ExperienceWithinLevelSendsNoBarRefresh) {
	const auto level = player->getLevel();

	player->experienceComponent().addExperience(nullptr, 1);

	EXPECT_EQ(level, player->getLevel());
	EXPECT_TRUE(feedback->speedChanges.empty());
	EXPECT_EQ(0U, feedback->healthUpdates);
	EXPECT_EQ(0U, feedback->manaUpdates);
}
