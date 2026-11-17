/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/market/items_price_broadcast.hpp"
#include "io/in_memory_market_repository.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "server/network/message/networkmessage.hpp"

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

// Online players get the list again only when a price changed (issue #2083).
TEST_F(ItemsPriceTest, RebuildReportsWhetherAPriceChanged) {
	IMarketRepository::StatisticsMap purchase;
	purchase[63001][0] = accepted(2, 300);

	EXPECT_TRUE(game.rebuildItemsPrice(purchase, {}));
	EXPECT_FALSE(game.rebuildItemsPrice(purchase, {}));

	purchase[63001][0] = accepted(3, 300);
	EXPECT_TRUE(game.rebuildItemsPrice(purchase, {}));
	EXPECT_TRUE(game.rebuildItemsPrice(purchase, { buyOffer(63002, 10) }));
	EXPECT_FALSE(game.rebuildItemsPrice(purchase, { buyOffer(63002, 10) }));
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

// Issue #2083 resend: batches in order, at most BatchSize players each.
TEST(ItemsPriceBroadcastTest, BatchesKeepTheOrderAndTheSize) {
	std::vector<uint32_t> ids(120);
	for (uint32_t i = 0; i < ids.size(); ++i) {
		ids[i] = 1000 + i;
	}

	const auto batches = items_price_broadcast::makeBatches(ids, 50);
	ASSERT_EQ(3U, batches.size());
	EXPECT_EQ(50U, batches[0].size());
	EXPECT_EQ(50U, batches[1].size());
	EXPECT_EQ(20U, batches[2].size());
	EXPECT_EQ(1000U, batches[0].front());
	EXPECT_EQ(1050U, batches[1].front());
	EXPECT_EQ(1119U, batches[2].back());
	EXPECT_TRUE(items_price_broadcast::makeBatches({}, 50).empty());
	EXPECT_EQ(2U, items_price_broadcast::makeBatches({ 1, 2 }, 0).size());
}

// A batch from an earlier refresh sends nothing; the current one reaches the players still online.
TEST_F(ItemsPriceTest, OnlyTheLatestRefreshSendsItsBatches) {
	auto &items = Item::items.getItems();
	const auto originalItemCount = items.size();
	if (items.empty()) {
		items.emplace_back(); // the player constructor needs a fallback item type
	}

	ASSERT_TRUE(g_game().getPlayers().empty());
	const auto earlier = items_price_broadcast::currentGeneration();
	items_price_broadcast::start(); // nobody online: only advances the generation
	const auto current = items_price_broadcast::currentGeneration();
	EXPECT_EQ(earlier + 1, current);

	auto player = std::make_shared<Player>();
	player->setName("ItemsPriceBroadcastTest");
	player->setGUID(64003);
	player->setID();
	g_game().addPlayer(player);
	NetworkMessage list;

	EXPECT_EQ(0U, items_price_broadcast::sendBatch(earlier, { player->getID() }, list));
	EXPECT_EQ(1U, items_price_broadcast::sendBatch(current, { player->getID(), 0x7FFFFFF0 }, list));

	g_game().removePlayer(player);
	items.resize(originalItemCount);
}
