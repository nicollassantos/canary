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
	#include <vector>
#endif

class Game;
class IPlayerRepository;

namespace market_expiry {

	/// Loads expired offers asynchronously and reschedules itself every
	/// CHECK_EXPIRED_MARKET_OFFERS_EACH_MINUTES (disabled when <= 0).
	void checkExpiredOffers();

	/// Moves each offer to history, then returns its items (sell offer) or gold (buy offer)
	/// to the owner. Offers that cannot be moved are skipped without a refund.
	void processExpiredOffers(Game &game, IMarketRepository &market, IPlayerRepository &players, const std::vector<ExpiredMarketOffer> &offers);

} // namespace market_expiry
