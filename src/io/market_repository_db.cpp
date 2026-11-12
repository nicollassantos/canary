/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/market_repository_db.hpp"

#include "config/config_port.hpp"
#include "database/database.hpp"
#include "database/database_port.hpp"
#include "database/databasetasks.hpp"
#include "lib/di/container.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <sstream>
#endif

namespace {

	uint8_t getTierFromDatabaseTable(const std::string &string, int32_t maxTier) {
		auto tier = static_cast<uint8_t>(std::atoi(string.c_str()));
		if (tier > maxTier) {
			g_logger().error("{} - Failed to get number value {} for tier table result", __FUNCTION__, tier);
			return 0;
		}

		return tier;
	}

	std::vector<ExpiredMarketOffer> readExpiredOffers(const DBResult_ptr &result, int32_t maxTier) {
		std::vector<ExpiredMarketOffer> offers;
		if (!result) {
			return offers;
		}

		do {
			ExpiredMarketOffer offer;
			offer.id = result->getNumber<uint32_t>("id");
			offer.playerId = result->getNumber<uint32_t>("player_id");
			offer.action = static_cast<MarketAction_t>(result->getNumber<uint16_t>("sale"));
			offer.itemId = result->getNumber<uint16_t>("itemtype");
			offer.amount = result->getNumber<uint16_t>("amount");
			offer.price = result->getNumber<uint64_t>("price");
			offer.tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);
			offers.push_back(offer);
		} while (result->next());
		return offers;
	}

	std::string activeOffersQuery(MarketAction_t action) {
		return fmt::format(
			"SELECT `id`, `itemtype`, `amount`, `price`, `tier`, `created`, `anonymous`, "
			"(SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `player_name` "
			"FROM `market_offers` WHERE `sale` = {}",
			action
		);
	}

	MarketOfferList readActiveOffers(const DBResult_ptr &result, int32_t marketOfferDuration, int32_t maxTier) {
		MarketOfferList offerList;
		if (!result) {
			return offerList;
		}

		do {
			MarketOffer offer;
			offer.itemId = result->getNumber<uint16_t>("itemtype");
			offer.amount = result->getNumber<uint16_t>("amount");
			offer.price = result->getNumber<uint64_t>("price");
			offer.timestamp = result->getNumber<uint32_t>("created") + marketOfferDuration;
			offer.counter = (result->getNumber<uint32_t>("id") ^ 0xABCDEF) & 0xFFFF;
			if (result->getNumber<uint16_t>("anonymous") == 0) {
				offer.playerName = result->getString("player_name");
			} else {
				offer.playerName = "Anonymous";
			}
			offer.tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);
			offerList.push_back(offer);
		} while (result->next());
		return offerList;
	}

	std::string statisticsQuery() {
		return fmt::format(
			"SELECT sale, itemtype, COUNT(price) AS num, MIN(price) AS min, MAX(price) AS max, SUM(price) AS sum, tier "
			"FROM market_history "
			"WHERE state = '{}' "
			"GROUP BY itemtype, sale, tier",
			OFFERSTATE_ACCEPTED
		);
	}

	void readStatistics(const DBResult_ptr &result, int32_t maxTier, IMarketRepository::StatisticsMap &newPurchase, IMarketRepository::StatisticsMap &newSale) {
		if (!result) {
			return;
		}

		do {
			const auto tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);
			const auto itemId = result->getNumber<uint16_t>("itemtype");
			auto &statistics = (result->getNumber<uint16_t>("sale") == MARKETACTION_BUY)
				? newPurchase[itemId][tier]
				: newSale[itemId][tier];

			statistics.numTransactions = result->getNumber<uint32_t>("num");
			statistics.lowestPrice = result->getNumber<uint64_t>("min");
			statistics.totalPrice = result->getNumber<uint64_t>("sum");
			statistics.highestPrice = result->getNumber<uint64_t>("max");
		} while (result->next());
	}

} // namespace

IMarketRepository &IMarketRepository::getInstance() {
	return DI::port<IMarketRepository>();
}

MarketOfferList MarketRepositoryDB::getActiveOffers(MarketAction_t action) {
	return readActiveOffers(db_.storeQuery(activeOffersQuery(action)), config_.getNumber(MARKET_OFFER_DURATION), config_.getNumber(FORGE_MAX_ITEM_TIER));
}

void MarketRepositoryDB::loadActiveOffers(MarketAction_t action, std::function<void(MarketOfferList)> onLoaded) {
	// The callback outlives this call; it captures values only.
	const int32_t marketOfferDuration = config_.getNumber(MARKET_OFFER_DURATION);
	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);
	g_databaseTasks().store(activeOffersQuery(action), [marketOfferDuration, maxTier, onLoaded = std::move(onLoaded)](const DBResult_ptr &result, bool) {
		onLoaded(readActiveOffers(result, marketOfferDuration, maxTier));
	});
}

MarketOfferList MarketRepositoryDB::getActiveOffers(MarketAction_t action, uint16_t itemId, uint8_t tier) {
	MarketOfferList offerList;

	const std::string priceOrder = (action == MARKETACTION_BUY) ? "DESC" : "ASC";
	std::ostringstream query;
	query << "SELECT `id`, `amount`, `price`, `tier`, `created`, `anonymous`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `player_name` FROM `market_offers` WHERE `sale` = " << action << " AND `itemtype` = " << itemId << " AND `tier` = " << std::to_string(tier) << " ORDER BY `price` " << priceOrder << ", `created` ASC";

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return offerList;
	}

	const int32_t marketOfferDuration = config_.getNumber(MARKET_OFFER_DURATION);
	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);

	do {
		MarketOffer offer;
		offer.itemId = itemId;
		offer.amount = result->getNumber<uint16_t>("amount");
		offer.price = result->getNumber<uint64_t>("price");
		offer.timestamp = result->getNumber<uint32_t>("created") + marketOfferDuration;
		offer.counter = (result->getNumber<uint32_t>("id") ^ 0xABCDEF) & 0xFFFF;
		if (result->getNumber<uint16_t>("anonymous") == 0) {
			offer.playerName = result->getString("player_name");
		} else {
			offer.playerName = "Anonymous";
		}
		offer.tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);
		offerList.push_back(offer);
	} while (result->next());
	return offerList;
}

MarketOfferList MarketRepositoryDB::getOwnOffers(MarketAction_t action, uint32_t playerId) {
	MarketOfferList offerList;

	const int32_t marketOfferDuration = config_.getNumber(MARKET_OFFER_DURATION);

	std::ostringstream query;
	query << "SELECT `id`, `amount`, `price`, `created`, `itemtype`, `tier` FROM `market_offers` WHERE `player_id` = " << playerId << " AND `sale` = " << action << " ORDER BY `created` ASC";

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return offerList;
	}

	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);

	do {
		MarketOffer offer;
		offer.amount = result->getNumber<uint16_t>("amount");
		offer.price = result->getNumber<uint64_t>("price");
		offer.timestamp = result->getNumber<uint32_t>("created") + marketOfferDuration;
		offer.counter = (result->getNumber<uint32_t>("id") ^ 0xABCDEF) & 0xFFFF;
		offer.itemId = result->getNumber<uint16_t>("itemtype");
		offer.tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);
		offerList.push_back(offer);
	} while (result->next());
	return offerList;
}

HistoryMarketOfferList MarketRepositoryDB::getOwnHistory(MarketAction_t action, uint32_t playerId) {
	HistoryMarketOfferList offerList;

	std::ostringstream query;
	query << "SELECT `itemtype`, `amount`, `price`, `expires_at`, `state`, `tier` FROM `market_history` WHERE `player_id` = " << playerId << " AND `sale` = " << action;

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return offerList;
	}

	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);

	do {
		HistoryMarketOffer offer {};
		offer.itemId = result->getNumber<uint16_t>("itemtype");
		offer.amount = result->getNumber<uint16_t>("amount");
		offer.price = result->getNumber<uint64_t>("price");
		offer.timestamp = result->getNumber<uint32_t>("expires_at");
		offer.tier = getTierFromDatabaseTable(result->getString("tier"), maxTier);

		MarketOfferState_t offerState = static_cast<MarketOfferState_t>(result->getNumber<uint16_t>("state"));
		if (offerState == OFFERSTATE_ACCEPTEDEX) {
			offerState = OFFERSTATE_ACCEPTED;
		}

		offer.state = offerState;

		offerList.push_back(offer);
	} while (result->next());
	return offerList;
}

void MarketRepositoryDB::loadExpiredOffers(time_t lastExpireDate, std::function<void(std::vector<ExpiredMarketOffer>)> onLoaded) {
	std::ostringstream query;
	query << "SELECT `id`, `amount`, `price`, `itemtype`, `player_id`, `sale`, `tier` FROM `market_offers` WHERE `created` <= " << lastExpireDate;

	// The callback outlives this call; it captures values only.
	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);
	g_databaseTasks().store(query.str(), [maxTier, onLoaded = std::move(onLoaded)](const DBResult_ptr &result, bool) {
		onLoaded(readExpiredOffers(result, maxTier));
	});
}

uint32_t MarketRepositoryDB::getPlayerOfferCount(uint32_t playerId) {
	std::ostringstream query;
	query << "SELECT COUNT(*) AS `count` FROM `market_offers` WHERE `player_id` = " << playerId;

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return 0;
	}
	return result->getNumber<int32_t>("count");
}

MarketOfferEx MarketRepositoryDB::getOfferByCounter(uint32_t timestamp, uint16_t counter) {
	MarketOfferEx offer;

	const int32_t created = timestamp - config_.getNumber(MARKET_OFFER_DURATION);

	std::ostringstream query;
	query << "SELECT `id`, `sale`, `itemtype`, `amount`, `created`, `price`, `player_id`, `anonymous`, `tier`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `player_name` FROM `market_offers` WHERE `created` = " << created << " AND ((`id` ^ 0xABCDEF) & 65535) = " << counter << " LIMIT 1";

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		offer.id = 0;
		return offer;
	}

	offer.id = result->getNumber<uint32_t>("id");
	offer.type = static_cast<MarketAction_t>(result->getNumber<uint16_t>("sale"));
	offer.amount = result->getNumber<uint16_t>("amount");
	offer.counter = (result->getNumber<uint32_t>("id") ^ 0xABCDEF) & 0xFFFF;
	offer.timestamp = result->getNumber<uint32_t>("created");
	offer.price = result->getNumber<uint64_t>("price");
	offer.itemId = result->getNumber<uint16_t>("itemtype");
	offer.playerId = result->getNumber<uint32_t>("player_id");
	offer.tier = getTierFromDatabaseTable(result->getString("tier"), config_.getNumber(FORGE_MAX_ITEM_TIER));
	if (result->getNumber<uint16_t>("anonymous") == 0) {
		offer.playerName = result->getString("player_name");
	} else {
		offer.playerName = "Anonymous";
	}
	return offer;
}

void MarketRepositoryDB::createOffer(uint32_t playerId, MarketAction_t action, uint32_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) {
	std::ostringstream query;
	query << "INSERT INTO `market_offers` (`player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, `price`, `tier`) VALUES (" << playerId << ',' << action << ',' << itemId << ',' << amount << ',' << getTimeNow() << ',' << anonymous << ',' << price << ',' << std::to_string(tier) << ')';
	std::ignore = db_.executeQuery(query.str());
}

void MarketRepositoryDB::acceptOffer(uint32_t offerId, uint16_t amount) {
	std::ostringstream query;
	query << "UPDATE `market_offers` SET `amount` = `amount` - " << amount << " WHERE `id` = " << offerId;
	std::ignore = db_.executeQuery(query.str());
}

void MarketRepositoryDB::deleteOffer(uint32_t offerId) {
	std::ostringstream query;
	query << "DELETE FROM `market_offers` WHERE `id` = " << offerId;
	std::ignore = db_.executeQuery(query.str());
}

void MarketRepositoryDB::appendHistory(uint32_t playerId, MarketAction_t type, uint16_t itemId, uint16_t amount, uint64_t price, time_t timestamp, uint8_t tier, MarketOfferState_t state) {
	std::ostringstream query;
	query << "INSERT INTO `market_history` (`player_id`, `sale`, `itemtype`, `amount`, `price`, `expires_at`, `inserted`, `state`, `tier`) VALUES ("
		  << playerId << ',' << type << ',' << itemId << ',' << amount << ',' << price << ','
		  << timestamp << ',' << getTimeNow() << ',' << state << ',' << std::to_string(tier) << ')';
	g_databaseTasks().execute(query.str());
}

bool MarketRepositoryDB::moveOfferToHistory(uint32_t offerId, MarketOfferState_t state) {
	std::ostringstream query;
	query << "SELECT `player_id`, `sale`, `itemtype`, `amount`, `price`, `created`, `tier` FROM `market_offers` WHERE `id` = " << offerId;

	DBResult_ptr result = db_.storeQuery(query.str());
	if (!result) {
		return false;
	}

	query.str(std::string());
	query << "DELETE FROM `market_offers` WHERE `id` = " << offerId;
	if (!db_.executeQuery(query.str())) {
		return false;
	}

	appendHistory(
		result->getNumber<uint32_t>("player_id"),
		static_cast<MarketAction_t>(result->getNumber<uint16_t>("sale")),
		result->getNumber<uint16_t>("itemtype"),
		result->getNumber<uint16_t>("amount"),
		result->getNumber<uint64_t>("price"),
		getTimeNow(),
		getTierFromDatabaseTable(result->getString("tier"), config_.getNumber(FORGE_MAX_ITEM_TIER)), state
	);
	return true;
}

void MarketRepositoryDB::updateStatistics() {
	DBResult_ptr result = db_.storeQuery(statisticsQuery());
	if (!result) {
		return;
	}

	StatisticsMap newPurchase;
	StatisticsMap newSale;
	readStatistics(result, config_.getNumber(FORGE_MAX_ITEM_TIER), newPurchase, newSale);
	setStatistics(std::move(newPurchase), std::move(newSale));
}

void MarketRepositoryDB::loadStatistics(std::function<void(StatisticsMap purchase, StatisticsMap sale)> onLoaded) {
	// Off the game thread: on a large market_history this query takes tens to hundreds of ms.
	const int32_t maxTier = config_.getNumber(FORGE_MAX_ITEM_TIER);
	g_databaseTasks().store(statisticsQuery(), [maxTier, onLoaded = std::move(onLoaded)](const DBResult_ptr &result, bool) {
		StatisticsMap newPurchase;
		StatisticsMap newSale;
		readStatistics(result, maxTier, newPurchase, newSale);
		onLoaded(std::move(newPurchase), std::move(newSale));
	});
}

void MarketRepositoryDB::setStatistics(StatisticsMap purchase, StatisticsMap sale) {
	std::scoped_lock lock(statisticsMutex);
	purchaseStatistics.swap(purchase);
	saleStatistics.swap(sale);
}

IMarketRepository::StatisticsMap MarketRepositoryDB::getPurchaseStatistics() const {
	std::scoped_lock lock(statisticsMutex);
	return purchaseStatistics;
}

IMarketRepository::StatisticsMap MarketRepositoryDB::getSaleStatistics() const {
	std::scoped_lock lock(statisticsMutex);
	return saleStatistics;
}

IMarketRepository::StatisticsSnapshot MarketRepositoryDB::getStatistics(uint16_t itemId, uint8_t tier) const {
	StatisticsSnapshot snapshot;
	std::scoped_lock lock(statisticsMutex);

	if (const auto purchaseIt = purchaseStatistics.find(itemId); purchaseIt != purchaseStatistics.end()) {
		if (const auto tierIt = purchaseIt->second.find(tier); tierIt != purchaseIt->second.end()) {
			snapshot.purchase = tierIt->second;
		}
	}

	if (const auto saleIt = saleStatistics.find(itemId); saleIt != saleStatistics.end()) {
		if (const auto tierIt = saleIt->second.find(tier); tierIt != saleIt->second.end()) {
			snapshot.sale = tierIt->second;
		}
	}

	return snapshot;
}
