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
#include "creatures/players/vocations/vocation.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {
	constexpr uint16_t kSorcerer = 1;
	constexpr uint16_t kMasterSorcerer = 5;
}

class PlayerDeathComponentTest : public ::testing::Test {
protected:
	void SetUp() override {
		previousContainer = DI::getTestContainer();
		injector = std::make_unique<di::extension::injector<>>();
		InMemoryLogger::install(*injector);
		InMemoryConfigManager::install(*injector);
		DI::setTestContainer(injector.get());
		config = &dynamic_cast<InMemoryConfigManager &>(IConfigManager::getInstance());
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

	std::shared_ptr<Player> makeVictim(uint32_t guid) const {
		auto victim = std::make_shared<Player>();
		victim->setGroup(std::make_shared<Group>());
		victim->setGUID(guid);
		victim->setName("Victim " + std::to_string(guid));
		return victim;
	}

	std::unique_ptr<di::extension::injector<>> injector;
	di::extension::injector<>* previousContainer = nullptr;
	InMemoryConfigManager* config = nullptr;
	std::shared_ptr<Player> player;
};

// --- getLostPercent: fixed deathLosePercent ---

TEST_F(PlayerDeathComponentTest, FixedPercent_ReturnsConfiguredValue) {
	config->setNumber(DEATH_LOSE_PERCENT, 10);
	EXPECT_DOUBLE_EQ(0.10, player->deathComponent().getLostPercent());
}

TEST_F(PlayerDeathComponentTest, FixedPercent_PromotionReducesThreePoints) {
	config->setNumber(DEATH_LOSE_PERCENT, 10);
	ASSERT_TRUE(player->setVocation(kMasterSorcerer));
	EXPECT_DOUBLE_EQ(0.07, player->deathComponent().getLostPercent());
}

TEST_F(PlayerDeathComponentTest, FixedPercent_EachBlessingReducesOnePoint) {
	config->setNumber(DEATH_LOSE_PERCENT, 10);
	player->addBlessing(2, 1);
	player->addBlessing(3, 1);
	EXPECT_DOUBLE_EQ(0.08, player->deathComponent().getLostPercent());
}

TEST_F(PlayerDeathComponentTest, FixedPercent_NeverNegative) {
	config->setNumber(DEATH_LOSE_PERCENT, 2);
	ASSERT_TRUE(player->setVocation(kMasterSorcerer));
	for (uint8_t blessing = 2; blessing <= 8; ++blessing) {
		player->addBlessing(blessing, 1);
	}
	EXPECT_DOUBLE_EQ(0.0, player->deathComponent().getLostPercent());
}

// --- getLostPercent: level formula (deathLosePercent = -1), low level ---

TEST_F(PlayerDeathComponentTest, Formula_LowLevelLosesTenPercent) {
	config->setNumber(DEATH_LOSE_PERCENT, -1);
	EXPECT_DOUBLE_EQ(0.10, player->deathComponent().getLostPercent());
}

TEST_F(PlayerDeathComponentTest, Formula_LowLevelPromotionReducesLoss) {
	config->setNumber(DEATH_LOSE_PERCENT, -1);
	ASSERT_TRUE(player->setVocation(kMasterSorcerer));
	EXPECT_DOUBLE_EQ(0.07, player->deathComponent().getLostPercent());
}

// --- addUnjustifiedDead ---

TEST_F(PlayerDeathComponentTest, UnjustifiedKill_OfSelfIsIgnored) {
	config->setNumber(DAY_KILLS_TO_RED, 1);
	player->deathComponent().addUnjustifiedDead(player);
	EXPECT_EQ(SKULL_NONE, player->getSkull());
}

TEST_F(PlayerDeathComponentTest, UnjustifiedKill_BelowThresholdKeepsSkull) {
	config->setNumber(DAY_KILLS_TO_RED, 3);
	config->setNumber(WEEK_KILLS_TO_RED, 5);
	config->setNumber(MONTH_KILLS_TO_RED, 10);
	player->deathComponent().addUnjustifiedDead(makeVictim(100));
	EXPECT_EQ(SKULL_NONE, player->getSkull());
}

TEST_F(PlayerDeathComponentTest, UnjustifiedKill_ReachingThresholdGivesRedSkull) {
	config->setNumber(DAY_KILLS_TO_RED, 1);
	config->setNumber(WEEK_KILLS_TO_RED, 5);
	config->setNumber(MONTH_KILLS_TO_RED, 10);
	config->setNumber(RED_SKULL_DURATION, 1);
	player->deathComponent().addUnjustifiedDead(makeVictim(100));
	EXPECT_EQ(SKULL_RED, player->getSkull());
}

TEST_F(PlayerDeathComponentTest, UnjustifiedKill_DoubleThresholdGivesBlackSkull) {
	config->setNumber(DAY_KILLS_TO_RED, 1);
	config->setNumber(WEEK_KILLS_TO_RED, 5);
	config->setNumber(MONTH_KILLS_TO_RED, 10);
	config->setNumber(RED_SKULL_DURATION, 1);
	config->setNumber(BLACK_SKULL_DURATION, 1);
	player->deathComponent().addUnjustifiedDead(makeVictim(100));
	player->deathComponent().addUnjustifiedDead(makeVictim(101));
	EXPECT_EQ(SKULL_BLACK, player->getSkull());
}
