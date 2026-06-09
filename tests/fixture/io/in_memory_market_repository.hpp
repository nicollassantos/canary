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
#include <list>
#include <unordered_map>
#include <vector>

#include "creatures/creatures_definitions.hpp"
#include "io/market_repository.hpp"
#include "test_injection.hpp"
#include "lib/di/container.hpp"

namespace di = boost::di;

class InMemoryMarketRepository final : public IMarketRepository {
	struct StoredOffer {
		uint32_t id;
		uint32_t playerId;
		MarketAction_t action;
		uint16_t itemId;
		uint16_t amount;
		uint64_t price;
		uint8_t tier;
		bool anonymous;
		uint32_t timestamp;
		uint16_t counter;
	};

	struct StoredHistory {
		uint32_t playerId;
		MarketAction_t action;
		uint16_t itemId;
		uint16_t amount;
		uint64_t price;
		time_t timestamp;
		uint8_t tier;
		MarketOfferState_t state;
	};

public:
	InMemoryMarketRepository() = default;
	InMemoryMarketRepository(const InMemoryMarketRepository &) { }
	InMemoryMarketRepository(InMemoryMarketRepository &&) { }

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IMarketRepository>.to<InMemoryMarketRepository>().in(di::singleton));
		return injector;
	}

	InMemoryMarketRepository &reset() {
		offers.clear();
		history.clear();
		nextId = 1;
		lastId = 0;
		return *this;
	}

	uint32_t getLastCreatedOfferId() const {
		return lastId;
	}

	// --- IMarketRepository ---

	MarketOfferList getActiveOffers(MarketAction_t action) override {
		MarketOfferList result;
		for (const auto &[id, o] : offers) {
			if (o.action == action) {
				result.push_back(toOffer(o));
			}
		}
		return result;
	}

	MarketOfferList getActiveOffers(MarketAction_t action, uint16_t itemId, uint8_t tier) override {
		MarketOfferList result;
		for (const auto &[id, o] : offers) {
			if (o.action == action && o.itemId == itemId && o.tier == tier) {
				result.push_back(toOffer(o));
			}
		}
		return result;
	}

	MarketOfferList getOwnOffers(MarketAction_t action, uint32_t playerId) override {
		MarketOfferList result;
		for (const auto &[id, o] : offers) {
			if (o.action == action && o.playerId == playerId) {
				result.push_back(toOffer(o));
			}
		}
		return result;
	}

	HistoryMarketOfferList getOwnHistory(MarketAction_t action, uint32_t playerId) override {
		HistoryMarketOfferList result;
		for (const auto &h : history) {
			if (h.action == action && h.playerId == playerId) {
				HistoryMarketOffer entry;
				entry.itemId = h.itemId;
				entry.amount = h.amount;
				entry.price = h.price;
				entry.timestamp = static_cast<uint32_t>(h.timestamp);
				entry.tier = h.tier;
				entry.state = h.state;
				result.push_back(entry);
			}
		}
		return result;
	}

	uint32_t getPlayerOfferCount(uint32_t playerId) override {
		uint32_t count = 0;
		for (const auto &[id, o] : offers) {
			if (o.playerId == playerId) {
				++count;
			}
		}
		return count;
	}

	MarketOfferEx getOfferByCounter(uint32_t timestamp, uint16_t counter) override {
		for (const auto &[id, o] : offers) {
			if (o.timestamp == timestamp && o.counter == counter) {
				return toOfferEx(o);
			}
		}
		return {};
	}

	void createOffer(uint32_t playerId, MarketAction_t action, uint32_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) override {
		const uint32_t id = nextId++;
		lastId = id;
		StoredOffer o;
		o.id = id;
		o.playerId = playerId;
		o.action = action;
		o.itemId = static_cast<uint16_t>(itemId);
		o.amount = amount;
		o.price = price;
		o.tier = tier;
		o.anonymous = anonymous;
		o.timestamp = static_cast<uint32_t>(std::time(nullptr));
		o.counter = static_cast<uint16_t>(id & 0xFFFF);
		offers[id] = o;
	}

	void acceptOffer(uint32_t offerId, uint16_t amount) override {
		auto it = offers.find(offerId);
		if (it != offers.end()) {
			if (it->second.amount <= amount) {
				offers.erase(it);
			} else {
				it->second.amount -= amount;
			}
		}
	}

	void deleteOffer(uint32_t offerId) override {
		offers.erase(offerId);
	}

	void appendHistory(uint32_t playerId, MarketAction_t type, uint16_t itemId, uint16_t amount, uint64_t price, time_t timestamp, uint8_t tier, MarketOfferState_t state) override {
		history.push_back({ playerId, type, itemId, amount, price, timestamp, tier, state });
	}

	bool moveOfferToHistory(uint32_t offerId, MarketOfferState_t state) override {
		auto it = offers.find(offerId);
		if (it == offers.end()) {
			return false;
		}
		const auto &o = it->second;
		appendHistory(o.playerId, o.action, o.itemId, o.amount, o.price, std::time(nullptr), o.tier, state);
		offers.erase(it);
		return true;
	}

private:
	std::unordered_map<uint32_t, StoredOffer> offers;
	std::vector<StoredHistory> history;
	uint32_t nextId = 1;
	uint32_t lastId = 0;

	static MarketOffer toOffer(const StoredOffer &o) {
		MarketOffer m;
		m.itemId = o.itemId;
		m.amount = o.amount;
		m.price = o.price;
		m.tier = o.tier;
		m.timestamp = o.timestamp;
		m.counter = o.counter;
		m.playerName = o.anonymous ? "" : std::to_string(o.playerId);
		return m;
	}

	static MarketOfferEx toOfferEx(const StoredOffer &o) {
		MarketOfferEx m;
		m.id = o.id;
		m.playerId = o.playerId;
		m.itemId = o.itemId;
		m.amount = o.amount;
		m.price = o.price;
		m.tier = o.tier;
		m.timestamp = o.timestamp;
		m.counter = o.counter;
		m.type = o.action;
		m.playerName = o.anonymous ? "" : std::to_string(o.playerId);
		return m;
	}
};

template <>
struct TestInjection<IMarketRepository> {
	using type = InMemoryMarketRepository;
};
