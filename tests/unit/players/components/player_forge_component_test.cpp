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
#include "creatures/players/components/player_forge_component.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PlayerForgeComponentTest : public ::testing::Test {
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

TEST_F(PlayerForgeComponentTest, GetForgeDusts_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->forgeComponent().getForgeDusts());
}

TEST_F(PlayerForgeComponentTest, SetForgeDusts_SetsExactAmount) {
	player->forgeComponent().setForgeDusts(500);
	EXPECT_EQ(500u, player->forgeComponent().getForgeDusts());
}

TEST_F(PlayerForgeComponentTest, AddForgeDusts_AccumulatesAmount) {
	player->forgeComponent().addForgeDusts(100);
	player->forgeComponent().addForgeDusts(50);
	EXPECT_EQ(150u, player->forgeComponent().getForgeDusts());
}

TEST_F(PlayerForgeComponentTest, RemoveForgeDusts_DecrementsAmount) {
	player->forgeComponent().setForgeDusts(200);
	player->forgeComponent().removeForgeDusts(80);
	EXPECT_EQ(120u, player->forgeComponent().getForgeDusts());
}

TEST_F(PlayerForgeComponentTest, RemoveForgeDusts_ClampsToZero) {
	player->forgeComponent().setForgeDusts(10);
	player->forgeComponent().removeForgeDusts(100);
	EXPECT_EQ(0u, player->forgeComponent().getForgeDusts());
}

TEST_F(PlayerForgeComponentTest, GetForgeDustLevel_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->forgeComponent().getForgeDustLevel());
}

TEST_F(PlayerForgeComponentTest, AddForgeDustLevel_AccumulatesAmount) {
	player->forgeComponent().addForgeDustLevel(3);
	player->forgeComponent().addForgeDustLevel(2);
	EXPECT_EQ(5u, player->forgeComponent().getForgeDustLevel());
}

TEST_F(PlayerForgeComponentTest, RemoveForgeDustLevel_ClampsToZero) {
	player->forgeComponent().addForgeDustLevel(5);
	player->forgeComponent().removeForgeDustLevel(10);
	EXPECT_EQ(0u, player->forgeComponent().getForgeDustLevel());
}
