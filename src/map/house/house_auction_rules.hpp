/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "enums/player_cyclopedia.hpp"
#include "map/house/house_repository.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <optional>
	#include <string_view>
	#include <utility>
	#include <vector>
#endif

class House;
class Player;

/// Who may take part in the Cyclopedia house auction: bid on a house, transfer one, accept a
/// transfer. The callers resolve the house and the characters (by client id, by name); the rules
/// only read them, so they need no global game state and are tested directly.
namespace house_auction_rules {
	/// A vocation, premium, no bid on another house, bank money for the rent plus the highest bid
	/// and, for a guildhall, the guild leader whose guild bank covers the same.
	BidErrorMessage canBid(const Player &bidder, const House &house);

	/// `owner` owns `house`, `newOwner` is someone else, with a vocation, premium and no bid on
	/// another house.
	TransferErrorMessage canTransfer(const Player &owner, const House &house, const Player &newOwner);

	/// `newOwner` is the one the transfer names, premium, with a vocation, no bid on another house,
	/// bank money for the rent plus the transfer price, and the transfer not accepted yet.
	AcceptTransferErrorMessage canAcceptTransfer(const Player &newOwner, const House &house);

	/// What the server does with a stored house when it starts.
	struct StartupResolution {
		/// Why (for the log): "transfer on restart", "bid won", "transfer accepted", "transfer refused",
		/// "move out", or empty when the stored owner simply stays.
		std::string_view reason;
		/// Give the house to this owner (0: nobody), recording it; nothing: keep the stored owner and state.
		std::optional<uint32_t> newOwner;
		/// Move the items in the house to this character's depot before the owner changes.
		std::optional<uint32_t> moveItemsOf;
		/// Bank credits: (character, amount).
		std::vector<std::pair<uint32_t, uint64_t>> credits;
		/// The auction state the house keeps afterwards.
		uint32_t bidder = 0;
		std::string bidderName;
		uint64_t highestBid = 0;
		uint64_t internalBid = 0;
		uint32_t bidEndDate = 0;
		bool transferStatus = false;
	};

	/// The owner changes decided at startup from the stored state, at time `now` (seconds):
	/// a pending transfer on restart, a won auction (the winner gets back what they bid above the price),
	/// a due transfer (accepted: the new owner gets the house and the old one is paid the price;
	/// otherwise the old owner keeps it) and a due move out.
	StartupResolution resolveAtStartup(const StoredHouse &stored, int64_t now, bool transferOnRestart);
}
