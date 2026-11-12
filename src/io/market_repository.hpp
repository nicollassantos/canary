/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <ctime>

#include "creatures/creatures_definitions.hpp"
#include "io/io_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <functional>
	#include <map>
	#include <optional>
	#include <vector>
#endif

/// Offer row read by the expiry sweep; the game layer moves it to history and refunds its owner.
struct ExpiredMarketOffer {
	uint32_t id = 0;
	uint32_t playerId = 0;
	MarketAction_t action = MARKETACTION_BUY;
	uint16_t itemId = 0;
	uint16_t amount = 0;
	uint64_t price = 0;
	uint8_t tier = 0;
};

class IMarketRepository {
public:
	IMarketRepository() = default;
	virtual ~IMarketRepository() = default;

	IMarketRepository(const IMarketRepository &) = delete;
	void operator=(const IMarketRepository &) = delete;

	static IMarketRepository &getInstance();

	[[nodiscard]] virtual MarketOfferList getActiveOffers(MarketAction_t action) = 0;
	[[nodiscard]] virtual MarketOfferList getActiveOffers(MarketAction_t action, uint16_t itemId, uint8_t tier) = 0;
	[[nodiscard]] virtual MarketOfferList getOwnOffers(MarketAction_t action, uint32_t playerId) = 0;
	[[nodiscard]] virtual HistoryMarketOfferList getOwnHistory(MarketAction_t action, uint32_t playerId) = 0;

	[[nodiscard]] virtual uint32_t getPlayerOfferCount(uint32_t playerId) = 0;
	[[nodiscard]] virtual MarketOfferEx getOfferByCounter(uint32_t timestamp, uint16_t counter) = 0;

	virtual void createOffer(uint32_t playerId, MarketAction_t action, uint32_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) = 0;
	virtual void acceptOffer(uint32_t offerId, uint16_t amount) = 0;
	virtual void deleteOffer(uint32_t offerId) = 0;

	virtual void appendHistory(uint32_t playerId, MarketAction_t type, uint16_t itemId, uint16_t amount, uint64_t price, time_t timestamp, uint8_t tier, MarketOfferState_t state) = 0;
	virtual bool moveOfferToHistory(uint32_t offerId, MarketOfferState_t state) = 0;

	// [item id, [tier, statistics]]
	using StatisticsMap = std::map<uint16_t, std::map<uint8_t, MarketStatistics>>;
	struct StatisticsSnapshot {
		std::optional<MarketStatistics> purchase;
		std::optional<MarketStatistics> sale;
	};

	/// Loads offers created at or before lastExpireDate off the game thread.
	/// onLoaded runs later on the dispatcher; it must not capture state that can die first.
	virtual void loadExpiredOffers(time_t lastExpireDate, std::function<void(std::vector<ExpiredMarketOffer>)> onLoaded) = 0;
	/// Synchronous; used at boot. The periodic refresh uses loadStatistics + setStatistics.
	virtual void updateStatistics() = 0;
	/// Loads the statistics off the game thread; onLoaded runs later on the dispatcher with the new
	/// maps (purchase, sale). It must not capture state that can die first.
	virtual void loadStatistics(std::function<void(StatisticsMap purchase, StatisticsMap sale)> onLoaded) = 0;
	virtual void setStatistics(StatisticsMap purchase, StatisticsMap sale) = 0;
	/// Loads every active offer of one side off the game thread; onLoaded runs on the dispatcher.
	virtual void loadActiveOffers(MarketAction_t action, std::function<void(MarketOfferList)> onLoaded) = 0;
	[[nodiscard]] virtual StatisticsMap getPurchaseStatistics() const = 0;
	[[nodiscard]] virtual StatisticsMap getSaleStatistics() const = 0;
	[[nodiscard]] virtual StatisticsSnapshot getStatistics(uint16_t itemId, uint8_t tier) const = 0;
};

constexpr auto g_marketRepository = IMarketRepository::getInstance;
