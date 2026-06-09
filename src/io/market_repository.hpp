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
};

constexpr auto g_marketRepository = IMarketRepository::getInstance;
