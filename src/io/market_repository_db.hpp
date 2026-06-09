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

class MarketRepositoryDB final : public IMarketRepository {
public:
	MarketRepositoryDB() = default;

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
};
