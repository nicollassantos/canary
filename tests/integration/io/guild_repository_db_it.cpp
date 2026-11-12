/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/grouping/guild.hpp"
#include "io/guild_repository_db.hpp"
#include "test_env.hpp"

namespace it_guild_repo_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 20000000;
		return base + counter.fetch_add(1);
	}

	// Inserts account + owner player + guild (the oncreate_guilds trigger adds the default ranks).
	uint32_t createGuild(Database &db, const std::string &name, uint64_t balance) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'guild_acc_{}', 'guild_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('guild_owner_{}', {}, '')", seed, seed)));
		const auto ownerId = static_cast<uint32_t>(db.getLastInsertId());
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `guilds` (`name`, `ownerid`, `creationdata`, `balance`) VALUES ({}, {}, 0, {})", db.escapeString(name), ownerId, balance)));
		return static_cast<uint32_t>(db.getLastInsertId());
	}

	TEST(GuildRepositoryDBTest, LoadGuildReadsNameBalanceAndRanks) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto name = fmt::format("Guild {}", uniqueSeed());
			const auto guildId = createGuild(db, name, 1500);

			GuildRepositoryDB repository(db);
			const auto guild = repository.loadGuild(guildId);
			ASSERT_NE(nullptr, guild);
			EXPECT_EQ(guildId, guild->getId());
			EXPECT_EQ(name, guild->getName());
			EXPECT_EQ(1500U, guild->getBankBalance());
			EXPECT_FALSE(guild->getRanks().empty());
		})();
	}

	TEST(GuildRepositoryDBTest, LoadUnknownGuildReturnsNull) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			GuildRepositoryDB repository(db);
			EXPECT_EQ(nullptr, repository.loadGuild(0x7FFFFFF0));
		})();
	}

	TEST(GuildRepositoryDBTest, SaveGuildPersistsBalance) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto guildId = createGuild(db, fmt::format("Guild {}", uniqueSeed()), 10);
			GuildRepositoryDB repository(db);
			const auto guild = repository.loadGuild(guildId);
			ASSERT_NE(nullptr, guild);

			guild->setBankBalance(987654);
			repository.saveGuild(guild);

			EXPECT_EQ(987654U, repository.loadGuild(guildId)->getBankBalance());
		})();
	}

	TEST(GuildRepositoryDBTest, GetGuildIdByNameEscapesAndResolves) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto name = fmt::format("O'Guild {}", uniqueSeed());
			const auto guildId = createGuild(db, name, 0);
			GuildRepositoryDB repository(db);
			EXPECT_EQ(guildId, repository.getGuildIdByName(name));
			EXPECT_EQ(0U, repository.getGuildIdByName("no such guild"));
		})();
	}

	TEST(GuildRepositoryDBTest, GetWarListReturnsActiveOpponentsOnly) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto guildA = createGuild(db, fmt::format("Guild A {}", uniqueSeed()), 0);
			const auto guildB = createGuild(db, fmt::format("Guild B {}", uniqueSeed()), 0);
			const auto guildC = createGuild(db, fmt::format("Guild C {}", uniqueSeed()), 0);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `guild_wars` (`guild1`, `guild2`, `name1`, `name2`, `status`) VALUES ({}, {}, 'a', 'b', 1)", guildA, guildB)));
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `guild_wars` (`guild1`, `guild2`, `name1`, `name2`, `status`) VALUES ({}, {}, 'c', 'a', 0)", guildC, guildA)));

			GuildRepositoryDB repository(db);
			GuildWarVector wars;
			repository.getWarList(guildA, wars);
			EXPECT_EQ(GuildWarVector { guildB }, wars);
		})();
	}

} // namespace it_guild_repo_db
