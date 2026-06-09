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
#include "creatures/players/components/player_training_component.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PlayerTrainingComponentTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector);
		DI::setTestContainer(&injector);
	}

	void SetUp() override {
		player = std::make_shared<Player>();
	}

	std::shared_ptr<Player> player;

private:
	inline static di::extension::injector<> injector {};
};

TEST_F(PlayerTrainingComponentTest, GetOfflineTrainingTime_ReturnsZero_Initially) {
	EXPECT_EQ(0, player->trainingComponent().getOfflineTrainingTime());
}

TEST_F(PlayerTrainingComponentTest, AddOfflineTrainingTime_AccumulatesTime) {
	player->trainingComponent().addOfflineTrainingTime(1000);
	player->trainingComponent().addOfflineTrainingTime(500);
	EXPECT_EQ(1500, player->trainingComponent().getOfflineTrainingTime());
}

TEST_F(PlayerTrainingComponentTest, AddOfflineTrainingTime_ClampsToMax) {
	const int32_t maxMs = 12 * 3600 * 1000;
	player->trainingComponent().addOfflineTrainingTime(maxMs);
	player->trainingComponent().addOfflineTrainingTime(maxMs);
	EXPECT_EQ(maxMs, player->trainingComponent().getOfflineTrainingTime());
}

TEST_F(PlayerTrainingComponentTest, RemoveOfflineTrainingTime_DecrementsTime) {
	player->trainingComponent().addOfflineTrainingTime(2000);
	player->trainingComponent().removeOfflineTrainingTime(800);
	EXPECT_EQ(1200, player->trainingComponent().getOfflineTrainingTime());
}

TEST_F(PlayerTrainingComponentTest, RemoveOfflineTrainingTime_ClampsToZero) {
	player->trainingComponent().addOfflineTrainingTime(100);
	player->trainingComponent().removeOfflineTrainingTime(9999);
	EXPECT_EQ(0, player->trainingComponent().getOfflineTrainingTime());
}

TEST_F(PlayerTrainingComponentTest, GetOfflineTrainingSkill_ReturnsDefaultValue_Initially) {
	EXPECT_EQ(-1, player->trainingComponent().getOfflineTrainingSkill());
}

TEST_F(PlayerTrainingComponentTest, SetOfflineTrainingSkill_StoresSkill) {
	player->trainingComponent().setOfflineTrainingSkill(3);
	EXPECT_EQ(3, player->trainingComponent().getOfflineTrainingSkill());
}
