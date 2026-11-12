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
#include "creatures/fake_world_registry.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "lib/logging/in_memory_logger.hpp"

// Player components read who is online and the world type through the WorldRegistry port.
class WorldRegistryTest : public ::testing::Test {
protected:
	void SetUp() override {
		previousContainer = DI::getTestContainer();
		injector = std::make_unique<di::extension::injector<>>();
		InMemoryLogger::install(*injector);
		InMemoryConfigManager::install(*injector);
		FakeWorldRegistry::install(*injector);
		DI::setTestContainer(injector.get());
		config = &dynamic_cast<InMemoryConfigManager &>(IConfigManager::getInstance());
		world = &FakeWorldRegistry::installed();
		ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));

		player = makePlayer(1, "Registry Player");
	}

	void TearDown() override {
		world->players.clear();
		player.reset();
		DI::setTestContainer(previousContainer);
		injector.reset();
	}

	static std::shared_ptr<Player> makePlayer(uint32_t guid, const std::string &name) {
		auto created = std::make_shared<Player>();
		created->setGroup(std::make_shared<Group>());
		created->setGUID(guid);
		created->setName(name);
		created->setID();
		return created;
	}

	std::unique_ptr<di::extension::injector<>> injector;
	di::extension::injector<>* previousContainer = nullptr;
	InMemoryConfigManager* config = nullptr;
	FakeWorldRegistry* world = nullptr;
	std::shared_ptr<Player> player;
};

TEST_F(WorldRegistryTest, UnboundTestContainerFallsBackToGameAdapter) {
	DI::setTestContainer(previousContainer);
	EXPECT_EQ(nullptr, dynamic_cast<FakeWorldRegistry*>(&WorldRegistry::getInstance()));
	DI::setTestContainer(injector.get());
}

TEST_F(WorldRegistryTest, LoginRegistersAndLogoutUnregistersThePlayer) {
	player->session().addList();
	EXPECT_EQ(1U, world->additions);
	EXPECT_EQ(player, world->getPlayerByID(player->getID(), false));

	player->session().removeList();
	EXPECT_EQ(1U, world->removals);
	EXPECT_TRUE(world->players.empty());
}

TEST_F(WorldRegistryTest, RemovingPlayerWithoutClientRemovesTheCreatureAsLogout) {
	player->session().removePlayer(false, false);

	ASSERT_EQ(1U, world->removedCreatures.size());
	EXPECT_EQ(player, world->removedCreatures.front());
	EXPECT_TRUE(world->lastRemovalWasLogout);
}

TEST_F(WorldRegistryTest, KillInPvpEnforcedWorldIsNeverUnjustified) {
	config->setNumber(DAY_KILLS_TO_RED, 1);
	config->setNumber(WEEK_KILLS_TO_RED, 5);
	config->setNumber(MONTH_KILLS_TO_RED, 10);
	config->setNumber(RED_SKULL_DURATION, 1);
	world->worldType = WORLD_TYPE_PVP_ENFORCED;

	player->deathComponent().addUnjustifiedDead(makePlayer(2, "Victim"));

	EXPECT_EQ(SKULL_NONE, player->getSkull());
}

TEST_F(WorldRegistryTest, SameKillInOpenPvpWorldGivesRedSkull) {
	config->setNumber(DAY_KILLS_TO_RED, 1);
	config->setNumber(WEEK_KILLS_TO_RED, 5);
	config->setNumber(MONTH_KILLS_TO_RED, 10);
	config->setNumber(RED_SKULL_DURATION, 1);
	world->worldType = WORLD_TYPE_PVP;

	player->deathComponent().addUnjustifiedDead(makePlayer(2, "Victim"));

	EXPECT_EQ(SKULL_RED, player->getSkull());
}
