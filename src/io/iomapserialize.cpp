/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/iomapserialize.hpp"
#include "map/house/house_auction_rules.hpp"
#include "map/house/house_repository.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/player_repository.hpp"
#include "io/iologindata.hpp"
#include "game/game.hpp"
#include "items/bed.hpp"

void IOMapSerialize::loadHouseItems(Map* map) {
	Benchmark bm_context;

	// The blobs outlive the loop: PropStream reads them in place.
	const auto storedTiles = g_houseRepository().loadTileItems();
	for (const auto &stored : storedTiles) {
		PropStream propStream;
		propStream.init(stored.data.data(), stored.data.size());

		uint16_t x, y;
		uint8_t z;
		if (!propStream.read<uint16_t>(x) || !propStream.read<uint16_t>(y) || !propStream.read<uint8_t>(z)) {
			continue;
		}

		std::shared_ptr<Tile> tile = map->getTile(x, y, z);
		if (!tile) {
			continue;
		}

		uint32_t item_count;
		if (!propStream.read<uint32_t>(item_count)) {
			continue;
		}

		while (item_count--) {
			if (auto houseTile = std::dynamic_pointer_cast<HouseTile>(tile)) {
				const auto &house = houseTile->getHouse();
				auto isTransferOnRestart = g_configManager().getBoolean(TOGGLE_HOUSE_TRANSFER_ON_SERVER_RESTART);
				if (!isTransferOnRestart && house->getOwner() == 0) {
					g_logger().trace("Skipping load item from house id: {}, position: {}, house does not have owner", house->getId(), house->getEntryPosition().toString());
					house->clearHouseInfo(false);
					continue;
				}
			}

			loadItem(propStream, tile, true);
		}
	}

	for (const auto &bed : bedsToCheck) {
		const auto &next = bed->getNextBedItem();
		const bool isComplete = next && (bed->isBedComplete(next) || next->isBedComplete(bed));
		if (!isComplete) {
			const Position &pos = bed->getPosition();
			g_logger().warn("[BedFix] House bug detected at {}, correcting bed.", pos);

			bed->forceClearSleeper();
			if (next && next->getNextBedItem() == bed) {
				next->forceClearSleeper();
			}
		}
	}

	bedsToCheck.clear();

	g_logger().info("Loaded house items in {} milliseconds", bm_context.duration());
}

bool IOMapSerialize::saveHouseItems() {
	std::vector<HouseTileItems> tiles;
	PropWriteStream stream;
	for (const auto &[key, house] : g_game().map.houses.getHouses()) {
		for (const auto &tile : house->getTiles()) {
			saveTile(stream, tile);

			size_t attributesSize;
			const char* attributes = stream.getStream(attributesSize);
			if (attributesSize > 0) {
				tiles.push_back({ house->getId(), std::string(attributes, attributesSize) });
				stream.clear();
			}
		}
	}

	const bool success = g_houseRepository().saveTileItems(tiles);
	if (!success) {
		g_logger().error("[{}] Error occurred saving houses", __FUNCTION__);
	}
	return success;
}

bool IOMapSerialize::loadContainer(PropStream &propStream, const std::shared_ptr<Container> &container) {
	while (container->serializationCount > 0) {
		if (!loadItem(propStream, container)) {
			g_logger().warn("Deserialization error for container item: {}", container->getID());
			return false;
		}
		container->serializationCount--;
	}

	uint8_t endAttr;
	if (!propStream.read<uint8_t>(endAttr) || endAttr != 0) {
		g_logger().warn("Deserialization error for container item: {}", container->getID());
		return false;
	}
	return true;
}

bool IOMapSerialize::loadItem(PropStream &propStream, const std::shared_ptr<Cylinder> &parent, bool isHouseItem /*= false*/) {
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return false;
	}

	std::shared_ptr<Tile> tile = nullptr;
	if (parent->getParent() == nullptr) {
		tile = parent->getTile();
	}

	const ItemType &iType = Item::items[id];
	if (iType.isBed() || iType.movable || !tile || iType.isCarpet() || iType.isTrashHolder()) {
		// create a new item
		auto item = Item::CreateItem(id);
		if (item) {
			if (item->unserializeAttr(propStream)) {
				// Remove only not movable and not sleeper bed
				auto bed = item->getBed();
				if (isHouseItem && iType.isBed() && bed && bed->getSleeper() == 0 && !iType.movable) {
					return false;
				}
				std::shared_ptr<Container> container = item->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				parent->internalAddThing(item);
				item->startDecaying();
			} else {
				g_logger().warn("Deserialization error in {}", id);

				return false;
			}

			if (const auto &bed = item->getBed()) {
				if (bed->getSleeper() != 0) {
					bedsToCheck.push_back(bed);
				}
			}
		}
	} else {
		// Stationary items like doors/beds/blackboards/bookcases
		std::shared_ptr<Item> item = nullptr;
		if (const TileItemVector* items = tile->getItemList()) {
			for (auto &findItem : *items) {
				if (findItem->getID() == id) {
					item = findItem;
					break;
				} else if (iType.m_transformOnUse && findItem->getID() == iType.m_transformOnUse) {
					item = findItem;
					break;
				} else if (iType.isDoor() && findItem->getDoor()) {
					item = findItem;
					break;
				} else if (iType.isBed() && findItem->getBed()) {
					item = findItem;
					break;
				}
			}
		}

		if (item) {
			if (item->unserializeAttr(propStream)) {
				std::shared_ptr<Container> container = item->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				g_game().transformItem(item, id);
			} else {
				g_logger().warn("Deserialization error in {}", id);
			}
		} else {
			// The map changed since the last save, just read the attributes
			auto dummy = Item::CreateItem(id);
			if (dummy) {
				dummy->unserializeAttr(propStream);
				std::shared_ptr<Container> container = dummy->getContainer();
				if (container) {
					if (!loadContainer(propStream, container)) {
						return false;
					}
				} else if (std::shared_ptr<BedItem> bedItem = std::dynamic_pointer_cast<BedItem>(dummy)) {
					uint32_t sleeperGUID = bedItem->getSleeper();
					if (sleeperGUID != 0) {
						g_game().removeBedSleeper(sleeperGUID);
					}
				}
			}
		}
	}
	return true;
}

void IOMapSerialize::saveItem(PropWriteStream &stream, const std::shared_ptr<Item> &item) {
	const auto &container = item->getContainer();

	// Write ID & props
	stream.write<uint16_t>(item->getID());
	item->serializeAttr(stream);

	if (container) {
		// Hack our way into the attributes
		stream.write<uint8_t>(ATTR_CONTAINER_ITEMS);
		stream.write<uint32_t>(container->size());
		for (auto it = container->getReversedItems(), end = container->getReversedEnd(); it != end; ++it) {
			saveItem(stream, *it);
		}
	}

	stream.write<uint8_t>(0x00); // attr end
}

void IOMapSerialize::saveTile(PropWriteStream &stream, const std::shared_ptr<Tile> &tile) {
	const TileItemVector* tileItems = tile->getItemList();
	if (!tileItems) {
		return;
	}

	std::list<std::shared_ptr<Item>> items;
	uint16_t count = 0;
	for (auto &item : *tileItems) {
		if (item->getID() == ITEM_BATHTUB_FILLED_NOTMOVABLE) {
			std::shared_ptr<Item> tub = Item::CreateItem(ITEM_BATHTUB_FILLED);
			items.push_front(tub);
			++count;
			continue;
		} else if (!item->isSavedToHouses()) {
			continue;
		}

		items.push_front(item);
		++count;
	}

	if (!items.empty()) {
		const Position &tilePosition = tile->getPosition();
		stream.write<uint16_t>(tilePosition.x);
		stream.write<uint16_t>(tilePosition.y);
		stream.write<uint8_t>(tilePosition.z);

		stream.write<uint32_t>(count);
		for (const std::shared_ptr<Item> &item : items) {
			saveItem(stream, item);
		}
	}
}

bool IOMapSerialize::loadHouseInfo() {
	auto &repository = g_houseRepository();
	const auto storedHouses = repository.loadHouses();
	if (storedHouses.empty()) {
		return false;
	}

	const auto timeNow = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	const bool transferOnRestart = g_configManager().getBoolean(TOGGLE_HOUSE_TRANSFER_ON_SERVER_RESTART);
	for (const auto &stored : storedHouses) {
		const auto house = g_game().map.houses.getHouse(stored.id);
		if (!house) {
			continue;
		}

		// The auction, transfer and move-out decisions are house_auction_rules::resolveAtStartup.
		const auto resolution = house_auction_rules::resolveAtStartup(stored, timeNow, transferOnRestart);
		if (resolution.moveItemsOf) {
			g_game().setTransferPlayerHouseItems(stored.id, *resolution.moveItemsOf);
		}
		if (resolution.newOwner) {
			g_logger().debug("[{}] house id '{}': owner GUID '{}' -> '{}'", resolution.reason, stored.id, stored.owner, *resolution.newOwner);
			house->setOwner(*resolution.newOwner);
		} else {
			house->setOwner(stored.owner, false);
			house->setState(stored.state);
		}
		for (const auto &[guid, amount] : resolution.credits) {
			g_playerRepository().increaseBankBalance(guid, amount);
		}
		house->setBidder(resolution.bidder);
		house->setBidderName(resolution.bidderName);
		house->setHighestBid(resolution.highestBid);
		house->setInternalBid(resolution.internalBid);
		house->setBidHolderLimit(resolution.internalBid);
		house->setBidEndDate(resolution.bidEndDate);
		house->setTransferStatus(resolution.transferStatus);
	}

	for (const auto &list : repository.loadAccessLists()) {
		if (const auto &house = g_game().map.houses.getHouse(list.houseId)) {
			house->setAccessList(list.listId, list.list);
		}
	}
	return true;
}

bool IOMapSerialize::saveHouseInfo() {
	std::vector<HouseRow> houses;
	std::vector<HouseAccessListRow> lists;
	for (const auto &[key, house] : g_game().map.houses.getHouses()) {
		houses.push_back(HouseRow {
			.id = house->getId(),
			.owner = house->getOwner(),
			.paidUntil = house->getPaidUntil(),
			.rentWarnings = house->getPayRentWarnings(),
			.name = house->getName(),
			.townId = house->getTownId(),
			.rent = house->getRent(),
			.size = house->getSize(),
			.beds = house->getBedCount(),
			.bidder = house->getBidder(),
			.bidderName = house->getBidderName(),
			.highestBid = house->getHighestBid(),
			.internalBid = house->getInternalBid(),
			.bidEndDate = house->getBidEndDate(),
			.state = house->getState(),
			.transferStatus = house->getTransferStatus(),
		});

		std::string listText;
		for (const uint32_t listId : { static_cast<uint32_t>(GUEST_LIST), static_cast<uint32_t>(SUBOWNER_LIST) }) {
			if (house->getAccessList(listId, listText) && !listText.empty()) {
				lists.push_back({ house->getId(), listId, listText });
			}
			listText.clear();
		}
		for (const std::shared_ptr<Door> &door : house->getDoors()) {
			if (door->getAccessList(listText) && !listText.empty()) {
				lists.push_back({ house->getId(), door->getDoorId(), listText });
			}
			listText.clear();
		}
	}

	const bool success = g_houseRepository().saveHouses(houses, lists);
	if (!success) {
		g_logger().error("[{}] Error occurred saving houses info", __FUNCTION__);
	}
	return success;
}
