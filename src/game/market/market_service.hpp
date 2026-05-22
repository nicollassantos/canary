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
#include <memory>

class Game;
class IConfigManager;

class MarketService {
public:
	MarketService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerBrowseMarket(uint32_t playerId, uint16_t itemId, uint8_t tier);
	void playerBrowseMarketOwnOffers(uint32_t playerId);
	void playerBrowseMarketOwnHistory(uint32_t playerId);
	void playerCreateMarketOffer(uint32_t playerId, uint8_t type, uint16_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous);
	void playerCancelMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter);
	void playerAcceptMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter, uint16_t amount);

private:
	Game &game_;
	IConfigManager &config_;
};
