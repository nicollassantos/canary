/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/cyclopedia/cyclopedia_service.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "database/database.hpp"
#include "database/databasetasks.hpp"
#include "enums/player_cyclopedia.hpp"
#include "game/game.hpp"
#include "game/game_definitions.hpp"
#include "creatures/players/player_repository.hpp"
#include "io/iologindata.hpp"
#include "lib/metrics/metrics.hpp"
#include "map/house/house.hpp"
#include "map/house/housetile.hpp"

void CyclopediaService::playerCyclopediaCharacterInfo(const std::shared_ptr<Player> &player, uint32_t characterID, CyclopediaCharacterInfoType_t characterInfoType, uint16_t entriesPerPage, uint16_t page) {
	uint32_t playerID = player->getID();
	if (playerID != characterID) {
		// For now allow viewing only our character since we don't have tournaments supported
		player->sendCyclopediaCharacterNoData(characterInfoType, 2);
		return;
	}

	switch (characterInfoType) {
		case CYCLOPEDIA_CHARACTERINFO_BASEINFORMATION:
			player->sendCyclopediaCharacterBaseInformation();
			break;
		case CYCLOPEDIA_CHARACTERINFO_GENERALSTATS:
			player->sendCyclopediaCharacterGeneralStats();
			break;
		case CYCLOPEDIA_CHARACTERINFO_RECENTDEATHS:
			player->cyclopedia().loadDeathHistory(page, entriesPerPage);
			break;
		case CYCLOPEDIA_CHARACTERINFO_RECENTPVPKILLS:
			player->cyclopedia().loadRecentKills(page, entriesPerPage);
			break;
		case CYCLOPEDIA_CHARACTERINFO_ACHIEVEMENTS:
			player->achiev().sendUnlockedSecretAchievements();
			break;
		case CYCLOPEDIA_CHARACTERINFO_ITEMSUMMARY: {
			const ItemsTierCountList &inventoryItems = player->getInventoryItemsId(true);
			const ItemsTierCountList &storeInboxItems = player->getStoreInboxItemsId();
			const StashItemList &stashItems = player->getStashItems();
			const ItemsTierCountList &depotBoxItems = player->getDepotChestItemsId();
			const ItemsTierCountList &inboxItems = player->getDepotInboxItemsId();

			player->sendCyclopediaCharacterItemSummary(inventoryItems, storeInboxItems, stashItems, depotBoxItems, inboxItems);
			break;
		}
		case CYCLOPEDIA_CHARACTERINFO_OUTFITSMOUNTS:
			player->sendCyclopediaCharacterOutfitsMounts();
			break;
		case CYCLOPEDIA_CHARACTERINFO_STORESUMMARY:
			player->sendCyclopediaCharacterStoreSummary();
			break;
		case CYCLOPEDIA_CHARACTERINFO_INSPECTION:
			player->sendCyclopediaCharacterInspection();
			break;
		case CYCLOPEDIA_CHARACTERINFO_BADGES:
			player->sendCyclopediaCharacterBadges();
			break;
		case CYCLOPEDIA_CHARACTERINFO_TITLES:
			player->sendCyclopediaCharacterTitles();
			break;
		case CYCLOPEDIA_CHARACTERINFO_WHEEL:
			game_.playerOpenWheel(playerID, characterID);
			break;
		case CYCLOPEDIA_CHARACTERINFO_OFFENCESTATS:
			player->sendCyclopediaCharacterOffenceStats();
			break;
		case CYCLOPEDIA_CHARACTERINFO_DEFENCESTATS:
			player->sendCyclopediaCharacterDefenceStats();
			break;
		case CYCLOPEDIA_CHARACTERINFO_MISCSTATS:
			player->sendCyclopediaCharacterMiscStats();
			break;
		default:
			player->sendCyclopediaCharacterNoData(characterInfoType, 1);
			break;
	}
}

void CyclopediaService::updatePlayersOnline() const {
	std::vector<uint32_t> onlineGuids;
	onlineGuids.reserve(game_.getPlayers().size());
	for (const auto &player : game_.getPlayers() | std::views::values) {
		onlineGuids.emplace_back(player->getGUID());
	}

	if (!g_playerRepository().updateOnlinePlayers(onlineGuids)) {
		g_logger().error("[Game::updatePlayersOnline] Failed to update players online.");
	}
}

void CyclopediaService::playerCyclopediaHousesByTown(uint32_t playerId, const std::string &townName) {
	std::shared_ptr<Player> player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	HouseMap houses;
	if (!townName.empty()) {
		const auto &housesList = game_.map.houses.getHouses();
		for (const auto &it : housesList) {
			const auto &house = it.second;
			const auto &town = game_.map.towns.getTown(house->getTownId());
			if (!town) {
				continue;
			}

			const std::string &houseTown = town->getName();
			if (houseTown == townName) {
				houses.emplace(house->getClientId(), house);
			}
		}
	} else {
		auto playerHouses = game_.map.houses.getAllHousesByPlayerId(player->getGUID());
		if (playerHouses.size()) {
			for (const auto &playerHouse : playerHouses) {
				if (!playerHouse) {
					continue;
				}
				houses.emplace(playerHouse->getClientId(), playerHouse);
			}
		}

		const auto house = game_.map.houses.getHouseByBidderName(player->getName());
		if (house) {
			houses.emplace(house->getClientId(), house);
		}
	}
	player->sendCyclopediaHouseList(houses);
}

void CyclopediaService::playerCyclopediaHouseBid(uint32_t playerId, uint32_t houseId, uint64_t bidValue) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	std::shared_ptr<Player> player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house) {
		return;
	}

	// Rented, transferring and moving-out houses keep their owner (and a transfer keeps the heir
	// in the bidder fields); only a house nobody owns is up for auction.
	if (house->getOwner() != 0 || house->getState() != CyclopediaHouseState::Available) {
		g_logger().warn("[{}] Player {} bid on house {}, which is not up for auction", __FUNCTION__, player->getName(), houseId);
		g_metrics().addCounter("house_auction_requests_rejected", 1, { { "reason", "not_on_auction" } });
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(BidErrorMessage::Internal));
		return;
	}

	auto ret = player->canBidHouse(houseId);
	if (ret != BidErrorMessage::NoError) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(ret));
		return;
	}
	ret = BidErrorMessage::NotEnoughMoney;
	auto retSuccess = BidSuccessMessage::BidSuccess;

	if (house->getBidderName().empty()) {
		if (!processBankAuction(player, house, bidValue)) {
			player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(ret));
			return;
		}
		house->setHighestBid(0);
		house->setInternalBid(bidValue);
		house->setBidHolderLimit(bidValue);
		house->setBidderName(player->getName());
		house->setBidder(player->getGUID());
		house->calculateBidEndDate(config_.getNumber(DAYS_TO_CLOSE_BID));
	} else if (house->getBidderName() == player->getName()) {
		if (!processBankAuction(player, house, bidValue, true)) {
			player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(ret));
			return;
		}
		house->setInternalBid(bidValue);
		house->setBidHolderLimit(bidValue);
	} else if (bidValue <= house->getInternalBid()) {
		house->setHighestBid(bidValue);
		retSuccess = BidSuccessMessage::LowerBid;
	} else {
		if (!processBankAuction(player, house, bidValue)) {
			player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(ret));
			return;
		}
		house->setHighestBid(house->getInternalBid() + 1);
		house->setInternalBid(bidValue);
		house->setBidHolderLimit(bidValue);
		house->setBidderName(player->getName());
		house->setBidder(player->getGUID());
	}

	const auto &town = game_.map.towns.getTown(house->getTownId());
	if (!town) {
		return;
	}

	const std::string houseTown = town->getName();
	player->sendHouseAuctionMessage(houseId, HouseAuctionType::Bid, enumToValue(retSuccess), true);
	playerCyclopediaHousesByTown(playerId, houseTown);
}

void CyclopediaService::playerCyclopediaHouseMoveOut(uint32_t playerId, uint32_t houseId, uint32_t timestamp) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	std::shared_ptr<Player> player = game_.getPlayerByID(playerId);
	if (!player) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getState() != CyclopediaHouseState::Rented) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::MoveOut, enumToValue(TransferErrorMessage::Internal));
		return;
	}

	if (house->getOwner() != player->getGUID()) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::MoveOut, enumToValue(TransferErrorMessage::NotHouseOwner));
		return;
	}

	house->setBidEndDate(timestamp);
	house->setState(CyclopediaHouseState::MoveOut);

	player->sendHouseAuctionMessage(houseId, HouseAuctionType::MoveOut, enumToValue(TransferErrorMessage::Success));
	playerCyclopediaHousesByTown(playerId, "");
}

void CyclopediaService::playerCyclopediaHouseCancelMoveOut(uint32_t playerId, uint32_t houseId) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	std::shared_ptr<Player> player = game_.getPlayerByID(playerId);
	if (!player) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getState() != CyclopediaHouseState::MoveOut) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelMoveOut, enumToValue(TransferErrorMessage::Internal));
		return;
	}

	if (house->getOwner() != player->getGUID()) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelMoveOut, enumToValue(TransferErrorMessage::NotHouseOwner));
		return;
	}

	house->setBidEndDate(0);
	house->setState(CyclopediaHouseState::Rented);

	player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelMoveOut, enumToValue(TransferErrorMessage::Success));
	playerCyclopediaHousesByTown(playerId, "");
}

void CyclopediaService::playerCyclopediaHouseTransfer(uint32_t playerId, uint32_t houseId, uint32_t timestamp, const std::string &newOwnerName, uint64_t bidValue) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	const std::shared_ptr<Player> &owner = game_.getPlayerByID(playerId);
	if (!owner) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	// The recipient lookup below may load a whole offline character, so every cheap check comes first.
	if (!houseTransferCooldown.tryTake(playerId, now())) {
		++throttledHouseTransfers_;
		g_metrics().addCounter("house_auction_requests_rejected", 1, { { "reason", "transfer_throttled" } });
		return;
	}

	if (newOwnerName.size() > NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		g_logger().warn("[{}] Player {} sent a house transfer recipient name of {} characters", __FUNCTION__, owner->getName(), newOwnerName.size());
		g_metrics().addCounter("house_auction_requests_rejected", 1, { { "reason", "recipient_name_too_long" } });
		owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(TransferErrorMessage::CharacterNotExist));
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getState() != CyclopediaHouseState::Rented) {
		owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(TransferErrorMessage::Internal));
		return;
	}

	if (house->getOwner() != owner->getGUID()) {
		owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(TransferErrorMessage::NotHouseOwner));
		return;
	}

	const std::shared_ptr<Player> &newOwner = game_.getPlayerByName(newOwnerName, true);
	if (!newOwner) {
		owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(TransferErrorMessage::CharacterNotExist));
		return;
	}

	auto ret = owner->canTransferHouse(houseId, newOwner->getGUID());
	if (ret != TransferErrorMessage::Success) {
		owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(ret));
		return;
	}

	house->setBidderName(newOwnerName);
	house->setBidder(newOwner->getGUID());
	house->setInternalBid(bidValue);
	house->setBidEndDate(timestamp);
	house->setState(CyclopediaHouseState::Transfer);

	owner->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(ret));
	playerCyclopediaHousesByTown(playerId, "");
}

void CyclopediaService::playerCyclopediaHouseCancelTransfer(uint32_t playerId, uint32_t houseId) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	const std::shared_ptr<Player> &player = game_.getPlayerByID(playerId);
	if (!player) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getState() != CyclopediaHouseState::Transfer) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelTransfer, enumToValue(TransferErrorMessage::Internal));
		return;
	}

	if (house->getOwner() != player->getGUID()) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelTransfer, enumToValue(TransferErrorMessage::NotHouseOwner));
		return;
	}

	if (house->getTransferStatus()) {
		const auto &newOwner = game_.getPlayerByGUID(house->getBidder());
		const auto amountPaid = house->getInternalBid() + house->getRent();
		if (newOwner) {
			newOwner->setBankBalance(newOwner->getBankBalance() + amountPaid);
			newOwner->sendResourceBalance(RESOURCE_BANK, newOwner->getBankBalance());
		} else {
			g_playerRepository().increaseBankBalance(house->getBidder(), amountPaid);
		}
	}

	house->setBidderName("");
	house->setBidder(0);
	house->setInternalBid(0);
	house->setBidEndDate(0);
	house->setState(CyclopediaHouseState::Rented);
	house->setTransferStatus(false);

	player->sendHouseAuctionMessage(houseId, HouseAuctionType::CancelTransfer, enumToValue(TransferErrorMessage::Success));
	playerCyclopediaHousesByTown(playerId, "");
}

void CyclopediaService::playerCyclopediaHouseAcceptTransfer(uint32_t playerId, uint32_t houseId) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	const std::shared_ptr<Player> &player = game_.getPlayerByID(playerId);
	if (!player) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getState() != CyclopediaHouseState::Transfer) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::AcceptTransfer, enumToValue(AcceptTransferErrorMessage::Internal));
		return;
	}

	auto ret = player->canAcceptTransferHouse(houseId);
	if (ret != AcceptTransferErrorMessage::Success) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::AcceptTransfer, enumToValue(ret));
		return;
	}

	if (!processBankAuction(player, house, house->getInternalBid())) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::AcceptTransfer, enumToValue(AcceptTransferErrorMessage::Frozen));
		return;
	}

	house->setTransferStatus(true);

	player->sendHouseAuctionMessage(houseId, HouseAuctionType::AcceptTransfer, enumToValue(ret));
	playerCyclopediaHousesByTown(playerId, "");
}

void CyclopediaService::playerCyclopediaHouseRejectTransfer(uint32_t playerId, uint32_t houseId) {
	if (!config_.getBoolean(CYCLOPEDIA_HOUSE_AUCTION)) {
		return;
	}

	const std::shared_ptr<Player> &player = game_.getPlayerByID(playerId);
	if (!player) {
		g_logger().warn("[{}] Player {} not found while handling house auction request.", __FUNCTION__, playerId);
		return;
	}

	const auto house = game_.map.houses.getHouseByClientId(houseId);
	if (!house || house->getBidder() != player->getGUID() || house->getState() != CyclopediaHouseState::Transfer) {
		player->sendHouseAuctionMessage(houseId, HouseAuctionType::Transfer, enumToValue(TransferErrorMessage::NotHouseOwner));
		return;
	}

	if (house->getTransferStatus()) {
		const auto &newOwner = game_.getPlayerByGUID(house->getBidder());
		const auto amountPaid = house->getInternalBid() + house->getRent();
		if (newOwner) {
			newOwner->setBankBalance(newOwner->getBankBalance() + amountPaid);
			newOwner->sendResourceBalance(RESOURCE_BANK, newOwner->getBankBalance());
		} else {
			g_playerRepository().increaseBankBalance(house->getBidder(), amountPaid);
		}
	}

	house->setBidderName("");
	house->setBidder(0);
	house->setInternalBid(0);
	house->setBidEndDate(0);
	house->setState(CyclopediaHouseState::Rented);
	house->setTransferStatus(false);

	player->sendHouseAuctionMessage(houseId, HouseAuctionType::RejectTransfer, enumToValue(TransferErrorMessage::Success));
	playerCyclopediaHousesByTown(playerId, "");
}

bool CyclopediaService::processBankAuction(std::shared_ptr<Player> player, const std::shared_ptr<House> &house, uint64_t bid, bool replace) {
	if (!replace && player->getBankBalance() < (house->getRent() + bid)) {
		return false;
	}

	if (player->getBankBalance() < bid) {
		return false;
	}

	uint64_t balance = player->getBankBalance();
	if (replace) {
		player->setBankBalance(balance - (bid - house->getInternalBid()));
	} else {
		player->setBankBalance(balance - (house->getRent() + bid));
	}

	player->sendResourceBalance(RESOURCE_BANK, player->getBankBalance());

	if (house->getBidderName() != player->getName()) {
		const auto otherPlayer = game_.getPlayerByName(house->getBidderName());
		if (!otherPlayer) {
			uint32_t bidderGuid = g_playerRepository().getGuidByName(house->getBidderName());
			g_playerRepository().increaseBankBalance(bidderGuid, (house->getBidHolderLimit() + house->getRent()));
		} else {
			otherPlayer->setBankBalance(otherPlayer->getBankBalance() + (house->getBidHolderLimit() + house->getRent()));
			otherPlayer->sendResourceBalance(RESOURCE_BANK, otherPlayer->getBankBalance());
		}
	}

	return true;
}
