/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "io/in_memory_market_repository.hpp"
#include "creatures/creatures_definitions.hpp"

class InMemoryMarketRepositoryTest : public ::testing::Test {
protected:
	InMemoryMarketRepository repo;
};

// --- getActiveOffers ---

TEST_F(InMemoryMarketRepositoryTest, GetActiveOffers_ReturnsEmpty_WhenNoOffersCreated) {
	auto offers = repo.getActiveOffers(MARKETACTION_BUY);
	EXPECT_TRUE(offers.empty());
}

TEST_F(InMemoryMarketRepositoryTest, CreateOffer_AppearsInGetActiveOffers) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);

	auto offers = repo.getActiveOffers(MARKETACTION_BUY);
	ASSERT_EQ(1u, offers.size());
	EXPECT_EQ(100u, offers.front().itemId);
	EXPECT_EQ(5u, offers.front().amount);
	EXPECT_EQ(1000u, offers.front().price);
}

TEST_F(InMemoryMarketRepositoryTest, GetActiveOffers_FiltersByAction) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);
	repo.createOffer(2, MARKETACTION_SELL, 100, 3, 900, 0, false);

	EXPECT_EQ(1u, repo.getActiveOffers(MARKETACTION_BUY).size());
	EXPECT_EQ(1u, repo.getActiveOffers(MARKETACTION_SELL).size());
}

TEST_F(InMemoryMarketRepositoryTest, GetActiveOffers_FiltersByItemIdAndTier) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);
	repo.createOffer(2, MARKETACTION_BUY, 200, 3, 900, 0, false);
	repo.createOffer(3, MARKETACTION_BUY, 100, 2, 800, 1, false);

	auto filtered = repo.getActiveOffers(MARKETACTION_BUY, 100, 0);
	ASSERT_EQ(1u, filtered.size());
	EXPECT_EQ(100u, filtered.front().itemId);
	EXPECT_EQ(0u, filtered.front().tier);
}

// --- getOwnOffers ---

TEST_F(InMemoryMarketRepositoryTest, GetOwnOffers_ReturnsOnlyPlayerOffers) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);
	repo.createOffer(2, MARKETACTION_BUY, 100, 3, 900, 0, false);

	auto offers = repo.getOwnOffers(MARKETACTION_BUY, 1);
	ASSERT_EQ(1u, offers.size());
}

// --- getPlayerOfferCount ---

TEST_F(InMemoryMarketRepositoryTest, GetPlayerOfferCount_ReturnsZero_WhenNoOffers) {
	EXPECT_EQ(0u, repo.getPlayerOfferCount(42));
}

TEST_F(InMemoryMarketRepositoryTest, GetPlayerOfferCount_CountsActiveOffers) {
	repo.createOffer(5, MARKETACTION_BUY, 100, 1, 100, 0, false);
	repo.createOffer(5, MARKETACTION_SELL, 200, 1, 200, 0, false);

	EXPECT_EQ(2u, repo.getPlayerOfferCount(5));
}

// --- deleteOffer ---

TEST_F(InMemoryMarketRepositoryTest, DeleteOffer_RemovesFromActiveOffers) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);
	auto before = repo.getActiveOffers(MARKETACTION_BUY);
	ASSERT_EQ(1u, before.size());
	uint32_t offerId = repo.getLastCreatedOfferId();

	repo.deleteOffer(offerId);

	EXPECT_TRUE(repo.getActiveOffers(MARKETACTION_BUY).empty());
}

TEST_F(InMemoryMarketRepositoryTest, DeleteOffer_DecrementsPlayerOfferCount) {
	repo.createOffer(7, MARKETACTION_BUY, 100, 1, 500, 0, false);
	EXPECT_EQ(1u, repo.getPlayerOfferCount(7));

	repo.deleteOffer(repo.getLastCreatedOfferId());

	EXPECT_EQ(0u, repo.getPlayerOfferCount(7));
}

// --- appendHistory / getOwnHistory ---

TEST_F(InMemoryMarketRepositoryTest, GetOwnHistory_ReturnsEmpty_WhenNone) {
	auto history = repo.getOwnHistory(MARKETACTION_BUY, 1);
	EXPECT_TRUE(history.empty());
}

TEST_F(InMemoryMarketRepositoryTest, AppendHistory_AppearsInGetOwnHistory) {
	repo.appendHistory(3, MARKETACTION_BUY, 100, 5, 1000, 0, 0, OFFERSTATE_ACCEPTED);

	auto history = repo.getOwnHistory(MARKETACTION_BUY, 3);
	ASSERT_EQ(1u, history.size());
	EXPECT_EQ(100u, history.front().itemId);
	EXPECT_EQ(OFFERSTATE_ACCEPTED, history.front().state);
}

TEST_F(InMemoryMarketRepositoryTest, GetOwnHistory_FiltersByAction) {
	repo.appendHistory(3, MARKETACTION_BUY, 100, 5, 1000, 0, 0, OFFERSTATE_ACCEPTED);
	repo.appendHistory(3, MARKETACTION_SELL, 200, 2, 500, 0, 0, OFFERSTATE_CANCELLED);

	EXPECT_EQ(1u, repo.getOwnHistory(MARKETACTION_BUY, 3).size());
	EXPECT_EQ(1u, repo.getOwnHistory(MARKETACTION_SELL, 3).size());
}

// --- moveOfferToHistory ---

TEST_F(InMemoryMarketRepositoryTest, MoveOfferToHistory_ReturnsFalse_WhenOfferNotFound) {
	EXPECT_FALSE(repo.moveOfferToHistory(9999, OFFERSTATE_CANCELLED));
}

TEST_F(InMemoryMarketRepositoryTest, MoveOfferToHistory_RemovesFromActive) {
	repo.createOffer(1, MARKETACTION_BUY, 100, 5, 1000, 0, false);
	uint32_t id = repo.getLastCreatedOfferId();

	EXPECT_TRUE(repo.moveOfferToHistory(id, OFFERSTATE_CANCELLED));
	EXPECT_TRUE(repo.getActiveOffers(MARKETACTION_BUY).empty());
}
