/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "map/house/house_repository.hpp"

class IDatabase;

/// IHouseRepository on the database (`houses`, `house_lists`, the owner's row in `players`): the SQL
/// that was in House::setOwner/setNewOwnerGuid and IOMapSerialize::loadHouseInfo/saveHouseInfo.
class HouseRepositoryDB final : public IHouseRepository {
public:
	explicit HouseRepositoryDB(IDatabase &db);

	[[nodiscard]] std::vector<StoredHouse> loadHouses() override;
	[[nodiscard]] std::vector<HouseAccessListRow> loadAccessLists() override;
	[[nodiscard]] bool saveHouses(const std::vector<HouseRow> &houses, const std::vector<HouseAccessListRow> &lists) override;
	void storeOwner(uint32_t houseId, uint32_t ownerGuid) override;
	void storeNewOwner(uint32_t houseId, int32_t newOwnerGuid) override;
	[[nodiscard]] std::optional<HouseOwnerIdentity> findOwnerIdentity(uint32_t playerGuid) override;

private:
	IDatabase &db;
};
