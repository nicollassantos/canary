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

	StartupResolution resolveAtStartup(const StoredHouse &stored, int64_t now, bool transferOnRestart) {
		StartupResolution resolution;
		resolution.bidder = stored.bidder;
		resolution.bidderName = stored.bidderName;
		resolution.highestBid = stored.highestBid;
		resolution.internalBid = stored.internalBid;
		resolution.bidEndDate = stored.bidEndDate;
		resolution.transferStatus = stored.transferStatus;
		const bool due = now > static_cast<int64_t>(stored.bidEndDate);

		if (transferOnRestart && stored.newOwner >= 0) {
			resolution.reason = "transfer on restart";
			resolution.moveItemsOf = stored.owner;
			resolution.newOwner = static_cast<uint32_t>(stored.newOwner);
		} else if (stored.state == CyclopediaHouseState::Available && due && stored.bidder > 0) {
			resolution.reason = "bid won";
			if (stored.highestBid < stored.internalBid) {
				// The winner pays the second price: what they bid above it comes back.
				resolution.credits.emplace_back(stored.bidder, stored.internalBid - stored.highestBid);
			}
			resolution.newOwner = stored.bidder;
			resolution.bidder = 0;
			resolution.bidderName.clear();
			resolution.highestBid = 0;
			resolution.internalBid = 0;
			resolution.bidEndDate = 0;
		} else if (stored.state == CyclopediaHouseState::Transfer && due && stored.bidder > 0) {
			if (stored.transferStatus) {
				resolution.reason = "transfer accepted";
				resolution.moveItemsOf = stored.owner;
				resolution.newOwner = stored.bidder;
				resolution.credits.emplace_back(stored.owner, stored.internalBid);
			} else {
				resolution.reason = "transfer refused";
				resolution.newOwner = stored.owner;
			}
			resolution.bidder = 0;
			resolution.bidderName.clear();
			resolution.internalBid = 0;
			resolution.bidEndDate = 0;
			resolution.transferStatus = false;
		} else if (stored.state == CyclopediaHouseState::MoveOut && due) {
			resolution.reason = "move out";
			resolution.moveItemsOf = stored.owner;
			resolution.newOwner = 0;
			resolution.bidEndDate = 0;
		}
		return resolution;
	}

} // namespace house_auction_rules
