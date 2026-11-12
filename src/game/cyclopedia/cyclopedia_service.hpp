/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "utils/request_cooldown.hpp"
#include "utils/tools.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>

class Game;
class IConfigManager;
class House;
class Player;
enum CyclopediaCharacterInfoType_t : uint8_t;

class CyclopediaService {
public:
	CyclopediaService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerCyclopediaCharacterInfo(const std::shared_ptr<Player> &player, uint32_t characterID, CyclopediaCharacterInfoType_t characterInfoType, uint16_t entriesPerPage, uint16_t page);
	void updatePlayersOnline() const;
	void playerCyclopediaHousesByTown(uint32_t playerId, const std::string &townName);
	void playerCyclopediaHouseBid(uint32_t playerId, uint32_t houseId, uint64_t bidValue);
	void playerCyclopediaHouseMoveOut(uint32_t playerId, uint32_t houseId, uint32_t timestamp);
	void playerCyclopediaHouseCancelMoveOut(uint32_t playerId, uint32_t houseId);
	void playerCyclopediaHouseTransfer(uint32_t playerId, uint32_t houseId, uint32_t timestamp, const std::string &newOwnerName, uint64_t bidValue);
	void playerCyclopediaHouseCancelTransfer(uint32_t playerId, uint32_t houseId);
	void playerCyclopediaHouseAcceptTransfer(uint32_t playerId, uint32_t houseId);
	void playerCyclopediaHouseRejectTransfer(uint32_t playerId, uint32_t houseId);
	bool processBankAuction(std::shared_ptr<Player> player, const std::shared_ptr<House> &house, uint64_t bid, bool replace = false);

	/// A transfer request names its recipient, and an offline recipient is loaded whole from the
	/// database on the dispatcher, so each player gets one request per interval.
	static constexpr int64_t HouseTransferCooldownMs = 1000;

	/// Bids and transfer prices are stored in `houses` int(11) columns (and read back as 32-bit),
	/// so larger values would be cut on save and refunds would go wrong.
	static constexpr uint64_t MaxHouseBid = std::numeric_limits<int32_t>::max();

	/// Whether any character of this player's account is the bidder of another house on auction.
	/// Derived from the houses in memory: `accounts.house_bid_id` is never written upstream.
	[[nodiscard]] bool bidsOnAnotherHouse(const std::shared_ptr<Player> &player, const std::shared_ptr<House> &except) const;

	[[nodiscard]] size_t throttledHouseTransfers() const {
		return throttledHouseTransfers_;
	}

	/// Clock in milliseconds; tests replace it.
	std::function<int64_t()> now = [] { return OTSYS_TIME(); };

private:
	Game &game_;
	IConfigManager &config_;
	RequestCooldown houseTransferCooldown { HouseTransferCooldownMs };
	size_t throttledHouseTransfers_ = 0;
};
