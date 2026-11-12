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
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/market/market_expiry.hpp"
#include "io/in_memory_market_repository.hpp"
#include "items/containers/inbox/inbox.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

namespace {

	constexpr uint32_t OwnerGuid = 7;

	class MarketExpiryTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			// A miss in getPlayerByGUID(guid, true) loads through the port; keep it off MySQL.
			InMemoryPlayerRepository::install(injector);
			DI::setTestContainer(&injector);
			TestItems::init();
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		void TearDown() override {
			for (const auto &saved : savedItemTypes) {
				auto &type = Item::items.getItems()[saved.id];
				type.id = saved.typeId;
				type.pickupable = saved.pickupable;
				type.movable = saved.movable;
				type.stackable = saved.stackable;
				type.charges = saved.charges;
				type.group = saved.group;
				type.type = saved.type;
				type.transformDeEquipTo = saved.transformDeEquipTo;
				type.wrapable = saved.wrapable;
			}
		}

		// Item::items is process-global: every mutation is restored in TearDown.
		void defineItem(uint16_t id, bool stackable) {
			auto &type = Item::items.getItems()[id];
			savedItemTypes.push_back({ id, type.id, type.pickupable, type.movable, type.stackable, type.charges, type.group, type.type, type.transformDeEquipTo, type.wrapable });
			type.id = id;
			type.pickupable = true;
			type.movable = true;
			type.stackable = stackable;
			type.charges = 0;
			// Plain item: no fluid/splash/container/door behaviour, so CreateItem keeps the count.
			type.group = ITEM_GROUP_NONE;
			type.type = ITEM_TYPE_OTHER;
			type.transformDeEquipTo = 0;
			type.wrapable = false;
		}

		std::shared_ptr<Player> registerOnlineOwner() {
			// Same constructor the server uses for real and offline-loaded players: it owns an inbox.
			auto owner = std::make_shared<Player>(nullptr);
			owner->setGroup(std::make_shared<Group>());
			owner->setGUID(OwnerGuid);
			owner->setName("Market Owner");
			owner->setID();
			game.addPlayer(owner);
			return owner;
		}

		static std::vector<uint16_t> inboxCounts(const std::shared_ptr<Player> &owner, uint16_t itemId) {
			std::vector<uint16_t> counts;
			for (const auto &item : owner->getInbox()->getItemList()) {
				if (item->getID() == itemId) {
					counts.push_back(item->getItemCount());
				}
			}
			// Containers insert at the front; order does not matter for the refund.
			std::ranges::sort(counts);
			return counts;
		}

		struct SavedItemType {
			uint16_t id;
			uint16_t typeId;
			bool pickupable;
			bool movable;
			bool stackable;
			uint32_t charges;
			ItemGroup_t group;
			ItemTypes_t type;
			uint16_t transformDeEquipTo;
			bool wrapable;
		};
		std::vector<SavedItemType> savedItemTypes;

		uint32_t createOffer(MarketAction_t action, uint16_t itemId, uint16_t amount, uint64_t price) {
			market.createOffer(OwnerGuid, action, itemId, amount, price, 0, false);
			return market.getLastCreatedOfferId();
		}

		void expire(const std::vector<ExpiredMarketOffer> &offers) {
			market_expiry::processExpiredOffers(game, market, players, offers);
		}

		Game game;
		InMemoryMarketRepository market;
		InMemoryPlayerRepository players;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(MarketExpiryTest, OfferThatCannotMoveToHistoryIsNotRefunded) {
	expire({ ExpiredMarketOffer { 42, OwnerGuid, MARKETACTION_BUY, 100, 3, 50, 0 } });

	EXPECT_EQ(0U, players.getBankBalance(OwnerGuid));
	EXPECT_EQ(0U, market.historySize());
}

TEST_F(MarketExpiryTest, ExpiredBuyOfferRefundsGoldToOfflineOwner) {
	const auto offerId = createOffer(MARKETACTION_BUY, 100, 3, 50);

	expire({ ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_BUY, 100, 3, 50, 0 } });

	EXPECT_FALSE(market.hasOffer(offerId));
	ASSERT_EQ(1U, market.historySize());
	EXPECT_EQ(OFFERSTATE_EXPIRED, market.lastHistoryState());
	EXPECT_EQ(150U, players.getBankBalance(OwnerGuid));
}

TEST_F(MarketExpiryTest, ExpiredSellOfferOfUnknownItemMovesToHistoryWithoutRefund) {
	const auto offerId = createOffer(MARKETACTION_SELL, 0, 2, 50);

	expire({ ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_SELL, 0, 2, 50, 0 } });

	EXPECT_FALSE(market.hasOffer(offerId));
	EXPECT_EQ(1U, market.historySize());
	EXPECT_EQ(0U, players.getBankBalance(OwnerGuid));
}

TEST_F(MarketExpiryTest, FailedRowDoesNotStopLaterRefunds) {
	const auto offerId = createOffer(MARKETACTION_BUY, 100, 2, 10);

	expire({
		ExpiredMarketOffer { 999, OwnerGuid, MARKETACTION_BUY, 100, 5, 1000, 0 },
		ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_BUY, 100, 2, 10, 0 },
	});

	EXPECT_EQ(20U, players.getBankBalance(OwnerGuid));
	EXPECT_EQ(1U, market.historySize());
}

TEST_F(MarketExpiryTest, ExpiredStackableSellOfferReturnsItemsInFullStacks) {
	constexpr uint16_t StackableId = 101;
	defineItem(StackableId, true);
	const auto owner = registerOnlineOwner();
	const auto offerId = createOffer(MARKETACTION_SELL, StackableId, 150, 10);

	expire({ ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_SELL, StackableId, 150, 10, 0 } });

	EXPECT_FALSE(market.hasOffer(offerId));
	EXPECT_EQ((std::vector<uint16_t> { 50, 100 }), inboxCounts(owner, StackableId));
	EXPECT_EQ(0U, players.getBankBalance(OwnerGuid));
}

TEST_F(MarketExpiryTest, ExpiredNonStackableSellOfferReturnsOneItemPerUnit) {
	constexpr uint16_t SingleId = 102;
	defineItem(SingleId, false);
	const auto owner = registerOnlineOwner();
	const auto offerId = createOffer(MARKETACTION_SELL, SingleId, 3, 10);

	expire({ ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_SELL, SingleId, 3, 10, 0 } });

	EXPECT_EQ((std::vector<uint16_t> { 1, 1, 1 }), inboxCounts(owner, SingleId));
}

TEST_F(MarketExpiryTest, ExpiredSellOfferOfUnknownOwnerIsMovedWithoutRefund) {
	constexpr uint16_t SingleId = 102;
	defineItem(SingleId, false);
	const auto offerId = createOffer(MARKETACTION_SELL, SingleId, 1, 10);

	// The owner is neither online nor in the (in-memory) repository.
	expire({ ExpiredMarketOffer { offerId, OwnerGuid, MARKETACTION_SELL, SingleId, 1, 10, 0 } });

	EXPECT_FALSE(market.hasOffer(offerId));
	EXPECT_EQ(1U, market.historySize());
}
