/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/config_enums.hpp"
#include "config/in_memory_config_manager.hpp"
#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "enums/player_cyclopedia.hpp"
#include "game/cyclopedia/cyclopedia_service.hpp"
#include "game/game.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "map/house/house.hpp"

namespace {

	constexpr uint32_t HouseClientId = 64201;
	constexpr uint64_t StartingBank = 1'000'000;
	constexpr uint64_t Bid = 5'000;

	class HouseAuctionBidTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryPlayerRepository::install(injector);
			DI::setTestContainer(&injector);
			logger = &dynamic_cast<InMemoryLogger &>(DI::get<Logger>());
			logger->reset();

			config.setBoolean(CYCLOPEDIA_HOUSE_AUCTION, true);
			config.setNumber(DAYS_TO_CLOSE_BID, 7);

			house = game.map.houses.addHouse(HouseClientId);
			game.map.houses.addHouseClientId(HouseClientId, house);
			// Player::canBidHouse looks the house up through g_game().
			g_game().map.houses.addHouseClientId(HouseClientId, house);

			bidder = std::make_shared<Player>(nullptr);
			bidder->setGroup(std::make_shared<Group>());
			bidder->setGUID(7);
			bidder->setName("Bidder");
			bidder->setID();
			bidder->setBankBalance(StartingBank);
			game.addPlayer(bidder);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		bool loggedNotOnAuction() const {
			return std::ranges::any_of(logger->logs, [](const auto &entry) {
				return entry.level == "warning" && entry.message.find("not up for auction") != std::string::npos;
			});
		}

		Game game;
		InMemoryConfigManager config;
		CyclopediaService service { game, config };
		std::shared_ptr<House> house;
		std::shared_ptr<Player> bidder;
		InMemoryLogger* logger = nullptr;

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// Regression: the refusal from canBidHouse was sent to the client but the bid went on, so
// players the rules exclude (here a Rookgaard player, also free accounts) still bid.
TEST_F(HouseAuctionBidTest, BidRefusedByTheBiddingRulesIsNotPlaced) {
	ASSERT_EQ(CyclopediaHouseState::Available, house->getState());

	service.playerCyclopediaHouseBid(bidder->getID(), HouseClientId, Bid);

	EXPECT_TRUE(house->getBidderName().empty());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
	EXPECT_FALSE(loggedNotOnAuction());
}

// Regression: a pending transfer keeps the heir in the bidder fields. A bid on that house
// replaced the heir, and the restart handed the house to the bidder.
TEST_F(HouseAuctionBidTest, BidOnAHouseUnderTransferIsRefused) {
	house->setState(CyclopediaHouseState::Transfer);
	house->setBidder(2);
	house->setBidderName("Heir");
	house->setInternalBid(1'000);

	service.playerCyclopediaHouseBid(bidder->getID(), HouseClientId, Bid);

	EXPECT_TRUE(loggedNotOnAuction());
	EXPECT_EQ(2, house->getBidder());
	EXPECT_EQ("Heir", house->getBidderName());
	EXPECT_EQ(1'000U, house->getInternalBid());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
}

TEST_F(HouseAuctionBidTest, BidOnARentedHouseIsRefused) {
	house->setState(CyclopediaHouseState::Rented);

	service.playerCyclopediaHouseBid(bidder->getID(), HouseClientId, Bid);

	EXPECT_TRUE(loggedNotOnAuction());
	EXPECT_TRUE(house->getBidderName().empty());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
}
