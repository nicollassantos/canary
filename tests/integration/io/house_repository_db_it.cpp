/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "map/house/house_repository_db.hpp"
#include "map/map_definitions.hpp"
#include "test_env.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
	#include <atomic>
	#include <chrono>
#endif

namespace it_house_repository_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x00FFFFFF) + 70000000;
		return base + counter.fetch_add(1);
	}

	uint32_t createPlayer(Database &db, const std::string &name, uint32_t accountId) {
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'house_acc_{}', 'house_{}@test.com', '', 1, 0, 0, 0)", accountId, accountId, accountId)));
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ({}, {}, '')", db.escapeString(name), accountId)));
		return static_cast<uint32_t>(db.getLastInsertId());
	}

	const StoredHouse* find(const std::vector<StoredHouse> &houses, uint32_t id) {
		const auto it = std::ranges::find_if(houses, [id](const auto &house) { return house.id == id; });
		return it == houses.end() ? nullptr : &*it;
	}

	TEST(HouseRepositoryDBTest, StoredAuctionStateIsReadBack) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto id = uniqueSeed();
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `houses` (`id`, `owner`, `new_owner`, `name`, `bidder`, `bidder_name`, `highest_bid`, `internal_bid`, `bid_end_date`, `state`, `transfer_status`) VALUES ({}, 0, 12, 'It''s a house', 34, 'Bidder', 3000, 5000, 1800000000, 3, 1)", id)));
			HouseRepositoryDB repository(db);

			const auto houses = repository.loadHouses();
			const auto* house = find(houses, id);
			ASSERT_NE(nullptr, house);
			EXPECT_EQ(12, house->newOwner);
			EXPECT_EQ(34U, house->bidder);
			EXPECT_EQ("Bidder", house->bidderName);
			EXPECT_EQ(3000U, house->highestBid);
			EXPECT_EQ(5000U, house->internalBid);
			EXPECT_EQ(1'800'000'000U, house->bidEndDate);
			EXPECT_EQ(static_cast<CyclopediaHouseState>(3), house->state);
			EXPECT_TRUE(house->transferStatus);
		})();
	}

	TEST(HouseRepositoryDBTest, GivingAHouseClearsItsAuctionAndPendingTransfer) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto id = uniqueSeed();
			const auto owner = createPlayer(db, fmt::format("House Owner {}", id), id);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `houses` (`id`, `owner`, `new_owner`, `name`, `bidder`, `bidder_name`, `highest_bid`, `internal_bid`, `bid_end_date`, `state`) VALUES ({}, 0, 5, 'h', 34, 'Bidder', 3000, 5000, 1800000000, 0)", id)));
			HouseRepositoryDB repository(db);

			repository.storeOwner(id, owner);
			auto row = db.storeQuery(fmt::format("SELECT * FROM `houses` WHERE `id` = {}", id));
			ASSERT_NE(nullptr, row);
			EXPECT_EQ(owner, row->getNumber<uint32_t>("owner"));
			EXPECT_EQ(-1, row->getNumber<int32_t>("new_owner"));
			EXPECT_EQ(0U, row->getNumber<uint32_t>("bidder"));
			EXPECT_EQ("", row->getString("bidder_name"));
			EXPECT_EQ(0U, row->getNumber<uint32_t>("internal_bid"));
			EXPECT_EQ(2U, row->getNumber<uint32_t>("state"));

			repository.storeNewOwner(id, 0);
			row = db.storeQuery(fmt::format("SELECT `new_owner` FROM `houses` WHERE `id` = {}", id));
			ASSERT_NE(nullptr, row);
			EXPECT_EQ(0, row->getNumber<int32_t>("new_owner"));

			const auto identity = repository.findOwnerIdentity(owner);
			ASSERT_TRUE(identity.has_value());
			EXPECT_EQ(fmt::format("House Owner {}", id), identity->name);
			EXPECT_EQ(id, identity->accountId);
			EXPECT_FALSE(repository.findOwnerIdentity(0).has_value());
		})();
	}

	// saveHouses runs its own transaction, so this one cleans up after itself.
	TEST(HouseRepositoryDBTest, SavingWritesTheHousesAndReplacesTheirLists) {
		auto &db = g_database();
		TestDatabase::init();
		const auto id = uniqueSeed();
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `houses` (`id`, `owner`, `name`) VALUES ({}, 0, 'before')", id)));
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `house_lists` (`house_id`, `listid`, `list`, `version`) VALUES ({}, {}, 'old subowner', 1)", id, static_cast<uint32_t>(SUBOWNER_LIST))));
		HouseRepositoryDB repository(db);

		HouseRow house { .id = id, .owner = 0, .paidUntil = 1'800'000'000, .rentWarnings = 2, .name = "Isn't renamed", .townId = 3, .rent = 1000, .size = 40, .beds = 2, .bidder = 0, .bidderName = "", .state = CyclopediaHouseState::Available };
		const std::vector<HouseAccessListRow> lists { { id, static_cast<uint32_t>(GUEST_LIST), "Guest One" }, { id, 0x42, "" } };
		EXPECT_TRUE(repository.saveHouses({ house }, lists));

		const auto row = db.storeQuery(fmt::format("SELECT * FROM `houses` WHERE `id` = {}", id));
		ASSERT_NE(nullptr, row);
		EXPECT_EQ("Isn't renamed", row->getString("name"));
		EXPECT_EQ(1'800'000'000U, row->getNumber<uint32_t>("paid"));
		EXPECT_EQ(40U, row->getNumber<uint32_t>("size"));
		EXPECT_EQ(2U, row->getNumber<uint32_t>("beds"));
		const auto stored = repository.loadAccessLists();
		const auto mine = std::ranges::count_if(stored, [id](const auto &list) { return list.houseId == id; });
		EXPECT_EQ(1, mine); // the guest list; the old subowner list and the empty one are gone
		EXPECT_TRUE(std::ranges::any_of(stored, [id](const auto &list) { return list.houseId == id && list.list == "Guest One"; }));

		EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `houses` WHERE `id` = {}", id)));
	}

	// The items in houses are binary blobs (IOMapSerialize's format); a save replaces all of them.
	TEST(HouseRepositoryDBTest, TileItemBlobsComeBackByteForByteAndASaveReplacesThemAll) {
		auto &db = g_database();
		TestDatabase::init();
		const auto id = uniqueSeed();
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `houses` (`id`, `owner`, `name`) VALUES ({}, 0, 'blobs')", id)));
		HouseRepositoryDB repository(db);

		const std::string first("\x01\x00\x02\x00\x07'\\\"\0end", 13);
		const std::string second(3000, '\xff');
		ASSERT_TRUE(repository.saveTileItems({ { id, first }, { id, second } }));
		auto stored = repository.loadTileItems();
		std::vector<std::string> mine;
		for (const auto &tile : stored) {
			if (tile.houseId == id) {
				mine.push_back(tile.data);
			}
		}
		ASSERT_EQ(2U, mine.size());
		EXPECT_TRUE(std::ranges::find(mine, first) != mine.end());
		EXPECT_TRUE(std::ranges::find(mine, second) != mine.end());

		ASSERT_TRUE(repository.saveTileItems({ { id, second } }));
		stored = repository.loadTileItems();
		EXPECT_EQ(1, std::ranges::count_if(stored, [id](const auto &tile) { return tile.houseId == id; }));

		EXPECT_TRUE(db.executeQuery(fmt::format("DELETE FROM `houses` WHERE `id` = {}", id)));
	}

} // namespace it_house_repository_db
