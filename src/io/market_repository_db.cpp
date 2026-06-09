/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/market_repository_db.hpp"

#include "io/iomarket.hpp"
#include "lib/di/container.hpp"

IMarketRepository &IMarketRepository::getInstance() {
	return inject<IMarketRepository>();
}

MarketOfferList MarketRepositoryDB::getActiveOffers(MarketAction_t action) {
	return IOMarket::getActiveOffers(action);
}

MarketOfferList MarketRepositoryDB::getActiveOffers(MarketAction_t action, uint16_t itemId, uint8_t tier) {
	return IOMarket::getActiveOffers(action, itemId, tier);
}

MarketOfferList MarketRepositoryDB::getOwnOffers(MarketAction_t action, uint32_t playerId) {
	return IOMarket::getOwnOffers(action, playerId);
}

HistoryMarketOfferList MarketRepositoryDB::getOwnHistory(MarketAction_t action, uint32_t playerId) {
	return IOMarket::getOwnHistory(action, playerId);
}

uint32_t MarketRepositoryDB::getPlayerOfferCount(uint32_t playerId) {
	return IOMarket::getPlayerOfferCount(playerId);
}

MarketOfferEx MarketRepositoryDB::getOfferByCounter(uint32_t timestamp, uint16_t counter) {
	return IOMarket::getOfferByCounter(timestamp, counter);
}

void MarketRepositoryDB::createOffer(uint32_t playerId, MarketAction_t action, uint32_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) {
	IOMarket::createOffer(playerId, action, itemId, amount, price, tier, anonymous);
}

void MarketRepositoryDB::acceptOffer(uint32_t offerId, uint16_t amount) {
	IOMarket::acceptOffer(offerId, amount);
}

void MarketRepositoryDB::deleteOffer(uint32_t offerId) {
	IOMarket::deleteOffer(offerId);
}

void MarketRepositoryDB::appendHistory(uint32_t playerId, MarketAction_t type, uint16_t itemId, uint16_t amount, uint64_t price, time_t timestamp, uint8_t tier, MarketOfferState_t state) {
	IOMarket::appendHistory(playerId, type, itemId, amount, price, timestamp, tier, state);
}

bool MarketRepositoryDB::moveOfferToHistory(uint32_t offerId, MarketOfferState_t state) {
	return IOMarket::moveOfferToHistory(offerId, state);
}
