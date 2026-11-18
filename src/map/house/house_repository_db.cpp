/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "map/house/house_repository_db.hpp"

#include "database/database.hpp"
#include "lib/di/container.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <fmt/format.h>
	#include <magic_enum/magic_enum.hpp>
#endif

IHouseRepository &IHouseRepository::getInstance() {
	return DI::port<IHouseRepository>();
}

HouseRepositoryDB::HouseRepositoryDB(IDatabase &db) :
	db(db) { }

std::vector<StoredHouse> HouseRepositoryDB::loadHouses() {
	std::vector<StoredHouse> houses;
	const auto result = db.storeQuery("SELECT `id`, `owner`, `new_owner`, `bidder`, `bidder_name`, `highest_bid`, `internal_bid`, `bid_end_date`, `state`, `transfer_status` FROM `houses`");
	if (!result) {
		return houses;
	}
	do {
		StoredHouse house;
		house.id = result->getNumber<uint32_t>("id");
		house.owner = result->getNumber<uint32_t>("owner");
		house.newOwner = result->getNumber<int32_t>("new_owner");
		house.bidder = result->getNumber<uint32_t>("bidder");
		house.bidderName = result->getString("bidder_name");
		house.highestBid = result->getNumber<uint32_t>("highest_bid");
		house.internalBid = result->getNumber<uint32_t>("internal_bid");
		house.bidEndDate = result->getNumber<uint32_t>("bid_end_date");
		house.state = static_cast<CyclopediaHouseState>(result->getNumber<uint16_t>("state"));
		house.transferStatus = result->getNumber<bool>("transfer_status");
		houses.push_back(std::move(house));
	} while (result->next());
	return houses;
}

std::vector<HouseAccessListRow> HouseRepositoryDB::loadAccessLists() {
	std::vector<HouseAccessListRow> lists;
	const auto result = db.storeQuery("SELECT `house_id`, `listid`, `list` FROM `house_lists`");
	if (!result) {
		return lists;
	}
	do {
		lists.push_back({ result->getNumber<uint32_t>("house_id"), result->getNumber<uint32_t>("listid"), result->getString("list") });
	} while (result->next());
	return lists;
}

bool HouseRepositoryDB::saveHouses(const std::vector<HouseRow> &houses, const std::vector<HouseAccessListRow> &lists) {
	return DBTransaction::executeWithinTransaction([this, &houses, &lists] {
		DBInsert houseUpdate("INSERT INTO `houses` (`id`, `owner`, `paid`, `warnings`, `name`, `town_id`, `rent`, `size`, `beds`, `bidder`, `bidder_name`, `highest_bid`, `internal_bid`, `bid_end_date`, `state`, `transfer_status`) VALUES ");
		houseUpdate.upsert({ "owner", "paid", "warnings", "name", "town_id", "rent", "size", "beds", "bidder", "bidder_name", "highest_bid", "internal_bid", "bid_end_date", "state", "transfer_status" });
		for (const auto &house : houses) {
			const auto values = fmt::format(
				"{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
				house.id, house.owner, house.paidUntil, house.rentWarnings, db.escapeString(house.name), house.townId, house.rent, house.size, house.beds,
				house.bidder, db.escapeString(house.bidderName), house.highestBid, house.internalBid, house.bidEndDate, magic_enum::enum_integer(house.state), house.transferStatus ? 1 : 0
			);
			if (!houseUpdate.addRow(values)) {
				return false;
			}
		}
		if (!houseUpdate.execute()) {
			return false;
		}

		// Lists are written with a version and the older ones removed after, so a list emptied since the
		// last save disappears.
		DBInsert listUpdate("INSERT INTO `house_lists` (`house_id` , `listid` , `list`, `version`) VALUES ");
		listUpdate.upsert({ "list", "version" });
		const auto version = getTimeUsNow();
		for (const auto &list : lists) {
			if (list.list.empty()) {
				continue;
			}
			if (!listUpdate.addRow(fmt::format("{},{},{},{}", list.houseId, list.listId, db.escapeString(list.list), version))) {
				return false;
			}
		}
		if (!listUpdate.execute()) {
			return false;
		}
		return db.executeQuery(fmt::format("DELETE FROM `house_lists` WHERE `version` < {}", version));
	});
}

void HouseRepositoryDB::storeOwner(uint32_t houseId, uint32_t ownerGuid) {
	std::ignore = db.executeQuery(fmt::format(
		"UPDATE `houses` SET `owner` = {}, `new_owner` = -1, `paid` = 0, `bidder` = 0, `bidder_name` = '', `highest_bid` = 0, `internal_bid` = 0, `bid_end_date` = 0, `state` = {} WHERE `id` = {}",
		ownerGuid, ownerGuid > 0 ? 2 : 0, houseId
	));
}

void HouseRepositoryDB::storeNewOwner(uint32_t houseId, int32_t newOwnerGuid) {
	std::ignore = db.executeQuery(fmt::format("UPDATE `houses` SET `new_owner` = {} WHERE `id` = {}", newOwnerGuid, houseId));
}

std::optional<HouseOwnerIdentity> HouseRepositoryDB::findOwnerIdentity(uint32_t playerGuid) {
	const auto result = db.storeQuery(fmt::format("SELECT `name`, `account_id` FROM `players` WHERE `id` = {}", playerGuid));
	if (!result) {
		return std::nullopt;
	}
	return HouseOwnerIdentity { result->getString("name"), result->getNumber<uint32_t>("account_id") };
}
