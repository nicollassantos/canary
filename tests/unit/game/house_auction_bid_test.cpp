/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "account/in_memory_account_repository.hpp"
#include "config/config_enums.hpp"
#include "config/in_memory_config_manager.hpp"
#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
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

namespace {

	constexpr uint32_t RulesHouseClientId = 64203;
	constexpr uint32_t OtherHouseClientId = 64204;
	constexpr uint16_t Sorcerer = 1;

	// Bidders that pass Player::canBidHouse (vocation, premium, account), so the auction rules
	// themselves are exercised. canBidHouse looks the house up through g_game(); that lookup keeps
	// the first registration, which only feeds its balance check (the bank here covers it).
	class HouseAuctionRulesTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryPlayerRepository::install(injector);
			tests::InMemoryAccountRepository::install(injector);
			DI::setTestContainer(&injector);
			ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));

			auto &accounts = dynamic_cast<tests::InMemoryAccountRepository &>(DI::get<AccountRepository>());
			AccountInfo bidderAccount;
			bidderAccount.id = 7001;
			bidderAccount.players = { { "Bidder", 0 }, { "Bidder Alt", 0 } };
			accounts.addAccount("bidder", bidderAccount);
			AccountInfo competitorAccount;
			competitorAccount.id = 7002;
			competitorAccount.players = { { "Competitor", 0 } };
			accounts.addAccount("competitor", competitorAccount);

			config.setBoolean(CYCLOPEDIA_HOUSE_AUCTION, true);
			config.setNumber(DAYS_TO_CLOSE_BID, 7);

			house = resetHouse(RulesHouseClientId);
			bidder = addBidder(11, "Bidder", 7001);
			competitor = addBidder(12, "Competitor", 7002);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		std::shared_ptr<House> resetHouse(uint32_t clientId) {
			auto local = game.map.houses.addHouse(clientId);
			game.map.houses.addHouseClientId(clientId, local);
			g_game().map.houses.addHouseClientId(clientId, local);
			return local;
		}

		std::shared_ptr<Player> addBidder(uint32_t guid, const std::string &name, uint32_t accountId) {
			auto player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(guid);
			player->setName(name);
			player->setID();
			EXPECT_TRUE(player->setVocation(Sorcerer));
			player->setFlag(PlayerFlags_t::IsAlwaysPremium);
			EXPECT_TRUE(player->setAccount(accountId));
			player->setBankBalance(StartingBank);
			game.addPlayer(player);
			return player;
		}

		void bid(const std::shared_ptr<Player> &player, uint64_t value, uint32_t clientId = RulesHouseClientId) {
			service.playerCyclopediaHouseBid(player->getID(), clientId, value);
		}

		Game game;
		InMemoryConfigManager config;
		CyclopediaService service { game, config };
		std::shared_ptr<House> house;
		std::shared_ptr<Player> bidder;
		std::shared_ptr<Player> competitor;

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(HouseAuctionRulesTest, FirstBidIsPlacedAndCharged) {
	bid(bidder, 10'000);

	EXPECT_EQ("Bidder", house->getBidderName());
	EXPECT_EQ(10'000U, house->getInternalBid());
	EXPECT_EQ(StartingBank - 10'000, bidder->getBankBalance());
}

TEST_F(HouseAuctionRulesTest, LeaderCanRaiseTheirBid) {
	bid(bidder, 10'000);
	bid(bidder, 15'000);

	EXPECT_EQ(15'000U, house->getInternalBid());
	EXPECT_EQ(StartingBank - 15'000, bidder->getBankBalance());
}

// Regression: lowering the leading bid below the competing one sold the house under that offer.
TEST_F(HouseAuctionRulesTest, LeaderCannotLowerTheirBid) {
	bid(bidder, 10'000);
	bid(bidder, 2'000);

	EXPECT_EQ(10'000U, house->getInternalBid());
	EXPECT_EQ(StartingBank - 10'000, bidder->getBankBalance());
}

TEST_F(HouseAuctionRulesTest, HigherCompetingBidTakesTheLeadAndRefundsTheLeader) {
	bid(bidder, 10'000);
	bid(competitor, 20'000);

	EXPECT_EQ("Competitor", house->getBidderName());
	EXPECT_EQ(10'001U, house->getHighestBid());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
	EXPECT_EQ(StartingBank - 20'000, competitor->getBankBalance());
}

// Regression: a lower competing bid replaced the price, so the leader's alt bidding 1 gold made
// the leader win for almost nothing.
TEST_F(HouseAuctionRulesTest, LowerCompetingBidCannotDropThePrice) {
	bid(bidder, 10'000);
	bid(competitor, 6'000);
	ASSERT_EQ(6'000U, house->getHighestBid());

	bid(competitor, 100);

	EXPECT_EQ(6'000U, house->getHighestBid());
	EXPECT_EQ("Bidder", house->getBidderName());
}

// Regression: accounts.house_bid_id is never written, so "one bid per account" never applied.
TEST_F(HouseAuctionRulesTest, AccountAlreadyBiddingElsewhereIsRefused) {
	const auto other = resetHouse(OtherHouseClientId);
	other->setBidder(99);
	other->setBidderName("Bidder Alt");

	EXPECT_TRUE(service.bidsOnAnotherHouse(bidder, house));
	EXPECT_FALSE(service.bidsOnAnotherHouse(competitor, house));

	bid(bidder, 10'000);

	EXPECT_TRUE(house->getBidderName().empty());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
}

TEST_F(HouseAuctionRulesTest, BidOutsideTheStoredRangeIsRefused) {
	bid(bidder, 0);
	bid(bidder, CyclopediaService::MaxHouseBid + 1);

	EXPECT_TRUE(house->getBidderName().empty());
	EXPECT_EQ(StartingBank, bidder->getBankBalance());
}
