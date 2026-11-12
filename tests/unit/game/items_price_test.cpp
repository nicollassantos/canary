/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/game.hpp"
#include "io/in_memory_market_repository.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	MarketStatistics accepted(uint32_t transactions, uint64_t total) {
		MarketStatistics statistics;
		statistics.numTransactions = transactions;
		statistics.totalPrice = total;
		return statistics;
	}

	MarketOffer buyOffer(uint16_t itemId, uint64_t price, uint8_t tier = 0) {
		MarketOffer offer;
		offer.itemId = itemId;
		offer.price = price;
		offer.tier = tier;
		return offer;
	}

	// Game::loadItemsPrice: average accepted purchase per item/tier, raised to the best buy offer.
	class ItemsPriceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryMarketRepository::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		Game game;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(ItemsPriceTest, AveragePurchaseRaisedToTheBestBuyOffer) {
	IMarketRepository::StatisticsMap purchase;
	purchase[100][0] = accepted(2, 300);
	purchase[102][1] = accepted(3, 90);

	game.rebuildItemsPrice(purchase, { buyOffer(100, 200), buyOffer(100, 120), buyOffer(101, 50) });

	const auto &prices = game.getItemsPrice();
	EXPECT_EQ(200U, prices.at(100).at(0));
	EXPECT_EQ(50U, prices.at(101).at(0));
	EXPECT_EQ(30U, prices.at(102).at(1));
}

TEST_F(ItemsPriceTest, LowerBuyOfferKeepsTheAverage) {
	IMarketRepository::StatisticsMap purchase;
	purchase[100][0] = accepted(2, 300);

	game.rebuildItemsPrice(purchase, { buyOffer(100, 90) });

	EXPECT_EQ(150U, game.getItemsPrice().at(100).at(0));
}

// The refresh reads both inputs through the repository port (asynchronously in production).
TEST_F(ItemsPriceTest, RefreshUsesTheRepositoryStatisticsAndOffers) {
	auto &market = dynamic_cast<InMemoryMarketRepository &>(IMarketRepository::getInstance());
	market.reset();
	market.statisticsLoad = true;
	market.nextPurchaseStatistics.clear();
	market.nextPurchaseStatistics[61001][0] = accepted(4, 400);
	market.createOffer(1, MARKETACTION_BUY, 61002, 1, 75, 0, false);

	g_game().loadItemsPrice();

	const auto &prices = g_game().getItemsPrice();
	EXPECT_EQ(100U, prices.at(61001).at(0));
	EXPECT_EQ(75U, prices.at(61002).at(0));
	EXPECT_TRUE(market.getPurchaseStatistics().contains(61001));
}

// A failed or empty statistics load must not wipe the cached statistics.
TEST_F(ItemsPriceTest, RefreshWithoutRowsKeepsTheCachedStatistics) {
	auto &market = dynamic_cast<InMemoryMarketRepository &>(IMarketRepository::getInstance());
	market.reset();
	market.purchaseStatistics.clear();
	market.purchaseStatistics[61003][0] = accepted(1, 10);
	market.statisticsLoad = false;

	g_game().loadItemsPrice();

	EXPECT_TRUE(market.getPurchaseStatistics().contains(61003));
	market.statisticsLoad = true;
}
