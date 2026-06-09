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

TEST_F(InMemoryPlayerRepositoryTest, GetGuidByName_ReturnsCorrectGuid) {
	auto player = std::make_shared<Player>();
	player->setName("Hermes");
	player->setGUID(55);
	repo.savePlayer(player);

	EXPECT_EQ(55u, repo.getGuidByName("Hermes"));
}

TEST_F(InMemoryPlayerRepositoryTest, GetGuidByName_ReturnsZeroWhenAbsent) {
	EXPECT_EQ(0u, repo.getGuidByName("NoSuchPlayer"));
}

TEST_F(InMemoryPlayerRepositoryTest, GetNameByGuid_ReturnsCorrectName) {
	auto player = std::make_shared<Player>();
	player->setName("Athena");
	player->setGUID(77);
	repo.savePlayer(player);

	EXPECT_EQ("Athena", repo.getNameByGuid(77));
}

TEST_F(InMemoryPlayerRepositoryTest, GetNameByGuid_ReturnsEmptyWhenAbsent) {
	EXPECT_EQ("", repo.getNameByGuid(999));
}

TEST_F(InMemoryPlayerRepositoryTest, IncreaseBankBalance_AccumulatesAmount) {
	repo.increaseBankBalance(10, 500);
	repo.increaseBankBalance(10, 300);

	EXPECT_EQ(800u, repo.getBankBalance(10));
}

TEST_F(InMemoryPlayerRepositoryTest, GetBankBalance_ReturnsZeroWhenNoEntry) {
	EXPECT_EQ(0u, repo.getBankBalance(42));
}

TEST_F(InMemoryPlayerRepositoryTest, IncreaseBankBalance_IndependentPerGuid) {
	repo.increaseBankBalance(1, 100);
	repo.increaseBankBalance(2, 200);

	EXPECT_EQ(100u, repo.getBankBalance(1));
	EXPECT_EQ(200u, repo.getBankBalance(2));
}

TEST_F(InMemoryPlayerRepositoryTest, ResetClearsBankBalances) {
	repo.increaseBankBalance(5, 1000);
	repo.reset();

	EXPECT_EQ(0u, repo.getBankBalance(5));
}
