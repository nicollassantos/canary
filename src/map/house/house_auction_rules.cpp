/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "map/house/house_auction_rules.hpp"

#include "account/account.hpp"
#include "creatures/players/grouping/guild.hpp"
#include "creatures/players/player.hpp"
#include "map/house/house.hpp"

namespace house_auction_rules {

	BidErrorMessage canBid(const Player &bidder, const House &house) {
		using enum BidErrorMessage;
		if (bidder.getPlayerVocationEnum() == Vocation_t::VOCATION_NONE) {
			return Rookgaard;
		}

		if (!bidder.isPremium()) {
			return Premium;
		}

		if (bidder.getAccount()->getHouseBidId() != 0) {
			return OnlyOneBid;
		}

		if (bidder.getBankBalance() < (house.getRent() + house.getHighestBid())) {
			return NotEnoughMoney;
		}

		if (house.isGuildhall()) {
			if (bidder.getGuildRank() && bidder.getGuildRank()->level != 3) {
				return Guildhall;
			}

			if (bidder.getGuild() && bidder.getGuild()->getBankBalance() < (house.getRent() + house.getHighestBid())) {
				return NotEnoughGuildMoney;
			}
		}

		return NoError;
	}

	TransferErrorMessage canTransfer(const Player &owner, const House &house, const Player &newOwner) {
		using enum TransferErrorMessage;
		if (owner.getGUID() != house.getOwner()) {
			return NotHouseOwner;
		}

		if (owner.getGUID() == newOwner.getGUID()) {
			return AlreadyTheOwner;
		}

		if (newOwner.getPlayerVocationEnum() == Vocation_t::VOCATION_NONE) {
			return Rookgaard;
		}

		if (!newOwner.isPremium()) {
			return Premium;
		}

		if (newOwner.getAccount()->getHouseBidId() != 0) {
			return OnlyOneBid;
		}

		return Success;
	}

	AcceptTransferErrorMessage canAcceptTransfer(const Player &newOwner, const House &house) {
		using enum AcceptTransferErrorMessage;
		if (newOwner.getGUID() != static_cast<uint32_t>(house.getBidder())) {
			return NotNewOwner;
		}

		if (!newOwner.isPremium()) {
			return Premium;
		}

		if (newOwner.getAccount()->getHouseBidId() != 0) {
			return AlreadyBid;
		}

		if (newOwner.getPlayerVocationEnum() == Vocation_t::VOCATION_NONE) {
			return Rookgaard;
		}

		if (newOwner.getBankBalance() < (house.getRent() + house.getInternalBid())) {
			return Frozen;
		}

		if (house.getTransferStatus()) {
			return AlreadyAccepted;
		}

		return Success;
	}

} // namespace house_auction_rules
