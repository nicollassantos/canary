/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "io/in_memory_guild_repository.hpp"
#include "creatures/players/grouping/guild.hpp"

class InMemoryGuildRepositoryTest : public ::testing::Test {
protected:
	InMemoryGuildRepository repo;
};

TEST_F(InMemoryGuildRepositoryTest, LoadGuild_ReturnsNull_WhenAbsent) {
	EXPECT_EQ(nullptr, repo.loadGuild(99));
}

TEST_F(InMemoryGuildRepositoryTest, SaveAndLoadGuild_ById) {
	auto guild = std::make_shared<Guild>(10, "Warrior Guild");
	repo.saveGuild(guild);

	auto loaded = repo.loadGuild(10);
	ASSERT_NE(nullptr, loaded);
	EXPECT_EQ(10u, loaded->getId());
	EXPECT_EQ("Warrior Guild", loaded->getName());
}

TEST_F(InMemoryGuildRepositoryTest, GetGuildIdByName_ReturnsId) {
	auto guild = std::make_shared<Guild>(5, "Mage Circle");
	repo.addGuild(guild);

	EXPECT_EQ(5u, repo.getGuildIdByName("Mage Circle"));
}

TEST_F(InMemoryGuildRepositoryTest, GetGuildIdByName_ReturnsZero_WhenAbsent) {
	EXPECT_EQ(0u, repo.getGuildIdByName("Unknown Guild"));
}

TEST_F(InMemoryGuildRepositoryTest, SaveGuild_UpdatesExisting) {
	auto guild = std::make_shared<Guild>(7, "Old Name");
	repo.saveGuild(guild);

	auto updated = std::make_shared<Guild>(7, "New Name");
	repo.saveGuild(updated);

	auto loaded = repo.loadGuild(7);
	ASSERT_NE(nullptr, loaded);
	EXPECT_EQ("New Name", loaded->getName());
}

TEST_F(InMemoryGuildRepositoryTest, GetWarList_ReturnsEmpty_WhenNoneSet) {
	GuildWarVector wars;
	repo.getWarList(1, wars);
	EXPECT_TRUE(wars.empty());
}

TEST_F(InMemoryGuildRepositoryTest, SetAndGetWarList) {
	GuildWarVector enemies = { 10, 20, 30 };
	repo.setWarList(1, enemies);

	GuildWarVector wars;
	repo.getWarList(1, wars);
	ASSERT_EQ(3u, wars.size());
	EXPECT_EQ(10u, wars[0]);
	EXPECT_EQ(20u, wars[1]);
	EXPECT_EQ(30u, wars[2]);
}

TEST_F(InMemoryGuildRepositoryTest, ResetClearsAllData) {
	auto guild = std::make_shared<Guild>(3, "Temp Guild");
	repo.saveGuild(guild);
	repo.setWarList(3, { 5, 6 });
	repo.reset();

	EXPECT_EQ(nullptr, repo.loadGuild(3));
	EXPECT_EQ(0u, repo.getGuildIdByName("Temp Guild"));

	GuildWarVector wars;
	repo.getWarList(3, wars);
	EXPECT_TRUE(wars.empty());
}
