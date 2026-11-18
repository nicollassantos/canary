/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "account/account.hpp"
#include "account/in_memory_account_repository.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/grouping/guild.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "enums/player_cyclopedia.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "map/house/house.hpp"
#include "map/house/house_auction_rules.hpp"

// The eligibility rules read the house and the characters they are given (no lookup through the
// global Game), so each refusal is reached directly.
namespace {

	constexpr uint16_t Sorcerer = 1;
	constexpr uint64_t Rich = 1'000'000;

	class HouseAuctionEligibilityTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			tests::InMemoryAccountRepository::install(injector);
			DI::setTestContainer(&injector);
			ASSERT_TRUE(g_vocations().loadFromXml(std::string(TESTS_SOURCE_DIR) + "/data/XML/vocations.xml"));

			accounts = &dynamic_cast<tests::InMemoryAccountRepository &>(DI::get<AccountRepository>());
			// No rent: House::getRent() scales it by the global config, which these tests do not load.
			house = std::make_shared<House>(64301);
			bidder = makePlayer(31, "Eligible Bidder", 8001);
			other = makePlayer(32, "Eligible Other", 8002);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		std::shared_ptr<Player> makePlayer(uint32_t guid, const std::string &name, uint32_t accountId) {
			AccountInfo info;
			info.id = accountId;
			info.players = { { name, 0 } };
			accounts->addAccount(name, info);
			auto player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(guid);
			player->setName(name);
			EXPECT_TRUE(player->setVocation(Sorcerer));
			player->setFlag(PlayerFlags_t::IsAlwaysPremium);
			EXPECT_TRUE(player->setAccount(accountId));
			player->setBankBalance(Rich);
			return player;
		}

		tests::InMemoryAccountRepository* accounts = nullptr;
		std::shared_ptr<House> house;
		std::shared_ptr<Player> bidder;
		std::shared_ptr<Player> other;

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(HouseAuctionEligibilityTest, BidNeedsBankMoneyForTheHighestBidOfThisHouse) {
	house->setHighestBid(Rich);
	EXPECT_EQ(BidErrorMessage::NoError, house_auction_rules::canBid(*bidder, *house));

	house->setHighestBid(Rich + 1);
	EXPECT_EQ(BidErrorMessage::NotEnoughMoney, house_auction_rules::canBid(*bidder, *house));
}

TEST_F(HouseAuctionEligibilityTest, BidIsRefusedToAnAccountAlreadyBidding) {
	bidder->getAccount()->setHouseBidId(99);
	EXPECT_EQ(BidErrorMessage::OnlyOneBid, house_auction_rules::canBid(*bidder, *house));
}

TEST_F(HouseAuctionEligibilityTest, AGuildhallBidNeedsTheLeaderAndTheGuildMoney) {
	house->setGuildhall(true);
	house->setHighestBid(1);
	const auto guild = std::make_shared<Guild>(5, "Eligibility Guild");
	guild->addRank(1, "Member", 1);
	guild->addRank(3, "Leader", 3);
	bidder->setGuild(guild);
	EXPECT_EQ(BidErrorMessage::Guildhall, house_auction_rules::canBid(*bidder, *house));

	bidder->setGuildRank(guild->getRankByLevel(3));
	EXPECT_EQ(BidErrorMessage::NotEnoughGuildMoney, house_auction_rules::canBid(*bidder, *house));

	guild->setBankBalance(Rich);
	EXPECT_EQ(BidErrorMessage::NoError, house_auction_rules::canBid(*bidder, *house));
}

// The rest of the transfer rules need an owned house, and House::setOwner sets the owner only after
// reading the owner's name from the database (tasks/solid_audit.md, IHouseRepository).
TEST_F(HouseAuctionEligibilityTest, OnlyTheOwnerTransfers) {
	EXPECT_EQ(TransferErrorMessage::NotHouseOwner, house_auction_rules::canTransfer(*bidder, *house, *other));
}

TEST_F(HouseAuctionEligibilityTest, OnlyTheNamedNewOwnerAcceptsOnceWithMoneyForThePrice) {
	EXPECT_EQ(AcceptTransferErrorMessage::NotNewOwner, house_auction_rules::canAcceptTransfer(*other, *house));

	house->setBidder(static_cast<int32_t>(other->getGUID()));
	EXPECT_EQ(AcceptTransferErrorMessage::Success, house_auction_rules::canAcceptTransfer(*other, *house));

	house->setInternalBid(Rich + 1);
	EXPECT_EQ(AcceptTransferErrorMessage::Frozen, house_auction_rules::canAcceptTransfer(*other, *house));

	house->setInternalBid(0);
	house->setTransferStatus(true);
	EXPECT_EQ(AcceptTransferErrorMessage::AlreadyAccepted, house_auction_rules::canAcceptTransfer(*other, *house));
}
