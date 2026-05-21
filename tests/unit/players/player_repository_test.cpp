/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/player.hpp"

class InMemoryPlayerRepositoryTest : public ::testing::Test {
protected:
	InMemoryPlayerRepository repo;
};

TEST_F(InMemoryPlayerRepositoryTest, SaveAndLoadByName) {
	auto player = std::make_shared<Player>();
	player->setName("TestPlayer");
	player->setGUID(42);
	ASSERT_TRUE(repo.savePlayer(player));

	auto loaded = std::make_shared<Player>();
	ASSERT_TRUE(repo.loadPlayerByName(loaded, "TestPlayer"));
	EXPECT_EQ("TestPlayer", loaded->getName());
	EXPECT_EQ(42u, loaded->getGUID());
}

TEST_F(InMemoryPlayerRepositoryTest, SaveAndLoadById) {
	auto player = std::make_shared<Player>();
	player->setName("ByIdPlayer");
	player->setGUID(99);
	ASSERT_TRUE(repo.savePlayer(player));

	auto loaded = std::make_shared<Player>();
	ASSERT_TRUE(repo.loadPlayerById(loaded, 99));
	EXPECT_EQ("ByIdPlayer", loaded->getName());
	EXPECT_EQ(99u, loaded->getGUID());
}

TEST_F(InMemoryPlayerRepositoryTest, LoadByNameReturnsFalseWhenAbsent) {
	auto player = std::make_shared<Player>();
	EXPECT_FALSE(repo.loadPlayerByName(player, "NonExistent"));
}

TEST_F(InMemoryPlayerRepositoryTest, LoadByIdReturnsFalseWhenAbsent) {
	auto player = std::make_shared<Player>();
	EXPECT_FALSE(repo.loadPlayerById(player, 12345));
}

TEST_F(InMemoryPlayerRepositoryTest, ResetClearsAllPlayers) {
	auto player = std::make_shared<Player>();
	player->setName("WillBeGone");
	player->setGUID(7);
	repo.savePlayer(player);
	repo.reset();

	auto loaded = std::make_shared<Player>();
	EXPECT_FALSE(repo.loadPlayerByName(loaded, "WillBeGone"));
	EXPECT_FALSE(repo.loadPlayerById(loaded, 7));
}
