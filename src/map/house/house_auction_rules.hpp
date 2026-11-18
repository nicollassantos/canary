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
}
