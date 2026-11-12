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
#include "game/game.hpp"
#include "game/market/market_expiry.hpp"
#include "io/in_memory_market_repository.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	constexpr uint32_t OwnerGuid = 7;

	class MarketExpiryTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
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
