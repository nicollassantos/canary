/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "io/market_repository.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <mutex>
#endif

class IConfigManager;
class IDatabase;

/// IMarketRepository adapter that owns the market SQL and the cached statistics.
/// Synchronous queries use the injected database; appendHistory and loadExpiredOffers
/// go through the shared DatabaseTasks pool, which runs on the global Database.
class MarketRepositoryDB final : public IMarketRepository {
public:
	MarketRepositoryDB(IDatabase &db, IConfigManager &config) :
		db_(db), config_(config) { }

	[[nodiscard]] MarketOfferList getActiveOffers(MarketAction_t action) override;
	[[nodiscard]] MarketOfferList getActiveOffers(MarketAction_t action, uint16_t itemId, uint8_t tier) override;
	[[nodiscard]] MarketOfferList getOwnOffers(MarketAction_t action, uint32_t playerId) override;
	[[nodiscard]] HistoryMarketOfferList getOwnHistory(MarketAction_t action, uint32_t playerId) override;

	[[nodiscard]] uint32_t getPlayerOfferCount(uint32_t playerId) override;
	[[nodiscard]] MarketOfferEx getOfferByCounter(uint32_t timestamp, uint16_t counter) override;

	void createOffer(uint32_t playerId, MarketAction_t action, uint32_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) override;
	void acceptOffer(uint32_t offerId, uint16_t amount) override;
	void deleteOffer(uint32_t offerId) override;

	void appendHistory(uint32_t playerId, MarketAction_t type, uint16_t itemId, uint16_t amount, uint64_t price, time_t timestamp, uint8_t tier, MarketOfferState_t state) override;
	bool moveOfferToHistory(uint32_t offerId, MarketOfferState_t state) override;

	void loadExpiredOffers(time_t lastExpireDate, std::function<void(std::vector<ExpiredMarketOffer>)> onLoaded) override;
	void updateStatistics() override;
	void loadStatistics(std::function<void(bool loaded, StatisticsMap purchase, StatisticsMap sale)> onLoaded) override;
	void setStatistics(StatisticsMap purchase, StatisticsMap sale) override;
	void loadActiveOffers(MarketAction_t action, std::function<void(MarketOfferList)> onLoaded) override;
	[[nodiscard]] StatisticsMap getPurchaseStatistics() const override;
	[[nodiscard]] StatisticsMap getSaleStatistics() const override;
	[[nodiscard]] StatisticsSnapshot getStatistics(uint16_t itemId, uint8_t tier) const override;

private:
	IDatabase &db_;
	IConfigManager &config_;

	// [uint16_t = item id, [uint8_t = item tier, MarketStatistics = structure of the statistics]]
	StatisticsMap purchaseStatistics;
	StatisticsMap saleStatistics;
	mutable std::mutex statisticsMutex;
};
