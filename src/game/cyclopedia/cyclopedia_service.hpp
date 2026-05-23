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

private:
	Game &game_;
	IConfigManager &config_;
};
