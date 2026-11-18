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

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <ctime>
	#include <optional>
	#include <string>
	#include <vector>
#endif

/// The auction and ownership state of a house as stored (`houses`), read at startup.
struct StoredHouse {
	uint32_t id = 0;
	uint32_t owner = 0;
	/// A pending owner change on restart (-1: none, 0: remove the owner).
	int32_t newOwner = -1;
	uint32_t bidder = 0;
	std::string bidderName;
	uint64_t highestBid = 0;
	uint64_t internalBid = 0;
	uint32_t bidEndDate = 0;
	CyclopediaHouseState state = CyclopediaHouseState::Available;
	bool transferStatus = false;
};

/// What the server save writes for a house (`houses`).
struct HouseRow {
	uint32_t id = 0;
	uint32_t owner = 0;
	time_t paidUntil = 0;
	uint32_t rentWarnings = 0;
	std::string name;
	uint32_t townId = 0;
	uint32_t rent = 0;
	uint32_t size = 0;
	uint32_t beds = 0;
	int32_t bidder = 0;
	std::string bidderName;
	uint64_t highestBid = 0;
	uint64_t internalBid = 0;
	uint32_t bidEndDate = 0;
	CyclopediaHouseState state = CyclopediaHouseState::Available;
	bool transferStatus = false;
};

/// A guest, subowner or door list (`house_lists`; listId is GUEST_LIST, SUBOWNER_LIST or a door id).
struct HouseAccessListRow {
	uint32_t houseId = 0;
	uint32_t listId = 0;
	std::string list;
};

/// The items of one house tile as IOMapSerialize serializes them (position, then the items).
struct HouseTileItems {
	uint32_t houseId = 0;
	std::string data;
};

struct HouseOwnerIdentity {
	std::string name;
	uint32_t accountId = 0;
};

/// Persistence of houses (driven port): their ownership and auction state, access lists, and the
/// owner a house is given, and the items in houses as serialized blobs (IOMapSerialize owns their
/// format). House and the startup load and server save use it; tests use an in-memory fake
/// (tests/fixture/map/in_memory_house_repository.hpp).
class IHouseRepository {
public:
	IHouseRepository() = default;
	virtual ~IHouseRepository() = default;
	IHouseRepository(const IHouseRepository &) = delete;
	void operator=(const IHouseRepository &) = delete;

	static IHouseRepository &getInstance();

	/// Every stored house (empty when there are none or the read failed).
	[[nodiscard]] virtual std::vector<StoredHouse> loadHouses() = 0;
	[[nodiscard]] virtual std::vector<HouseAccessListRow> loadAccessLists() = 0;
	/// Writes every house and replaces the access lists, in one transaction.
	[[nodiscard]] virtual bool saveHouses(const std::vector<HouseRow> &houses, const std::vector<HouseAccessListRow> &lists) = 0;
	/// Every stored house tile with items.
	[[nodiscard]] virtual std::vector<HouseTileItems> loadTileItems() = 0;
	/// Replaces every stored house tile with `tiles`, in one transaction.
	[[nodiscard]] virtual bool saveTileItems(const std::vector<HouseTileItems> &tiles) = 0;

	/// Gives `houseId` to `ownerGuid` (0: nobody) and clears its auction and pending transfer.
	virtual void storeOwner(uint32_t houseId, uint32_t ownerGuid) = 0;
	/// Records an owner change applied on the next restart (TOGGLE_HOUSE_TRANSFER_ON_SERVER_RESTART).
	virtual void storeNewOwner(uint32_t houseId, int32_t newOwnerGuid) = 0;
	/// Name and account of the character that would own a house; nothing when it does not exist.
	[[nodiscard]] virtual std::optional<HouseOwnerIdentity> findOwnerIdentity(uint32_t playerGuid) = 0;
};

constexpr auto g_houseRepository = IHouseRepository::getInstance;
