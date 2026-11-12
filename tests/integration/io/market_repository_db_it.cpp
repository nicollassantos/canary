/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/in_memory_config_manager.hpp"
#include "io/market_repository_db.hpp"
#include "test_env.hpp"

namespace it_market_repo_db {

	constexpr int32_t OfferDuration = 3600;
	constexpr int32_t MaxTier = 10;

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 30000000;
		return base + counter.fetch_add(1);
	}

	uint16_t uniqueItemId() {
		return static_cast<uint16_t>(40000 + (uniqueSeed() % 20000));
	}

	struct Owner {
		uint32_t id;
		std::string name;
	};

	Owner createOwner(Database &db) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'market_acc_{}', 'market_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		const auto name = fmt::format("market_owner_{}", seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('{}', {}, '')", name, seed)));
		return { static_cast<uint32_t>(db.getLastInsertId()), name };
	}

	std::vector<uint64_t> prices(const MarketOfferList &offers) {
		std::vector<uint64_t> result;
		for (const auto &offer : offers) {
			result.push_back(offer.price);
		}
		return result;
	}

	InMemoryConfigManager &marketConfig() {
		static InMemoryConfigManager config;
		config.setNumber(MARKET_OFFER_DURATION, OfferDuration);
		config.setNumber(FORGE_MAX_ITEM_TIER, MaxTier);
		return config;
	}

	TEST(MarketRepositoryDBTest, CreatedOfferIsListedForOwnerAndCounted) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			const auto itemId = uniqueItemId();
			MarketRepositoryDB repository(db, marketConfig());

			repository.createOffer(owner.id, MARKETACTION_SELL, itemId, 5, 100, 2, false);

			const auto offers = repository.getOwnOffers(MARKETACTION_SELL, owner.id);
			ASSERT_EQ(1U, offers.size());
			EXPECT_EQ(itemId, offers.front().itemId);
			EXPECT_EQ(5, offers.front().amount);
			EXPECT_EQ(100U, offers.front().price);
			EXPECT_EQ(2, offers.front().tier);
			EXPECT_GE(offers.front().timestamp, static_cast<uint32_t>(getTimeNow() + OfferDuration - 5));
			EXPECT_TRUE(repository.getOwnOffers(MARKETACTION_BUY, owner.id).empty());
			EXPECT_EQ(1U, repository.getPlayerOfferCount(owner.id));
		})();
	}

	TEST(MarketRepositoryDBTest, ActiveOffersAreSortedByBestPriceFirst) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			const auto itemId = uniqueItemId();
			MarketRepositoryDB repository(db, marketConfig());
			for (const uint64_t price : { 10, 30, 20 }) {
				repository.createOffer(owner.id, MARKETACTION_BUY, itemId, 1, price, 0, false);
				repository.createOffer(owner.id, MARKETACTION_SELL, itemId, 1, price, 0, false);
			}

			EXPECT_EQ((std::vector<uint64_t> { 30, 20, 10 }), prices(repository.getActiveOffers(MARKETACTION_BUY, itemId, 0)));
			EXPECT_EQ((std::vector<uint64_t> { 10, 20, 30 }), prices(repository.getActiveOffers(MARKETACTION_SELL, itemId, 0)));

			EXPECT_TRUE(repository.getActiveOffers(MARKETACTION_BUY, itemId, 1).empty());
		})();
	}

	TEST(MarketRepositoryDBTest, AnonymousOfferHidesOwnerName) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			const auto itemId = uniqueItemId();
			MarketRepositoryDB repository(db, marketConfig());
			repository.createOffer(owner.id, MARKETACTION_SELL, itemId, 1, 10, 0, true);
			repository.createOffer(owner.id, MARKETACTION_SELL, itemId, 1, 20, 0, false);

			const auto offers = repository.getActiveOffers(MARKETACTION_SELL, itemId, 0);
			ASSERT_EQ(2U, offers.size());
			EXPECT_EQ("Anonymous", offers.front().playerName);
			EXPECT_EQ(owner.name, offers.back().playerName);
		})();
	}

	TEST(MarketRepositoryDBTest, OfferIsFoundByClientTimestampAndCounter) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			const auto itemId = uniqueItemId();
			MarketRepositoryDB repository(db, marketConfig());
			repository.createOffer(owner.id, MARKETACTION_SELL, itemId, 4, 70, 1, false);
			const auto listed = repository.getOwnOffers(MARKETACTION_SELL, owner.id).front();

			const auto offer = repository.getOfferByCounter(listed.timestamp, listed.counter);
			EXPECT_NE(0U, offer.id);
			EXPECT_EQ(owner.id, offer.playerId);
			EXPECT_EQ(MARKETACTION_SELL, offer.type);
			EXPECT_EQ(itemId, offer.itemId);
			EXPECT_EQ(4, offer.amount);
			EXPECT_EQ(1, offer.tier);
			EXPECT_EQ(owner.name, offer.playerName);

			EXPECT_EQ(0U, repository.getOfferByCounter(listed.timestamp + 1, listed.counter).id);
		})();
	}

	TEST(MarketRepositoryDBTest, AcceptReducesAmountAndDeleteRemovesOffer) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			MarketRepositoryDB repository(db, marketConfig());
			repository.createOffer(owner.id, MARKETACTION_BUY, uniqueItemId(), 5, 10, 0, false);
			const auto listed = repository.getOwnOffers(MARKETACTION_BUY, owner.id).front();
			const auto offerId = repository.getOfferByCounter(listed.timestamp, listed.counter).id;

			repository.acceptOffer(offerId, 2);
			EXPECT_EQ(3, repository.getOwnOffers(MARKETACTION_BUY, owner.id).front().amount);

			repository.deleteOffer(offerId);
			EXPECT_EQ(0U, repository.getPlayerOfferCount(owner.id));
		})();
	}

	// The success path also queues an async history insert, so only the synchronous rejection is covered here.
	TEST(MarketRepositoryDBTest, MoveUnknownOfferToHistoryFails) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			MarketRepositoryDB repository(db, marketConfig());
			EXPECT_FALSE(repository.moveOfferToHistory(0x7FFFFFF0, OFFERSTATE_CANCELLED));
		})();
	}

	TEST(MarketRepositoryDBTest, TierAboveConfiguredMaximumReadsAsZero) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			MarketRepositoryDB repository(db, marketConfig());
			repository.createOffer(owner.id, MARKETACTION_SELL, uniqueItemId(), 1, 10, MaxTier + 1, false);

			EXPECT_EQ(0, repository.getOwnOffers(MARKETACTION_SELL, owner.id).front().tier);
		})();
	}

	TEST(MarketRepositoryDBTest, StatisticsAggregateAcceptedHistoryOnly) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto owner = createOwner(db);
			const auto itemId = uniqueItemId();
			const auto insertHistory = [&](MarketAction_t sale, uint64_t price, MarketOfferState_t state) {
				EXPECT_TRUE(db.executeQuery(fmt::format(
					"INSERT INTO `market_history` (`player_id`, `sale`, `itemtype`, `amount`, `price`, `expires_at`, `inserted`, `state`, `tier`) VALUES ({}, {}, {}, 1, {}, 0, 0, {}, 0)",
					owner.id, fmt::underlying(sale), itemId, price, fmt::underlying(state)
				)));
			};
			insertHistory(MARKETACTION_BUY, 100, OFFERSTATE_ACCEPTED);
			insertHistory(MARKETACTION_BUY, 300, OFFERSTATE_ACCEPTED);
			insertHistory(MARKETACTION_BUY, 999, OFFERSTATE_EXPIRED);
			insertHistory(MARKETACTION_SELL, 50, OFFERSTATE_ACCEPTED);

			MarketRepositoryDB repository(db, marketConfig());
			repository.updateStatistics();
			const auto snapshot = repository.getStatistics(itemId, 0);

			ASSERT_TRUE(snapshot.purchase.has_value());
			EXPECT_EQ(2U, snapshot.purchase->numTransactions);
			EXPECT_EQ(100U, snapshot.purchase->lowestPrice);
			EXPECT_EQ(300U, snapshot.purchase->highestPrice);
			EXPECT_EQ(400U, snapshot.purchase->totalPrice);
			ASSERT_TRUE(snapshot.sale.has_value());
			EXPECT_EQ(1U, snapshot.sale->numTransactions);
			EXPECT_TRUE(repository.getPurchaseStatistics().contains(itemId));
			EXPECT_FALSE(repository.getStatistics(itemId, 1).purchase.has_value());
		})();
	}

} // namespace it_market_repo_db
