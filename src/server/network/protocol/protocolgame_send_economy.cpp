/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// ProtocolGame send methods: Market, store, forge/wheel, prey/task hunting and imbuement packets.
// Split from protocolgame_send.cpp; shares protocolgame_helpers.hpp.

#include "server/network/protocol/protocolgame.hpp"
#include "server/network/commands/game_command_dispatcher.hpp"
#include "account/account.hpp"
#include "config/configmanager.hpp"
#include "core.hpp"
#include "creatures/appearance/mounts/mounts.hpp"
#include "creatures/appearance/attached_effects/attached_effects.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/combat/spells.hpp"
#include "creatures/interactions/chat.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/grouping/familiars.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/grouping/team_finder.hpp"
#include "creatures/players/highscore_category.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/management/ban.hpp"
#include "creatures/players/management/waitlist.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/components/player_forge_history.hpp"
#include "enums/player_icons.hpp"
#include "game/game.hpp"
#include "game/modal_window/modal_window.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "io/functions/iologindata_load_player.hpp"
#include "io/io_bosstiary.hpp"
#include "io/iobestiary.hpp"
#include "creatures/players/player_repository.hpp"
#include "io/market_repository.hpp"
#include "io/iologindata.hpp"
#include "io/iomarket.hpp"
#include "io/ioguild.hpp"
#include "io/ioprey.hpp"
#include "items/items_classification.hpp"
#include "items/weapons/weapons.hpp"
#include "lua/creature/creatureevent.hpp"
#include "lua/modules/modules.hpp"
#include "server/network/message/outputmessage.hpp"
#include "utils/tools.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "enums/account_coins.hpp"
#include "enums/account_group_type.hpp"
#include "enums/account_type.hpp"
#include "enums/object_category.hpp"
#include "enums/player_blessings.hpp"
#include "enums/player_cyclopedia.hpp"
#include "enums/container_type.hpp"
#include "enums/imbuement.hpp"
#include "creatures/players/components/pvp/expert_pvp.hpp"
#include "enums/player_wheel.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "server/network/connection/connection.hpp"
#include "server/network/protocol/protocol_port_utils.hpp"
#include "server/network/protocol/transport_codec.hpp"
#include "server/network/protocol/protocolgame_helpers.hpp"

void ProtocolGame::sendMarketEnter(uint32_t depotId) {
	if (!player) {
		return;
	}

	if (!hasProtocolFeature(protocolProfile, ProtocolFeature::MarketPackets)) {
		player->sendCancelMessage("The market is not available on this client version.");
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xF6);

	if (oldProtocol) {
		msg.add<uint64_t>(player->getBankBalance());
	}

	msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(g_marketRepository().getPlayerOfferCount(player->getGUID()), std::numeric_limits<uint8_t>::max())));

	std::shared_ptr<DepotLocker> depotLocker = player->getDepotLocker(depotId);
	if (!depotLocker) {
		msg.add<uint16_t>(0x00);
		writeToOutputBuffer(msg);
		return;
	}

	player->setInMarket(true);

	// Only use here locker items, itemVector is for use of Game::createMarketOffer
	auto [itemVector, lockerItems] = player->requestLockerItems(depotLocker, true);
	auto totalItemsCountPosition = msg.getBufferPosition();
	msg.skipBytes(2); // Total items count

	const uint16_t entriesLimit = std::numeric_limits<uint16_t>::max();
	uint16_t entriesSent = 0;
	bool limitReached = false;
	for (const auto &[itemId, tierAndCountMap] : lockerItems) {
		for (const auto &[tier, count] : tierAndCountMap) {
			if (entriesSent >= entriesLimit) {
				limitReached = true;
				break;
			}
			msg.add<uint16_t>(itemId);
			if (!oldProtocol && Item::items[itemId].upgradeClassification > 0) {
				msg.addByte(tier);
			}
			msg.add<uint16_t>(static_cast<uint16_t>(count));
			++entriesSent;
		}
		if (limitReached) {
			break;
		}
	}

	auto endPosition = msg.getBufferPosition();
	msg.setBufferPosition(totalItemsCountPosition);
	msg.add<uint16_t>(entriesSent);
	msg.setBufferPosition(endPosition);

	writeToOutputBuffer(msg);

	updateCoinBalance();
	sendResourcesBalance(player->getMoney(), player->getBankBalance(), player->getPreyCards(), player->getTaskHuntingPoints());
}

void ProtocolGame::sendCoinBalance() {
	if (!player) {
		return;
	}

	// send is updating
	// TODO: export this to it own function
	NetworkMessage msg;
	msg.addByte(0xF2);
	msg.addByte(0x01);
	writeToOutputBuffer(msg);

	msg.reset();

	// send update
	msg.addByte(0xDF);
	msg.addByte(0x01);

	msg.add<uint32_t>(player->coinBalance); // Normal Coins
	msg.add<uint32_t>(player->coinTransferableBalance); // Transferable Coins

	if (!oldProtocol) {
		msg.add<uint32_t>(player->coinBalance); // Reserved Auction Coins
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketLeave() {
	if (!hasProtocolFeature(protocolProfile, ProtocolFeature::MarketPackets)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xF7);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseItem(uint16_t itemId, const MarketOfferList &buyOffers, const MarketOfferList &sellOffers, uint8_t tier) {
	NetworkMessage msg;

	msg.addByte(0xF9);
	if (!oldProtocol) {
		msg.addByte(MARKETREQUEST_ITEM_BROWSE);
	}

	msg.add<uint16_t>(itemId);
	if (!oldProtocol && Item::items[itemId].upgradeClassification > 0) {
		msg.addByte(tier);
	}

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer &offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer &offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	updateCoinBalance();
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketAcceptOffer(const MarketOfferEx &offer) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (!oldProtocol) {
		msg.addByte(MARKETREQUEST_ITEM_BROWSE);
	}

	msg.add<uint16_t>(offer.itemId);
	if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
		msg.addByte(offer.tier);
	}

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnOffers(const MarketOfferList &buyOffers, const MarketOfferList &sellOffers) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_OFFERS);
	}

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer &offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer &offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketCancelOffer(const MarketOfferEx &offer) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_OFFERS);
	}

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnHistory(const HistoryMarketOfferList &buyOffers, const HistoryMarketOfferList &sellOffers) {
	uint32_t i = 0;
	std::map<uint32_t, uint16_t> counterMap;
	uint32_t buyOffersToSend = std::min<uint32_t>(buyOffers.size(), 810 + std::max<int32_t>(0, 810 - sellOffers.size()));
	uint32_t sellOffersToSend = std::min<uint32_t>(sellOffers.size(), 810 + std::max<int32_t>(0, 810 - buyOffers.size()));

	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_HISTORY_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_HISTORY);
	}

	msg.add<uint32_t>(buyOffersToSend);
	for (auto it = buyOffers.begin(); i < buyOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.add<uint16_t>(it->itemId);
		if (!oldProtocol && Item::items[it->itemId].upgradeClassification > 0) {
			msg.addByte(it->tier);
		}
		msg.add<uint16_t>(it->amount);
		if (oldProtocol) {
			msg.add<uint32_t>(it->price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(it->price));
		}
		msg.addByte(it->state);
	}

	counterMap.clear();
	i = 0;

	msg.add<uint32_t>(sellOffersToSend);
	for (auto it = sellOffers.begin(); i < sellOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.add<uint16_t>(it->itemId);
		if (Item::items[it->itemId].upgradeClassification > 0) {
			msg.addByte(it->tier);
		}
		msg.add<uint16_t>(it->amount);
		msg.add<uint64_t>(it->price);
		msg.addByte(it->state);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenForge() {
	// We will use it when sending the bytes to send the item information to the client
	std::map<uint16_t, std::map<uint8_t, uint16_t>> fusionItemsMap;
	std::map<int32_t, std::map<uint16_t, std::map<uint8_t, uint16_t>>> convergenceFusionItemsMap;
	std::map<int32_t, std::map<uint16_t, std::map<uint8_t, uint16_t>>> convergenceTransferItemsMap;
	std::map<uint16_t, std::map<uint8_t, uint16_t>> donorTierItemMap;
	std::map<uint16_t, std::map<uint8_t, uint16_t>> receiveTierItemMap;

	auto maxConfigTier = g_configManager().getNumber(FORGE_MAX_ITEM_TIER);

	/*
	 *Start - Parsing items informations
	 */
	for (const auto &item : player->getAllInventoryItems(true)) {
		if (item->hasImbuements()) {
			continue;
		}

		auto itemClassification = item->getClassification();
		auto itemTier = item->getTier();
		auto maxTier = (itemClassification == 4 ? maxConfigTier : itemClassification);
		// Save fusion items on map
		if (itemClassification != 0 && itemTier < maxTier) {
			getForgeInfoMap(item, fusionItemsMap);
		}

		if (itemClassification > 0) {
			if (itemClassification < 4 && itemTier > maxTier) {
				continue;
			}
			// Save transfer (donator of tier) items on map
			if (itemTier > 1) {
				getForgeInfoMap(item, donorTierItemMap);
			}
			// Save transfer (receiver of tier) items on map
			if (itemTier == 0) {
				getForgeInfoMap(item, receiveTierItemMap);
			}
			if (itemClassification == 4) {
				auto slotPosition = item->getSlotPosition();
				if ((slotPosition & SLOTP_TWO_HAND) != 0) {
					slotPosition = SLOTP_HAND;
				}
				getForgeInfoMap(item, convergenceFusionItemsMap[slotPosition]);
				getForgeInfoMap(item, convergenceTransferItemsMap[item->getClassification()]);
			}
		}
	}

	// Checking size of map to send in the addByte (total fusion items count)
	uint8_t fusionTotalItemsCount = 0;
	for (const auto &[itemId, tierAndCountMap] : fusionItemsMap) {
		for (const auto &[itemTier, itemCount] : tierAndCountMap) {
			if (itemCount >= 2) {
				fusionTotalItemsCount++;
			}
		}
	}

	/*
	 * Start - Sending bytes
	 */
	NetworkMessage msg;

	// Header byte (135)
	msg.addByte(0x87);

	msg.add<uint16_t>(fusionTotalItemsCount);
	for (const auto &[itemId, tierAndCountMap] : fusionItemsMap) {
		for (const auto &[itemTier, itemCount] : tierAndCountMap) {
			if (itemCount >= 2) {
				msg.addByte(0x01); // Number of friend items?
				msg.add<uint16_t>(itemId);
				msg.addByte(itemTier);
				msg.add<uint16_t>(itemCount);
			}
		}
	}

	// msg.add<uint16_t>(convergenceItemsMap.size());
	auto convergenceFusionCountPosition = msg.getBufferPosition();
	msg.skipBytes(2);
	uint16_t convergenceFusionCount = 0;
	/*
	for each convergence fusion (1 per item slot, only class 4):
	1 byte: count fusable items
	for each fusable item:
	    2 bytes: item id
	    1 byte: tier
	    2 bytes: count
	*/
	for (const auto &[slot, itemMap] : convergenceFusionItemsMap) {
		uint8_t totalItemsCount = 0;
		auto totalItemsCountPosition = msg.getBufferPosition();
		msg.skipBytes(1); // Total items count
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto &[tier, itemCount] : tierAndCountMap) {
				if (tier >= maxConfigTier) {
					continue;
				}
				totalItemsCount++;
				msg.add<uint16_t>(itemId);
				msg.addByte(tier);
				msg.add<uint16_t>(itemCount);
			}
		}
		auto endPosition = msg.getBufferPosition();
		msg.setBufferPosition(totalItemsCountPosition);
		if (totalItemsCount > 0) {
			msg.addByte(totalItemsCount);
			msg.setBufferPosition(endPosition);
			convergenceFusionCount++;
		}
	}

	auto transferTotalCountPosition = msg.getBufferPosition();
	msg.setBufferPosition(convergenceFusionCountPosition);
	msg.add<uint16_t>(convergenceFusionCount);
	msg.setBufferPosition(transferTotalCountPosition);

	auto transferTotalCount = donorTierItemMap.size();
	msg.addByte(transferTotalCount);
	for (const auto &[itemId, tierAndCountMap] : donorTierItemMap) {
		// Let's access the itemType to check the item's (donator of tier) classification level
		// Must be the same as the item that will receive the tier
		const ItemType &donorType = Item::items[itemId];

		// Total count of item (donator of tier)
		auto donorTierTotalItemsCount = tierAndCountMap.size();
		msg.add<uint16_t>(donorTierTotalItemsCount);
		for (const auto &[donorItemTier, donorItemCount] : tierAndCountMap) {
			msg.add<uint16_t>(itemId);
			msg.addByte(donorItemTier);
			msg.add<uint16_t>(donorItemCount);
		}

		uint16_t receiveTierTotalItemCount = 0;
		for (const auto &[iteratorItemId, unusedTierAndCountMap] : receiveTierItemMap) {
			// Let's access the itemType to check the item's (receiver of tier) classification level
			const ItemType &receiveType = Item::items[iteratorItemId];
			if (donorType.upgradeClassification == receiveType.upgradeClassification) {
				receiveTierTotalItemCount++;
			}
		}

		// Total count of item (receiver of tier)
		msg.add<uint16_t>(receiveTierTotalItemCount);
		if (receiveTierTotalItemCount == 0) {
			continue;
		}

		for (const auto &[receiveItemId, receiveTierAndCountMap] : receiveTierItemMap) {
			// Let's access the itemType to check the item's (receiver of tier) classification level
			const ItemType &receiveType = Item::items[receiveItemId];
			if (donorType.upgradeClassification != receiveType.upgradeClassification) {
				continue;
			}

			for (const auto &[receiveItemTier, receiveItemCount] : receiveTierAndCountMap) {
				msg.add<uint16_t>(receiveItemId);
				msg.add<uint16_t>(receiveItemCount);
			}
		}
	}

	auto convergenceCountPosition = msg.getBufferPosition();
	msg.skipBytes(1);
	uint8_t convergenceTransferCount = 0;

	/*
	for each convergence transfer:
	    2 bytes: count donors
	    for each donor:
	        2 bytes: item id
	        1 byte: tier
	        2 bytes: count
	    2 bytes: count receivers
	    for each receiver:
	        2 bytes: item id
	        2 bytes: count
	*/
	for (const auto &[slot, itemMap] : convergenceTransferItemsMap) {
		uint16_t donorCount = 0;
		uint16_t receiverCount = 0;
		auto donorCountPosition = msg.getBufferPosition();
		msg.skipBytes(2); // Donor count
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto [tier, itemCount] : tierAndCountMap) {
				if (tier >= 1) {
					donorCount++;
					msg.add<uint16_t>(itemId);
					msg.addByte(tier);
					msg.add<uint16_t>(itemCount);
				} else {
					receiverCount++;
				}
			}
		}
		if (donorCount == 0 && receiverCount == 0) {
			msg.setBufferPosition(donorCountPosition);
			continue;
		}
		auto receiverCountPosition = msg.getBufferPosition();
		msg.setBufferPosition(donorCountPosition);
		msg.add<uint16_t>(donorCount);
		++convergenceTransferCount;
		msg.setBufferPosition(receiverCountPosition);
		msg.add<uint16_t>(receiverCount);
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto [tier, itemCount] : tierAndCountMap) {
				if (tier == 0) {
					msg.add<uint16_t>(itemId);
					msg.add<uint16_t>(itemCount);
				}
			}
		}
	}
	auto dustLevelPosition = msg.getBufferPosition();
	msg.setBufferPosition(convergenceCountPosition);
	msg.addByte(convergenceTransferCount);
	msg.setBufferPosition(dustLevelPosition);

	msg.add<uint16_t>(player->getForgeDustLevel()); // Player dust limit
	writeToOutputBuffer(msg);
	// Update forging informations
	sendForgingData();
}

void ProtocolGame::sendForgeResult(ForgeAction_t actionType, uint16_t leftItemId, uint8_t leftTier, uint16_t rightItemId, uint8_t rightTier, bool success, uint8_t bonus, uint8_t coreCount, bool convergence) {
	NetworkMessage msg;
	msg.addByte(0x8A);

	// 0 = fusion | 1 = transfer
	msg.addByte(static_cast<uint8_t>(actionType));
	msg.addByte(convergence);

	if (convergence && actionType == ForgeAction_t::FUSION) {
		success = true;
		std::swap(leftItemId, rightItemId);
	}

	msg.addByte(success);

	msg.add<uint16_t>(leftItemId);
	msg.addByte(leftTier);
	msg.add<uint16_t>(rightItemId);
	msg.addByte(rightTier);

	if (actionType == ForgeAction_t::TRANSFER) {
		msg.addByte(0x00); // Bonus type always none for transfer
	} else {
		msg.addByte(bonus); // Roll fusion bonus
		// Core kept
		if (bonus == 2) {
			msg.addByte(coreCount);
		} else if (bonus >= 4 && bonus <= 8) {
			msg.add<uint16_t>(leftItemId);
			msg.addByte(leftTier);
		}
	}

	writeToOutputBuffer(msg);
	g_logger().debug("Send forge fusion: type {}, left item {}, left tier {}, right item {}, rightTier {}, success {}, bonus {}, coreCount {}, convergence {}", fmt::underlying(actionType), leftItemId, leftTier, rightItemId, rightTier, success, bonus, coreCount, convergence);
	sendOpenForge();
}

void ProtocolGame::sendForgeHistory(uint8_t page) {
	static constexpr size_t entriesPerPage = 9;
	const auto requestedPage = static_cast<size_t>(page) + 1;
	const auto &historyVector = player->forgeHistory().get();
	const auto historyVectorLen = historyVector.size();

	uint16_t currentPage = 1;
	uint16_t lastPage = 1;

	std::vector<ForgeHistory> historyPerPage;
	if (historyVectorLen > 0) {
		const auto lastPageCount = ((historyVectorLen - 1) / entriesPerPage) + 1;
		lastPage = static_cast<uint16_t>(std::min<size_t>(lastPageCount, std::numeric_limits<uint16_t>::max()));
		currentPage = static_cast<uint16_t>(std::min(requestedPage, static_cast<size_t>(lastPage)));

		const auto pageFirstEntry = historyVectorLen - (static_cast<size_t>(currentPage) - 1) * entriesPerPage;
		const auto pageLastEntry = historyVectorLen > static_cast<size_t>(currentPage) * entriesPerPage ? historyVectorLen - static_cast<size_t>(currentPage) * entriesPerPage : 0;

		for (auto entry = pageFirstEntry; entry > pageLastEntry; --entry) {
			[[maybe_unused]] auto &history_ref = historyPerPage.emplace_back(historyVector[entry - 1]);
		}
	}

	auto historyPageToSend = historyPerPage.size();
	NetworkMessage msg;
	msg.addByte(0x88);
	msg.add<uint16_t>(currentPage - 1); // Current page
	msg.add<uint16_t>(lastPage); // Last page
	msg.addByte(static_cast<uint8_t>(historyPageToSend)); // History to send

	if (historyPageToSend > 0) {
		for (const auto &history : historyPerPage) {
			auto action = magic_enum::enum_integer(history.actionType);
			msg.add<uint32_t>(static_cast<uint32_t>(history.createdAt));
			msg.addByte(action);
			msg.addString(history.description);
			msg.addByte((history.bonus >= 1 && history.bonus < 8) ? 0x01 : 0x00);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendForgeError(const ReturnValue returnValue) {
	sendMessageDialog(getReturnMessage(returnValue));
	closeForgeWindow();
}

void ProtocolGame::sendMarketDetail(uint16_t itemId, uint8_t tier) {
	NetworkMessage msg;
	msg.addByte(0xF8);
	msg.add<uint16_t>(itemId);
	const ItemType &it = Item::items[itemId];

	if (!oldProtocol && it.upgradeClassification > 0) {
		msg.addByte(tier);
	}

	if (it.armor != 0) {
		msg.addString(std::to_string(it.armor));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.isRanged()) {
		std::ostringstream ss;
		bool separator = false;

		if (it.attack != 0) {
			ss << "attack +" << it.attack;
			separator = true;
		}

		if (it.hitChance != 0) {
			if (separator) {
				ss << ", ";
			}
			ss << "chance to hit +" << static_cast<int16_t>(it.hitChance) << "%";
			separator = true;
		}

		if (it.shootRange != 0) {
			if (separator) {
				ss << ", ";
			}
			ss << static_cast<uint16_t>(it.shootRange) << " fields";
		}
		msg.addString(ss.str());
	} else {
		std::string attackDescription;
		if (it.abilities && it.abilities->elementType != COMBAT_NONE && it.abilities->elementDamage != 0) {
			attackDescription = fmt::format("{} {}", it.abilities->elementDamage, getCombatName(it.abilities->elementType));
		}

		if (it.attack != 0 && !attackDescription.empty()) {
			attackDescription = fmt::format("{} physical + {}", it.attack, attackDescription);
		} else if (it.attack != 0 && attackDescription.empty()) {
			attackDescription = std::to_string(it.attack);
		}

		msg.addString(attackDescription);
	}

	if (it.isContainer()) {
		msg.addString(std::to_string(it.maxItems));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.defense != 0 || it.isMissile()) {
		if (it.extraDefense != 0) {
			std::ostringstream ss;
			ss << it.defense << ' ' << std::showpos << it.extraDefense << std::noshowpos;
			msg.addString(ss.str());
		} else {
			msg.addString(std::to_string(it.defense));
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!it.description.empty()) {
		const std::string &descr = it.description;
		if (descr.back() == '.') {
			msg.addString(std::string(descr, 0, descr.length() - 1));
		} else {
			msg.addString(descr);
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.decayTime != 0) {
		std::ostringstream ss;
		ss << it.decayTime << " seconds";
		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (size_t i = 0; i < COMBAT_COUNT; ++i) {
			if (it.abilities->absorbPercent[i] == 0) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}%", getCombatName(indexToCombatType(i)), it.abilities->absorbPercent[i]);
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqLevel != 0) {
		msg.addString(std::to_string(it.minReqLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqMagicLevel != 0) {
		msg.addString(std::to_string(it.minReqMagicLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	msg.addString(it.vocationString);
	msg.addString(it.runeSpellName);

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; i++) {
			if (i == SKILL_MANA_LEECH_CHANCE || i == SKILL_LIFE_LEECH_CHANCE) {
				continue;
			}

			if (!it.abilities->skills[i]) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}", getSkillName(i), it.abilities->skills[i]);
		}

		for (uint8_t i = SKILL_CRITICAL_HIT_CHANCE; i <= SKILL_LAST; i++) {
			if (i == SKILL_MANA_LEECH_CHANCE || i == SKILL_LIFE_LEECH_CHANCE) {
				continue;
			}

			auto skills = it.abilities->skills[i];
			if (!skills) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}%", getSkillName(i), skills / 100.0);
		}

		if (it.abilities->stats[STAT_MAGICPOINTS] != 0) {
			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format(" magic level {:+}", it.abilities->stats[STAT_MAGICPOINTS]);
		}

		// Version 12.72 (Specialized magic level modifier)
		for (uint8_t i = 1; i <= 11; i++) {
			if (it.abilities->specializedMagicLevel[i]) {
				if (separator) {
					ss << ", ";
				} else {
					separator = true;
				}
				auto combatName = getCombatName(indexToCombatType(i));
				ss << fmt::format("{}magic level +{}", combatName, it.abilities->specializedMagicLevel[i]);
			}
		}

		if (it.abilities->speed != 0) {
			if (separator) {
				ss << ", ";
			}

			ss << fmt::format("speed {:+}", (it.abilities->speed >> 1));
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.charges != 0) {
		msg.addString(std::to_string(it.charges));
	} else {
		msg.add<uint16_t>(0x00);
	}

	std::string weaponName = getWeaponName(it.weaponType);

	if (it.slotPosition & SLOTP_TWO_HAND) {
		if (!weaponName.empty()) {
			weaponName += ", two-handed";
		} else {
			weaponName = "two-handed";
		}
	}

	msg.addString(weaponName);

	if (it.weight != 0) {
		std::ostringstream ss;
		if (it.weight < 10) {
			ss << "0.0" << it.weight;
		} else if (it.weight < 100) {
			ss << "0." << it.weight;
		} else {
			std::string weightString = std::to_string(it.weight);
			weightString.insert(weightString.end() - 2, '.');
			ss << weightString;
		}
		ss << " oz";
		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!oldProtocol) {
		std::string augmentsDescription = it.parseAugmentDescription(true);
		if (!augmentsDescription.empty()) {
			msg.addString(augmentsDescription);
		} else {
			msg.add<uint16_t>(0x00); // no augments
		}
	}

	if (it.imbuementSlot > 0) {
		msg.addString(std::to_string(it.imbuementSlot));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!oldProtocol) {
		// Version 12.70 new skills
		if (it.abilities) {
			if (it.abilities->magicShieldCapacityFlat > 0) {
				msg.addString(fmt::format("{:+} and {}%", it.abilities->magicShieldCapacityFlat, it.abilities->magicShieldCapacityPercent));
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->cleavePercent > 0) {
				msg.addString(fmt::format("{}%", it.abilities->cleavePercent));
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->reflectFlat[COMBAT_PHYSICALDAMAGE] > 0) {
				msg.addString(std::to_string(it.abilities->reflectFlat[COMBAT_PHYSICALDAMAGE]));
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->perfectShotDamage > 0) {
				msg.addString(fmt::format("{:+} at range {}", it.abilities->perfectShotDamage, static_cast<unsigned>(it.abilities->perfectShotRange)));
			} else {
				msg.add<uint16_t>(0x00);
			}
		} else {
			// Send empty skills
			// Cleave modifier
			msg.add<uint16_t>(0x00);
			// Magic shield capacity
			msg.add<uint16_t>(0x00);
			// Damage reflection modifier
			msg.add<uint16_t>(0x00);
			// Perfect shot modifier
			msg.add<uint16_t>(0x00);
		}

		// Upgrade classification
		if (it.upgradeClassification > 0) {
			msg.addString(std::to_string(it.upgradeClassification));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (it.elementalBond != COMBAT_NONE) {
			msg.addString(toPascalCase(getCombatName(it.elementalBond)));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (it.mantra > 0) {
			msg.addString(std::to_string(it.mantra));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (clientVersion >= 1510) {
			msg.addString(getMarketDetailImbuementEffect(itemId));
		}

		if (it.upgradeClassification > 0 && tier > 0) {
			std::ostringstream ss;

			double chance;
			if (it.isWeapon()) {
				chance = (0.05 * tier * tier) + (0.4 * tier) + 0.05;
				ss << fmt::format("{} ({:.2f}% Onslaught)", static_cast<uint16_t>(tier), chance);
			} else if (it.isHelmet()) {
				chance = (0.05 * tier * tier) + (1.9 * tier) + 0.05;
				ss << fmt::format("{} ({:.2f}% Momentum)", static_cast<uint16_t>(tier), chance);
			} else if (it.isArmor()) {
				chance = (0.0307576 * tier * tier) + (0.440697 * tier) + 0.026;
				ss << fmt::format("{} ({:.2f}% Ruse)", static_cast<uint16_t>(tier), chance);
			} else if (it.isLegs()) {
				chance = (0.0127 * tier * tier) + (0.1070 * tier) + 0.0073;
				ss << fmt::format("{} ({:.2f}% Transcendence)", static_cast<uint16_t>(tier), chance);
			} else if (it.isBoots()) {
				chance = (0.4 * tier * tier) + (1.7 * tier) + 0.4;
				ss << fmt::format("{} ({:.2f}% Amplification)", static_cast<uint16_t>(tier), chance);
			}
			msg.addString(ss.str());
		} else if (it.upgradeClassification > 0 && tier == 0) {
			msg.addString(std::to_string(tier));
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	const auto &purchaseStatsMap = g_marketRepository().getPurchaseStatistics();
	auto purchaseIterator = purchaseStatsMap.find(itemId);
	if (purchaseIterator != purchaseStatsMap.end()) {
		const auto &tierStatsMap = purchaseIterator->second;
		auto tierStatsIter = tierStatsMap.find(tier);
		if (tierStatsIter != tierStatsMap.end()) {
			const auto &purchaseStatistics = tierStatsIter->second;
			msg.addByte(0x01);
			msg.add<uint32_t>(purchaseStatistics.numTransactions);
			if (oldProtocol) {
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.totalPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.highestPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.lowestPrice));
			} else {
				msg.add<uint64_t>(purchaseStatistics.totalPrice);
				msg.add<uint64_t>(purchaseStatistics.highestPrice);
				msg.add<uint64_t>(purchaseStatistics.lowestPrice);
			}
		} else {
			msg.addByte(0x00);
		}
	} else {
		msg.addByte(0x00); // send to old protocol ?
	}

	const auto &saleStatsMap = g_marketRepository().getSaleStatistics();
	auto saleIterator = saleStatsMap.find(itemId);
	if (saleIterator != saleStatsMap.end()) {
		const auto &tierStatsMap = saleIterator->second;
		auto tierStatsIter = tierStatsMap.find(tier);
		if (tierStatsIter != tierStatsMap.end()) {
			const auto &saleStatistics = tierStatsIter->second;
			msg.addByte(0x01);
			msg.add<uint32_t>(saleStatistics.numTransactions);
			if (oldProtocol) {
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.totalPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.highestPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.lowestPrice));
			} else {
				msg.add<uint64_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.totalPrice));
				msg.add<uint64_t>(saleStatistics.highestPrice);
				msg.add<uint64_t>(saleStatistics.lowestPrice);
			}
		} else {
			msg.addByte(0x00);
		}
	} else {
		msg.addByte(0x00); // send to old protocol ?
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyTimeLeft(const std::unique_ptr<PreySlot> &slot) {
	if (!player || !slot) {
		return;
	}

	NetworkMessage msg;

	msg.addByte(0xE7);
	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.add<uint16_t>(slot->bonusTimeLeft);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyData(const std::unique_ptr<PreySlot> &slot) {
	if (!player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xE8);
	std::vector<uint16_t> validRaceIds;
	for (auto raceId : slot->raceIdList) {
		if (g_monsters().getMonsterTypeByRaceId(raceId)) {
			validRaceIds.emplace_back(raceId);
		} else {
			g_logger().error("[ProtocolGame::sendPreyData] - Unknown monster type raceid: {}, removing prey slot from player {}", raceId, player->getName());
			// Remove wrong raceid from slot
			slot->removeMonsterType(raceId);
			// Send empty bytes (do not debug client)
			msg.addByte(0);
			msg.addByte(1);
			msg.add<uint32_t>(0);
			msg.addByte(0);
			writeToOutputBuffer(msg);
			return;
		}
	}

	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.addByte(static_cast<uint8_t>(slot->state));

	if (slot->state == PreyDataState_Locked) {
		msg.addByte(player->isPremium() ? 0x01 : 0x00);
	} else if (slot->state == PreyDataState_Inactive) {
		// Empty
	} else if (slot->state == PreyDataState_Active) {
		if (const auto mtype = g_monsters().getMonsterTypeByRaceId(slot->selectedRaceId)) {
			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
			} else {
				msg.addByte(outfit.lookHead);
				msg.addByte(outfit.lookBody);
				msg.addByte(outfit.lookLegs);
				msg.addByte(outfit.lookFeet);
				msg.addByte(outfit.lookAddons);
			}

			msg.addByte(static_cast<uint8_t>(slot->bonus));
			msg.add<uint16_t>(slot->bonusPercentage);
			msg.addByte(slot->bonusRarity);
			msg.add<uint16_t>(slot->bonusTimeLeft);
		}
	} else if (slot->state == PreyDataState_Selection) {
		msg.addByte(static_cast<uint8_t>(validRaceIds.size()));
		for (uint16_t raceId : validRaceIds) {
			const auto mtype = g_monsters().getMonsterTypeByRaceId(raceId);
			if (!mtype) {
				continue;
			}

			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
				continue;
			}

			msg.addByte(outfit.lookHead);
			msg.addByte(outfit.lookBody);
			msg.addByte(outfit.lookLegs);
			msg.addByte(outfit.lookFeet);
			msg.addByte(outfit.lookAddons);
		}
	} else if (slot->state == PreyDataState_SelectionChangeMonster) {
		msg.addByte(static_cast<uint8_t>(slot->bonus));
		msg.add<uint16_t>(slot->bonusPercentage);
		msg.addByte(slot->bonusRarity);
		msg.addByte(static_cast<uint8_t>(validRaceIds.size()));
		for (uint16_t raceId : validRaceIds) {
			const auto mtype = g_monsters().getMonsterTypeByRaceId(raceId);
			if (!mtype) {
				continue;
			}

			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
				continue;
			}

			msg.addByte(outfit.lookHead);
			msg.addByte(outfit.lookBody);
			msg.addByte(outfit.lookLegs);
			msg.addByte(outfit.lookFeet);
			msg.addByte(outfit.lookAddons);
		}
	} else if (slot->state == PreyDataState_ListSelection) {
		const std::map<uint16_t, std::string> &bestiaryList = g_game().getBestiaryList();
		msg.add<uint16_t>(static_cast<uint16_t>(bestiaryList.size()));
		std::for_each(bestiaryList.begin(), bestiaryList.end(), [&msg](auto mType) {
			msg.add<uint16_t>(mType.first);
		});
	} else {
		g_logger().warn("[ProtocolGame::sendPreyData] - Unknown prey state: {}", fmt::underlying(slot->state));
		return;
	}

	if (oldProtocol) {
		auto currentTime = OTSYS_TIME();
		auto timeDiffMs = (slot->freeRerollTimeStamp > currentTime) ? (slot->freeRerollTimeStamp - currentTime) : 0;
		auto timeDiffMinutes = timeDiffMs / 60000;
		msg.add<uint16_t>(timeDiffMinutes ? timeDiffMinutes : 0);
	} else {
		msg.add<uint32_t>(std::max<uint32_t>(static_cast<uint32_t>(((slot->freeRerollTimeStamp - OTSYS_TIME()) / 1000)), 0));
		msg.addByte(static_cast<uint8_t>(slot->option));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyPrices() {
	if (!player) {
		return;
	}

	NetworkMessage msg;

	msg.addByte(0xE9);
	msg.add<uint32_t>(player->getPreyRerollPrice());
	if (!oldProtocol) {
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(PREY_BONUS_REROLL_PRICE)));
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(PREY_SELECTION_LIST_PRICE)));
		if (!hasProtocolFeature(protocolProfile, ProtocolFeature::OfficialTaskboardPackets)) {
			msg.add<uint32_t>(player->getTaskHuntingRerollPrice());
			msg.add<uint32_t>(player->getTaskHuntingRerollPrice());
			msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(TASK_HUNTING_SELECTION_LIST_PRICE)));
			msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(TASK_HUNTING_BONUS_REROLL_PRICE)));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendImbuementResult(const std::string &message) {
	NetworkMessage msg;
	msg.addByte(0xED);
	msg.addByte(0x01);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTaskHuntingData(const std::unique_ptr<TaskHuntingSlot> &slot) {
	if (!player || oldProtocol || hasProtocolFeature(protocolProfile, ProtocolFeature::OfficialTaskboardPackets)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xBB);
	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.addByte(static_cast<uint8_t>(slot->state));
	if (slot->state == PreyTaskDataState_Locked) {
		msg.addByte(player->isPremium() ? 0x01 : 0x00);
	} else if (slot->state == PreyTaskDataState_Inactive) {
		// Empty
	} else if (slot->state == PreyTaskDataState_Selection) {
		std::shared_ptr<Player> user = player;
		msg.add<uint16_t>(static_cast<uint16_t>(slot->raceIdList.size()));
		std::for_each(slot->raceIdList.begin(), slot->raceIdList.end(), [&msg, user](uint16_t raceid) {
			msg.add<uint16_t>(raceid);
			msg.addByte(user->isCreatureUnlockedOnTaskHunting(g_monsters().getMonsterTypeByRaceId(raceid)) ? 0x01 : 0x00);
		});
	} else if (slot->state == PreyTaskDataState_ListSelection) {
		std::shared_ptr<Player> user = player;
		const std::map<uint16_t, std::string> &bestiaryList = g_game().getBestiaryList();
		msg.add<uint16_t>(static_cast<uint16_t>(bestiaryList.size()));
		std::for_each(bestiaryList.begin(), bestiaryList.end(), [&msg, user](auto mType) {
			msg.add<uint16_t>(mType.first);
			msg.addByte(user->isCreatureUnlockedOnTaskHunting(g_monsters().getMonsterType(mType.second)) ? 0x01 : 0x00);
		});
	} else if (slot->state == PreyTaskDataState_Active) {
		if (const auto &option = g_ioprey().getTaskRewardOption(slot)) {
			msg.add<uint16_t>(slot->selectedRaceId);
			if (slot->upgrade) {
				msg.addByte(0x01);
				msg.add<uint16_t>(option->secondKills);
			} else {
				msg.addByte(0x00);
				msg.add<uint16_t>(option->firstKills);
			}
			msg.add<uint16_t>(slot->currentKills);
			msg.addByte(slot->rarity);
		} else {
			g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown slot option {} on player {}", fmt::underlying(slot->id), player->getName());
			return;
		}
	} else if (slot->state == PreyTaskDataState_Completed) {
		if (const auto &option = g_ioprey().getTaskRewardOption(slot)) {
			msg.add<uint16_t>(slot->selectedRaceId);
			if (slot->upgrade) {
				msg.addByte(0x01);
				msg.add<uint16_t>(option->secondKills);
				msg.add<uint16_t>(std::min<uint16_t>(slot->currentKills, option->secondKills));
			} else {
				msg.addByte(0x00);
				msg.add<uint16_t>(option->firstKills);
				msg.add<uint16_t>(std::min<uint16_t>(slot->currentKills, option->firstKills));
			}
			msg.addByte(slot->rarity);
		} else {
			g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown slot option {} on player {}", fmt::underlying(slot->id), player->getName());
			return;
		}
	} else {
		g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown task hunting state: {}", fmt::underlying(slot->state));
		return;
	}

	msg.add<uint32_t>(std::max<uint32_t>(static_cast<uint32_t>(((slot->freeRerollTimeStamp - OTSYS_TIME()) / 1000)), 0));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendInventoryImbuements(const std::map<Slots_t, std::shared_ptr<Item>> &items) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x5D);

	msg.addByte(static_cast<uint8_t>(items.size()));
	for (const auto &[slot, item] : items) {
		msg.addByte(slot);
		AddItem(msg, item);

		uint8_t slots = item->getImbuementSlot();
		msg.addByte(slots);
		if (slots == 0) {
			continue;
		}

		for (uint8_t imbueSlot = 0; imbueSlot < slots; imbueSlot++) {
			ImbuementInfo imbuementInfo;
			if (!item->getImbuementInfo(imbueSlot, &imbuementInfo)) {
				msg.addByte(0x00);
				continue;
			}

			auto imbuement = imbuementInfo.imbuement;
			if (!imbuement) {
				msg.addByte(0x00);
				continue;
			}

			const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());
			msg.addByte(0x01);
			msg.addString(baseImbuement->name + " " + imbuement->getName());
			msg.add<uint16_t>(imbuement->getIconID());
			msg.add<uint32_t>(imbuementInfo.duration);

			std::shared_ptr<Tile> playerTile = player->getTile();
			// Check if the player is in a protection zone
			bool isInProtectionZone = playerTile && playerTile->hasFlag(TILESTATE_PROTECTIONZONE);
			// Check if the player is in fight mode
			bool isInFightMode = player->hasCondition(CONDITION_INFIGHT);
			// Get the category of the imbuement
			const CategoryImbuement* categoryImbuement = g_imbuements().getCategoryByID(imbuement->getCategory());
			// Parent of the imbued item
			auto parent = item->getParent();
			// If the imbuement is aggressive and the player is not in fight mode or is in a protection zone, or the item is in a container, ignore it.
			if (categoryImbuement && categoryImbuement->agressive && (isInProtectionZone || !isInFightMode)) {
				msg.addByte(0);
				continue;
			}
			// If the item is not in the backpack slot and it's not a agressive imbuement, ignore it.
			if (categoryImbuement && !categoryImbuement->agressive && parent && parent != player) {
				msg.addByte(0);
				continue;
			}

			msg.addByte(1);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendForgeSkillStats(NetworkMessage &msg) const {
	if (oldProtocol) {
		return;
	}

	std::vector<Slots_t> slots { CONST_SLOT_LEFT, CONST_SLOT_ARMOR, CONST_SLOT_HEAD, CONST_SLOT_LEGS };
	for (const auto &slot : slots) {
		double_t skill = 0;
		const auto &item = player->getInventoryItem(slot);
		if (!item) {
			continue;
		}

		const ItemType &it = Item::items[item->getID()];
		if (it.isWeapon()) {
			skill = item->getFatalChance() * 100;
		}
		if (it.isArmor()) {
			skill = item->getDodgeChance() * 100;
		}
		if (it.isHelmet()) {
			skill = item->getMomentumChance() * 100;
		}
		if (it.isLegs()) {
			skill = item->getTranscendenceChance() * 100;
		}
		if (it.isBoots()) {
			skill = item->getAmplificationChance();
		}

		auto skillCast = static_cast<uint16_t>(skill);
		msg.add<uint16_t>(skillCast);
		msg.add<uint16_t>(skillCast);
	}
}

void ProtocolGame::sendOpenWheelWindow(uint32_t ownerId) {
	if (!player || oldProtocol || !g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		return;
	}

	NetworkMessage msg;
	player->wheel().sendOpenWheelWindow(msg, ownerId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendGemAtelierGemRevealed(uint16_t gemIndex) {
	if (!player || oldProtocol || !hasProtocolFeature(protocolProfile, ProtocolFeature::CurrentPayload)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC5);
	msg.add<uint16_t>(gemIndex);
	writeToOutputBuffer(msg);
}
