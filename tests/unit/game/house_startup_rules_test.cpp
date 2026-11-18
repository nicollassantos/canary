/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "map/house/house_auction_rules.hpp"

// What the server does with each stored house when it starts (it moves houses and money: before,
// this sat inside IOMapSerialize::loadHouseInfo between SQL reads and had no test).
namespace {
	constexpr int64_t Now = 1'800'000'000;
	constexpr uint32_t Owner = 10;
	constexpr uint32_t Bidder = 20;

	StoredHouse stored(CyclopediaHouseState state, uint32_t bidEndDate = Now - 1) {
		StoredHouse house;
		house.id = 7;
		house.owner = Owner;
		house.state = state;
		house.bidEndDate = bidEndDate;
		return house;
	}

	using Credits = std::vector<std::pair<uint32_t, uint64_t>>;
} // namespace

TEST(HouseStartupRulesTest, AHouseWithNothingDueKeepsItsOwnerAndAuction) {
	auto house = stored(CyclopediaHouseState::Available, Now + 60);
	house.bidder = Bidder;
	house.bidderName = "Bidder";
	house.highestBid = 5'000;
	const auto resolution = house_auction_rules::resolveAtStartup(house, Now, false);
	EXPECT_FALSE(resolution.newOwner.has_value());
	EXPECT_FALSE(resolution.moveItemsOf.has_value());
	EXPECT_TRUE(resolution.credits.empty());
	EXPECT_EQ(Bidder, resolution.bidder);
	EXPECT_EQ(5'000U, resolution.highestBid);
	EXPECT_EQ(static_cast<uint32_t>(Now + 60), resolution.bidEndDate);
}

TEST(HouseStartupRulesTest, AWonAuctionGivesTheHouseAndReturnsWhatWasBidAboveThePrice) {
	auto house = stored(CyclopediaHouseState::Available);
	house.owner = 0;
	house.bidder = Bidder;
	house.bidderName = "Bidder";
	house.highestBid = 30'000; // the price: the second highest bid
	house.internalBid = 50'000; // what the winner offered
	const auto resolution = house_auction_rules::resolveAtStartup(house, Now, false);
	EXPECT_EQ(std::optional<uint32_t>(Bidder), resolution.newOwner);
	EXPECT_EQ((Credits { { Bidder, 20'000 } }), resolution.credits);
	EXPECT_EQ(0U, resolution.bidder);
	EXPECT_TRUE(resolution.bidderName.empty());
	EXPECT_EQ(0U, resolution.highestBid);
	EXPECT_EQ(0U, resolution.internalBid);
	EXPECT_EQ(0U, resolution.bidEndDate);

	house.internalBid = house.highestBid; // offered exactly the price: nothing comes back
	EXPECT_TRUE(house_auction_rules::resolveAtStartup(house, Now, false).credits.empty());
}

TEST(HouseStartupRulesTest, AnAuctionWithoutBiddersOrNotDueYetChangesNothing) {
	auto house = stored(CyclopediaHouseState::Available);
	house.owner = 0;
	EXPECT_FALSE(house_auction_rules::resolveAtStartup(house, Now, false).newOwner.has_value());

	house.bidder = Bidder;
	house.bidEndDate = static_cast<uint32_t>(Now);
	EXPECT_FALSE(house_auction_rules::resolveAtStartup(house, Now, false).newOwner.has_value());
}

TEST(HouseStartupRulesTest, AnAcceptedTransferMovesTheItemsGivesTheHouseAndPaysTheOldOwner) {
	auto house = stored(CyclopediaHouseState::Transfer);
	house.bidder = Bidder;
	house.bidderName = "New Owner";
	house.internalBid = 80'000;
	house.highestBid = 123;
	house.transferStatus = true;
	const auto resolution = house_auction_rules::resolveAtStartup(house, Now, false);
	EXPECT_EQ(std::optional<uint32_t>(Owner), resolution.moveItemsOf);
	EXPECT_EQ(std::optional<uint32_t>(Bidder), resolution.newOwner);
	EXPECT_EQ((Credits { { Owner, 80'000 } }), resolution.credits);
	EXPECT_EQ(0U, resolution.bidder);
	EXPECT_EQ(0U, resolution.internalBid);
	EXPECT_EQ(123U, resolution.highestBid); // untouched by a transfer
	EXPECT_FALSE(resolution.transferStatus);
}

TEST(HouseStartupRulesTest, ARefusedTransferLeavesTheHouseWithItsOwnerAndNoMoney) {
	auto house = stored(CyclopediaHouseState::Transfer);
	house.bidder = Bidder;
	house.internalBid = 80'000;
	house.transferStatus = false;
	const auto resolution = house_auction_rules::resolveAtStartup(house, Now, false);
	EXPECT_EQ(std::optional<uint32_t>(Owner), resolution.newOwner);
	EXPECT_FALSE(resolution.moveItemsOf.has_value());
	EXPECT_TRUE(resolution.credits.empty());
	EXPECT_EQ(0U, resolution.bidder);
	EXPECT_EQ(0U, resolution.internalBid);
}

TEST(HouseStartupRulesTest, ADueMoveOutEmptiesTheHouse) {
	const auto resolution = house_auction_rules::resolveAtStartup(stored(CyclopediaHouseState::MoveOut), Now, false);
	EXPECT_EQ(std::optional<uint32_t>(Owner), resolution.moveItemsOf);
	EXPECT_EQ(std::optional<uint32_t>(0), resolution.newOwner);
	EXPECT_EQ(0U, resolution.bidEndDate);
	EXPECT_FALSE(house_auction_rules::resolveAtStartup(stored(CyclopediaHouseState::MoveOut, Now + 1), Now, false).newOwner.has_value());
}

TEST(HouseStartupRulesTest, ATransferOnRestartAppliesOnlyWhenEnabled) {
	auto house = stored(CyclopediaHouseState::Rented, 0);
	house.newOwner = static_cast<int32_t>(Bidder);
	EXPECT_FALSE(house_auction_rules::resolveAtStartup(house, Now, false).newOwner.has_value());

	auto resolution = house_auction_rules::resolveAtStartup(house, Now, true);
	EXPECT_EQ(std::optional<uint32_t>(Bidder), resolution.newOwner);
	EXPECT_EQ(std::optional<uint32_t>(Owner), resolution.moveItemsOf);

	house.newOwner = 0; // remove the owner
	EXPECT_EQ(std::optional<uint32_t>(0), house_auction_rules::resolveAtStartup(house, Now, true).newOwner);
	house.newOwner = -1; // nothing pending
	EXPECT_FALSE(house_auction_rules::resolveAtStartup(house, Now, true).newOwner.has_value());
}
