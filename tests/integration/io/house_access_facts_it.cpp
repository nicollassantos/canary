/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player_repository_db.hpp"
#include "map/house/house.hpp"
#include "test_env.hpp"

// BedItem::canUse compares the sleeper's house access level with the user's. The sleeper is
// offline, so the level is computed from HouseAccessFacts (one query) instead of a full load.
namespace it_house_access_facts {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 80000000;
		return base + counter.fetch_add(1);
	}

	struct Stored {
		uint32_t guid;
		uint32_t accountId;
		std::string name;
	};

	Stored createPlayer(Database &db, const std::string &prefix) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'house_acc_{}', 'house_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		// House lists keep only letters, spaces and a few symbols, so the unique suffix uses letters.
		std::string suffix;
		for (const char digit : std::to_string(seed)) {
			suffix += static_cast<char>('a' + (digit - '0'));
		}
		const auto name = fmt::format("{} {}", prefix, suffix);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('{}', {}, '')", name, seed)));
		return { static_cast<uint32_t>(db.getLastInsertId()), seed, name };
	}

	TEST(HouseAccessFactsTest, FactsComeFromOneQuery) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto member = createPlayer(db, "Guild Member");
			const auto loner = createPlayer(db, "Loner");
			EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `guilds` (`name`, `ownerid`, `creationdata`) VALUES ('House Guild {}', {}, 0)", member.guid, member.guid)));
			const auto guildId = static_cast<uint32_t>(db.getLastInsertId());
			const auto rank = db.storeQuery(fmt::format("SELECT `id` FROM `guild_ranks` WHERE `guild_id` = {} ORDER BY `level` DESC LIMIT 1", guildId));
			ASSERT_NE(nullptr, rank);
			const auto rankId = rank->getNumber<uint32_t>("id");
			EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `guild_membership` (`player_id`, `guild_id`, `rank_id`, `nick`) VALUES ({}, {}, {}, '')", member.guid, guildId, rankId)));
			PlayerRepositoryDB repository(db);

			const auto memberFacts = repository.loadHouseAccessFacts(member.guid);
			ASSERT_TRUE(memberFacts.has_value());
			EXPECT_EQ(member.guid, memberFacts->guid);
			EXPECT_EQ(member.accountId, memberFacts->accountId);
			EXPECT_EQ(rankId, memberFacts->guildRankId);

			const auto lonerFacts = repository.loadHouseAccessFacts(loner.guid);
			ASSERT_TRUE(lonerFacts.has_value());
			EXPECT_EQ(0U, lonerFacts->guildRankId);

			EXPECT_FALSE(repository.loadHouseAccessFacts(0x7FFFFFF0).has_value());
		})();
	}

	TEST(HouseAccessFactsTest, LevelsMatchTheHouseLists) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createPlayer(db, "House Owner");
			const auto subOwner = createPlayer(db, "House Sub");
			const auto guest = createPlayer(db, "House Guest");
			const auto stranger = createPlayer(db, "House Stranger");
			PlayerRepositoryDB repository(db);

			House house(uniqueSeed());
			house.setOwner(owner.guid, false);
			house.setAccessList(SUBOWNER_LIST, subOwner.name);
			house.setAccessList(GUEST_LIST, guest.name);

			const auto level = [&](const Stored &player) {
				return house.getHouseAccessLevel(*repository.loadHouseAccessFacts(player.guid));
			};
			EXPECT_EQ(HOUSE_OWNER, level(owner));
			EXPECT_EQ(HOUSE_SUBOWNER, level(subOwner));
			EXPECT_EQ(HOUSE_GUEST, level(guest));
			EXPECT_EQ(HOUSE_NOT_INVITED, level(stranger));

			house.setAccessList(GUEST_LIST, "*");
			EXPECT_EQ(HOUSE_GUEST, level(stranger));
		})();
	}

} // namespace it_house_access_facts
