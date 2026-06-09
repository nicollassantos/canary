/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/market/market_service.hpp"
#include "account/account.hpp"
#include "config/configmanager.hpp"
#include "enums/account_coins.hpp"
#include "enums/account_errors.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/market_repository.hpp"
#include "io/iomarket.hpp"
#include "items/containers/depot/depotlocker.hpp"
#include "items/containers/inbox/inbox.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "lib/metrics/metrics.hpp"
#include "utils/tools.hpp"
#include "game/scheduling/save_manager.hpp"

void MarketService::playerBrowseMarket(uint32_t playerId, uint16_t itemId, uint8_t tier) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const ItemType &it = Item::items[itemId];
	if (it.id == 0) {
		return;
	}

	if (it.wareId == 0) {
		return;
	}

	const MarketOfferList &buyOffers = g_marketRepository().getActiveOffers(MARKETACTION_BUY, it.id, tier);
	const MarketOfferList &sellOffers = g_marketRepository().getActiveOffers(MARKETACTION_SELL, it.id, tier);
	player->sendMarketBrowseItem(it.id, buyOffers, sellOffers, tier);
	player->sendMarketDetail(it.id, tier);
}

void MarketService::playerBrowseMarketOwnOffers(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const MarketOfferList &buyOffers = g_marketRepository().getOwnOffers(MARKETACTION_BUY, player->getGUID());
	const MarketOfferList &sellOffers = g_marketRepository().getOwnOffers(MARKETACTION_SELL, player->getGUID());
	player->sendMarketBrowseOwnOffers(buyOffers, sellOffers);
}

void MarketService::playerBrowseMarketOwnHistory(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	const HistoryMarketOfferList &buyOffers = g_marketRepository().getOwnHistory(MARKETACTION_BUY, player->getGUID());
	const HistoryMarketOfferList &sellOffers = g_marketRepository().getOwnHistory(MARKETACTION_SELL, player->getGUID());
	player->sendMarketBrowseOwnHistory(buyOffers, sellOffers);
}

namespace {
	bool removeOfferItems(const std::shared_ptr<Player> &player, const std::shared_ptr<DepotLocker> &depotLocker, const ItemType &itemType, uint16_t amount, uint8_t tier, std::ostringstream &offerStatus) {
		uint16_t removeAmount = amount;
		if (tier == 0) {
			if (
				// Init-statement
				auto stashItemCount = player->getStashItemCount(itemType.wareId);
				// Condition
				stashItemCount > 0
			) {
				if (removeAmount > stashItemCount && player->withdrawItem(itemType.wareId, stashItemCount)) {
					removeAmount -= stashItemCount;
				} else if (player->withdrawItem(itemType.wareId, removeAmount)) {
					removeAmount = 0;
				} else {
					offerStatus << "Failed to remove stash items from player " << player->getName();
					return false;
				}
			}
		}

		auto [itemVector, totalCount] = player->getLockerItemsAndCountById(depotLocker, tier, itemType.id);
		if (removeAmount > 0) {
			if (totalCount == 0 || itemVector.empty()) {
				offerStatus << "Player " << player->getName() << " not have item for create offer";
				return false;
			}

			uint32_t removedCount = 0;
			for (const auto &item : itemVector) {
				if (!item) {
					continue;
				}

				if (removedCount >= removeAmount) {
					break;
				}

				uint16_t thisRemove = std::min<uint16_t>(
					removeAmount - removedCount,
					item->getItemCount()
				);

				ReturnValue ret = player->removeItem(item, thisRemove);
				if (ret != RETURNVALUE_NOERROR) {
					offerStatus << "Failed to remove: " << amount << " items of id: " << itemType.id << " from player " << player->getName() << " error: " << getReturnMessage(ret);
					return false;
				}

				removedCount += thisRemove;
			}

			player->updateState();

			if (removedCount < removeAmount) {
				g_logger().error("Player {} tried to sell an item {} without this item", player->getName(), itemType.id);
				offerStatus << "The item you tried to market is not correct. Check the item again.";
				return false;
			}
		}
		return true;
	}

	ReturnValue queryItemInsertion(const std::shared_ptr<Player> &recipient, uint16_t itemId, uint16_t amount, uint8_t tier) {
		uint32_t actuallyAdded = 0;
		return recipient->addItemBatchToPaginedContainer(
			recipient->getInbox(),
			itemId,
			amount,
			actuallyAdded,
			FLAG_NOLIMIT,
			tier,
			true
		);
	}

	bool rollbackInboxInsertion(const std::shared_ptr<Player> &recipient, uint16_t itemId, uint32_t amount, uint8_t tier) {
		if (!recipient || amount == 0) {
			return true;
		}

		const auto &recipientInbox = recipient->getInbox();
		if (!recipientInbox) {
			return false;
		}

		auto rollbackProbeItem = Item::createItemBatch(itemId, 1, 0);
		if (!rollbackProbeItem) {
			return false;
		}
		if (tier > 0) {
			rollbackProbeItem->setTier(tier);
		}

		uint32_t removedCount = 0;
		std::vector<std::shared_ptr<Item>> rollbackCandidates;
		rollbackCandidates.reserve(recipientInbox->size());

		for (const auto &inboxItem : recipientInbox->getItemList()) {
			if (!inboxItem || !inboxItem->equals(rollbackProbeItem)) {
				continue;
			}
			rollbackCandidates.push_back(inboxItem);
		}

		for (const auto &rollbackItem : rollbackCandidates) {
			if (removedCount >= amount) {
				break;
			}

			const uint32_t toRemove = std::min<uint32_t>(amount - removedCount, rollbackItem->getItemCount());
			const ReturnValue removeResult = recipient->removeItem(rollbackItem, toRemove);
			if (removeResult != RETURNVALUE_NOERROR) {
				g_logger().error("{} - Failed to rollback inbox item {} amount {} for player {}, error code: {}", __FUNCTION__, itemId, toRemove, recipient->getName(), getReturnMessage(removeResult));
				return false;
			}

			removedCount += toRemove;
		}

		if (removedCount < amount) {
			g_logger().error("{} - Incomplete inbox rollback for player {}, item {}, expected remove {}, removed {}", __FUNCTION__, recipient->getName(), itemId, amount, removedCount);
			return false;
		}

		return true;
	}

	ReturnValue processItemInsertion(const std::shared_ptr<Player> &recipient, uint16_t itemId, uint16_t amount, uint8_t tier) {
		uint32_t actuallyAdded = 0;
		ReturnValue returnValue = recipient->addItemBatchToPaginedContainer(
			recipient->getInbox(),
			itemId,
			amount,
			actuallyAdded,
			FLAG_NOLIMIT,
			tier
		);
		if (returnValue != RETURNVALUE_NOERROR) {
			g_logger().warn("{} - Failed to add item {} total amount {} to inbox for player {}, error code: {}", __FUNCTION__, itemId, amount, recipient->getName(), getReturnMessage(returnValue));
			return returnValue;
		}

		if (actuallyAdded != amount) {
			g_logger().error("{} - Atomic inbox insertion mismatch for item {} total amount {}, currently added: {} to inbox for player {}", __FUNCTION__, itemId, amount, actuallyAdded, recipient->getName());
			if (actuallyAdded > 0 && !rollbackInboxInsertion(recipient, itemId, actuallyAdded, tier)) {
				g_logger().error("{} - Failed to rollback partial inbox insertion mismatch for item {} total amount {}, currently added: {} to inbox for player {}", __FUNCTION__, itemId, amount, actuallyAdded, recipient->getName());
			}
			return RETURNVALUE_NOTPOSSIBLE;
		}

		return RETURNVALUE_NOERROR;
	}

	void sendInboxSpaceMessage(const std::shared_ptr<Player> &recipient) {
		if (recipient && !recipient->isOffline()) {
			recipient->sendTextMessage(MESSAGE_MARKET, "Not enough space in your inbox.");
		}
	}

	void sendBuyerInboxSpaceMessage(const std::shared_ptr<Player> &seller, const std::shared_ptr<Player> &buyer) {
		if (seller) {
			seller->sendTextMessage(MESSAGE_MARKET, "The buyer does not have enough space in the inbox.");
		}
		sendInboxSpaceMessage(buyer);
	}

	[[nodiscard]] bool isInboxCapacityError(ReturnValue returnValue) {
		return returnValue == RETURNVALUE_DEPOTISFULL
			|| returnValue == RETURNVALUE_CONTAINERNOTENOUGHROOM
			|| returnValue == RETURNVALUE_NOTENOUGHROOM;
	}

	bool handleInboxPrecheckFailure(const std::shared_ptr<Player> &recipient, ReturnValue returnValue, const std::string &context) {
		if (returnValue == RETURNVALUE_NOERROR) {
			return false;
		}

		if (isInboxCapacityError(returnValue)) {
			sendInboxSpaceMessage(recipient);
		} else {
			if (recipient && !recipient->isOffline()) {
				recipient->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			}

			const std::string playerName = recipient ? recipient->getName() : "unknown";
			g_logger().warn("{} - Unexpected inbox precheck failure for player {}, error code: {}", context, playerName, getReturnMessage(returnValue));
		}

		return true;
	}

	bool handleBuyerInboxPrecheckFailure(const std::shared_ptr<Player> &seller, const std::shared_ptr<Player> &buyer, ReturnValue returnValue, const std::string &context) {
		if (returnValue == RETURNVALUE_NOERROR) {
			return false;
		}

		if (isInboxCapacityError(returnValue)) {
			sendBuyerInboxSpaceMessage(seller, buyer);
		} else {
			if (seller) {
				seller->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			}
			if (buyer && !buyer->isOffline()) {
				buyer->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			}

			const std::string sellerName = seller ? seller->getName() : "unknown";
			const std::string buyerName = buyer ? buyer->getName() : "unknown";
			g_logger().warn("{} - Unexpected buyer inbox precheck failure (seller: {}, buyer: {}), error code: {}", context, sellerName, buyerName, getReturnMessage(returnValue));
		}

		return true;
	}

	struct WithdrawnFunds {
		uint64_t bank = 0;
		uint64_t inventory = 0;

		[[nodiscard]] uint64_t total() const {
			return bank + inventory;
		}
	};

	bool removeInventoryMoneyToBank(const std::shared_ptr<Player> &player, uint64_t money) {
		if (!player) {
			g_logger().error("[{}] player is nullptr", __FUNCTION__);
			return false;
		}

		if (money == 0) {
			return true;
		}

		std::multimap<uint32_t, std::shared_ptr<Item>> moneyMap;
		uint64_t inventoryMoney = 0;

		for (const auto &item : player->getAllInventoryItems()) {
			if (!item || item->getContainer()) {
				continue;
			}

			const uint32_t worth = item->getWorth();
			if (worth == 0) {
				continue;
			}

			inventoryMoney += worth;
			moneyMap.emplace(worth, item);
		}

		if (inventoryMoney < money) {
			return false;
		}

		for (const auto &[worth, item] : moneyMap) {
			if (worth < money) {
				g_game().internalRemoveItem(item);
				money -= worth;
				continue;
			}

			if (worth > money) {
				const uint32_t singleWorth = worth / item->getItemCount();
				const uint32_t removeCount = static_cast<uint32_t>(std::ceil(money / static_cast<double>(singleWorth)));
				const uint64_t change = static_cast<uint64_t>(singleWorth) * removeCount - money;
				g_game().internalRemoveItem(item, removeCount);
				if (change > 0) {
					player->setBankBalance(player->getBankBalance() + change);
				}
				return true;
			}

			g_game().internalRemoveItem(item);
			return true;
		}

		return true;
	}

	bool withdrawMarketFunds(const std::shared_ptr<Player> &player, uint64_t amount, WithdrawnFunds &withdrawnFunds) {
		withdrawnFunds = {};

		if (!player) {
			return false;
		}

		if (amount == 0) {
			return true;
		}

		withdrawnFunds.bank = std::min<uint64_t>(player->getBankBalance(), amount);
		if (withdrawnFunds.bank > 0) {
			player->setBankBalance(player->getBankBalance() - withdrawnFunds.bank);
		}

		withdrawnFunds.inventory = amount - withdrawnFunds.bank;
		if (withdrawnFunds.inventory > 0 && !removeInventoryMoneyToBank(player, withdrawnFunds.inventory)) {
			if (withdrawnFunds.bank > 0) {
				player->setBankBalance(player->getBankBalance() + withdrawnFunds.bank);
			}
			withdrawnFunds = {};
			return false;
		}

		return true;
	}

	void refundMarketFunds(const std::shared_ptr<Player> &player, const WithdrawnFunds &withdrawnFunds) {
		if (!player || withdrawnFunds.total() == 0) {
			return;
		}

		player->setBankBalance(player->getBankBalance() + withdrawnFunds.total());
	}

} // namespace

bool checkCanInitCreateMarketOffer(const std::shared_ptr<Player> &player, uint8_t type, const ItemType &it, uint16_t amount, uint64_t price, std::ostringstream &offerStatus) {
	if (!player) {
		offerStatus << "Failed to load player";
		return false;
	}

	if (!player->getAccount()) {
		offerStatus << "Failed to load player account";
		return false;
	}

	if (!player->isInMarket()) {
		offerStatus << "Failed to load market for player " << player->getName();
		return false;
	}

	if (price == 0) {
		offerStatus << "Failed to process price for player " << player->getName();
		return false;
	}

	if (price > 999999999999) {
		offerStatus << "Player " << player->getName() << " is trying to sell an item with a higher than allowed value";
		return false;
	}

	if (type != MARKETACTION_BUY && type != MARKETACTION_SELL) {
		offerStatus << "Failed to process type " << type << "for player " << player->getName();
		return false;
	}

	if (player->isUIExhausted(1000)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (it.id == 0 || it.wareId == 0) {
		offerStatus << "Failed to load offer or item id";
		return false;
	}

	if (amount == 0 || (!it.stackable && amount > 2000) || (it.stackable && amount > 64000)) {
		offerStatus << "Failed to load amount " << amount << " for player " << player->getName();
		return false;
	}

	g_logger().debug("{} - Offer amount: {}", __FUNCTION__, amount);

	if (g_configManager().getBoolean(MARKET_PREMIUM) && !player->isPremium()) {
		player->sendTextMessage(MESSAGE_MARKET, "Only premium accounts may create offers for that object.");
		return false;
	}

	const uint32_t maxOfferCount = g_configManager().getNumber(MAX_MARKET_OFFERS_AT_A_TIME_PER_PLAYER);
	if (maxOfferCount != 0 && g_marketRepository().getPlayerOfferCount(player->getGUID()) >= maxOfferCount) {
		offerStatus << "Player " << player->getName() << "excedeed max offer count " << maxOfferCount;
		return false;
	}

	return true;
}

void MarketService::playerCreateMarketOffer(uint32_t playerId, uint8_t type, uint16_t itemId, uint16_t amount, uint64_t price, uint8_t tier, bool anonymous) {
	// Initialize variables
	// Before creating the offer we will compare it with the RETURN VALUE ERROR
	std::ostringstream offerStatus;
	const auto &player = game_.getPlayerByID(playerId);
	const ItemType &it = Item::items[itemId];

	// Make sure everything is ok before the create market offer starts
	if (!checkCanInitCreateMarketOffer(player, type, it, amount, price, offerStatus)) {
		g_logger().error("{} - Player {} had an error on init offer on the market, error code: {}", __FUNCTION__, player->getName(), offerStatus.str());
		return;
	}

	const uint8_t maxTier = static_cast<uint8_t>(config_.getNumber(FORGE_MAX_ITEM_TIER));
	if (tier > maxTier) {
		tier = maxTier;
	}

	if (tier > 0 && it.upgradeClassification == 0) {
		tier = 0;
	}

	uint64_t totalPrice = price * amount;
	uint64_t totalFee = totalPrice * 0.02; // 2% fee
	uint64_t minFee = 20; // Min fee is 20gp
	uint64_t maxFee = std::min<uint64_t>(1000000, totalFee); // Max fee is 1kk
	// Prevent std::clamp from hitting an invalid range (min > max), which in MSVC debug builds triggers an assertion failure
	if (maxFee < minFee) {
		maxFee = minFee;
	}

	uint64_t fee = std::clamp(totalFee, uint64_t(20), maxFee); // Limit between 20 and maxFee

	if (type == MARKETACTION_SELL) {
		if (fee > (player->getMoney() + player->getBankBalance())) {
			offerStatus << "Fee is greater than player money";
			return;
		}

		const std::shared_ptr<DepotLocker> &depotLocker = player->getDepotLocker(player->getLastDepotId());
		if (depotLocker == nullptr) {
			offerStatus << "Depot locker is nullptr for player " << player->getName();
			return;
		}

		if (it.id == ITEM_STORE_COIN) {
			auto [transferableCoins, result] = player->getAccount()->getCoins(CoinType::Transferable);

			if (amount > transferableCoins) {
				offerStatus << "Amount is greater than coins for player " << player->getName();
				return;
			}

			// Do not register a transaction for coins creating an offer
			player->getAccount()->removeCoins(CoinType::Transferable, static_cast<uint32_t>(amount), "");
		} else {
			if (!removeOfferItems(player, depotLocker, it, amount, tier, offerStatus)) {
				g_logger().error("[{}] failed to remove item with id {}, from player {}, errorcode: {}", __FUNCTION__, it.id, player->getName(), offerStatus.str());
				return;
			}
		}

		game_.removeMoney(player, fee, 0, true);
		g_metrics().addCounter("balance_decrease", fee, { { "player", player->getName() }, { "context", "market_fee" } });
	} else {
		uint64_t totalPrice = price * amount;
		totalPrice += fee;
		if (totalPrice > (player->getMoney() + player->getBankBalance())) {
			offerStatus << "Fee is greater than player money (buy offer)";
			return;
		}

		game_.removeMoney(player, totalPrice, 0, true);
		g_metrics().addCounter("balance_decrease", totalPrice, { { "player", player->getName() }, { "context", "market_offer" } });
	}

	// Send market window again for update item stats and avoid item clone
	player->sendMarketEnter(player->getLastDepotId());

	// If there is any error, then we will send the log and block the creation of the offer to avoid clone of items
	// The player may lose the item as it will have already been removed, but will not clone
	if (!offerStatus.str().empty()) {
		if (offerStatus.str() == "The item you tried to market is not correct. Check the item again.") {
			player->sendTextMessage(MESSAGE_MARKET, offerStatus.str());
		} else {
			player->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
		}
		g_logger().error("{} - Player {} had an error creating an offer on the market, error code: {}", __FUNCTION__, player->getName(), offerStatus.str());
		return;
	}

	g_marketRepository().createOffer(player->getGUID(), static_cast<MarketAction_t>(type), it.id, amount, price, tier, anonymous);

	const MarketOfferList &buyOffers = g_marketRepository().getActiveOffers(MARKETACTION_BUY, it.id, tier);
	const MarketOfferList &sellOffers = g_marketRepository().getActiveOffers(MARKETACTION_SELL, it.id, tier);
	player->sendMarketBrowseItem(it.id, buyOffers, sellOffers, tier);

	// Exhausted for create offert in the market
	player->updateUIExhausted();
	g_saveManager().savePlayer(player);
}

void MarketService::playerCancelMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->getAccount()) {
		return;
	}

	if (!player->isInMarket()) {
		return;
	}

	if (player->isUIExhausted(1000)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	MarketOfferEx offer = g_marketRepository().getOfferByCounter(timestamp, counter);
	if (offer.id == 0 || offer.playerId != player->getGUID()) {
		return;
	}

	if (offer.type == MARKETACTION_BUY) {
		player->setBankBalance(player->getBankBalance() + offer.price * offer.amount);
		g_metrics().addCounter("balance_decrease", offer.price * offer.amount, { { "player", player->getName() }, { "context", "market_purchase" } });
		// Send market window again for update stats
		player->sendMarketEnter(player->getLastDepotId());
	} else {
		const ItemType &it = Item::items[offer.itemId];
		if (it.id == 0) {
			return;
		}

		const uint8_t maxTier = static_cast<uint8_t>(config_.getNumber(FORGE_MAX_ITEM_TIER));
		const uint8_t offerTier = it.upgradeClassification > 0 ? std::min<uint8_t>(offer.tier, maxTier) : 0;
		offer.tier = offerTier;

		if (it.id == ITEM_STORE_COIN) {
			// Do not register a transaction for coins upon cancellation
			player->getAccount()->addCoins(CoinType::Transferable, offer.amount, "");
		} else {
			ReturnValue inboxCheckResult = queryItemInsertion(player, it.id, offer.amount, offerTier);
			if (handleInboxPrecheckFailure(player, inboxCheckResult, __FUNCTION__)) {
				return;
			}

			ReturnValue inboxInsertResult = processItemInsertion(player, it.id, offer.amount, offerTier);
			if (inboxInsertResult != RETURNVALUE_NOERROR) {
				player->sendTextMessage(MESSAGE_MARKET, "There was an error returning your items, please contact the administrator.");
				g_logger().error("{} - Failed to return cancelled offer item {} total amount {} to inbox for player {}, error code: {}", __FUNCTION__, it.id, offer.amount, player->getName(), getReturnMessage(inboxInsertResult));
				return;
			}
		}
	}

	g_marketRepository().moveOfferToHistory(offer.id, OFFERSTATE_CANCELLED);

	offer.amount = 0;
	offer.timestamp += config_.getNumber(MARKET_OFFER_DURATION);
	player->sendMarketCancelOffer(offer);
	// Send market window again for update stats
	player->sendMarketEnter(player->getLastDepotId());
	// Exhausted for cancel offer in the market
	player->updateUIExhausted();
	g_saveManager().savePlayer(player);
}

void MarketService::playerAcceptMarketOffer(uint32_t playerId, uint32_t timestamp, uint16_t counter, uint16_t amount) {
	std::ostringstream offerStatus;
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->getAccount()) {
		offerStatus << "Failed to load player";
		return;
	}

	if (!player->isInMarket()) {
		offerStatus << "Failed to load market";
		return;
	}

	if (player->isUIExhausted(1000)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	MarketOfferEx offer = g_marketRepository().getOfferByCounter(timestamp, counter);
	if (offer.id == 0) {
		offerStatus << "Failed to load offer id";
		return;
	}

	const ItemType &it = Item::items[offer.itemId];
	if (it.id == 0) {
		offerStatus << "Failed to load item id";
		return;
	}

	const uint8_t maxTier = static_cast<uint8_t>(config_.getNumber(FORGE_MAX_ITEM_TIER));
	const uint8_t offerTier = it.upgradeClassification > 0 ? std::min<uint8_t>(offer.tier, maxTier) : 0;
	offer.tier = offerTier;

	if (amount == 0 || (!it.stackable && amount > 2000) || (it.stackable && amount > 64000) || amount > offer.amount) {
		offerStatus << "Invalid offer amount " << amount << " for player " << player->getName();
		return;
	}

	uint64_t totalPrice = offer.price * amount;

	// The player has an offer to by something and someone is going to sell to item type
	// so the market action is 'buy' as who created the offer is buying.
	if (offer.type == MARKETACTION_BUY) {
		const std::shared_ptr<DepotLocker> &depotLocker = player->getDepotLocker(player->getLastDepotId());
		if (depotLocker == nullptr) {
			offerStatus << "Depot locker is nullptr";
			return;
		}

		const std::shared_ptr<Player> &buyerPlayer = game_.getPlayerByGUID(offer.playerId, true);
		if (!buyerPlayer) {
			offerStatus << "Failed to load buyer player " << player->getName();
			return;
		}

		const auto &buyerPlayerAccount = buyerPlayer->getAccount();
		if (!buyerPlayerAccount) {
			player->sendTextMessage(MESSAGE_MARKET, "Cannot accept offer.");
			return;
		}

		const auto &playerAccount = player->getAccount();
		if (player == buyerPlayer || playerAccount == buyerPlayerAccount) {
			player->sendTextMessage(MESSAGE_MARKET, "You cannot accept your own offer.");
			return;
		}

		if (it.id != ITEM_STORE_COIN) {
			ReturnValue inboxCheckResult = queryItemInsertion(buyerPlayer, it.id, amount, offerTier);
			if (handleBuyerInboxPrecheckFailure(player, buyerPlayer, inboxCheckResult, __FUNCTION__)) {
				return;
			}

			ReturnValue inboxInsertResult = processItemInsertion(buyerPlayer, it.id, amount, offerTier);
			if (inboxInsertResult != RETURNVALUE_NOERROR) {
				offerStatus << "Failed to add item " << it.id << " total amount " << amount << " to inbox for player " << buyerPlayer->getName() << " error: " << getReturnMessage(inboxInsertResult);
			}
		}

		if (!offerStatus.str().empty()) {
			player->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			g_logger().error("{} - Player {} had an error accepting an offer on the market, error code: {}", __FUNCTION__, player->getName(), offerStatus.str());
			player->sendMarketEnter(player->getLastDepotId());
			return;
		}

		if (it.id == ITEM_STORE_COIN) {
			auto [transferableCoins, error] = playerAccount->getCoins(CoinType::Transferable);

			if (error != AccountErrors_t::Ok) {
				offerStatus << "Failed to load transferable coins for player " << player->getName();
				return;
			}

			if (amount > transferableCoins) {
				offerStatus << "Amount is greater than coins";
				return;
			}

			playerAccount->removeCoins(
				CoinType::Transferable,
				amount,
				"Sold on Market"
			);
		} else {
			if (!removeOfferItems(player, depotLocker, it, amount, offerTier, offerStatus)) {
				ReturnValue rollbackResult = rollbackInboxInsertion(buyerPlayer, it.id, amount, offerTier) ? RETURNVALUE_NOERROR : RETURNVALUE_NOTPOSSIBLE;
				if (rollbackResult != RETURNVALUE_NOERROR) {
					offerStatus << "; rollback from buyer inbox failed: " << getReturnMessage(rollbackResult);
					g_logger().error("{} - Failed to rollback delivered market items {} amount {} from buyer {} inbox after seller removal error, rollback code: {}", __FUNCTION__, it.id, amount, buyerPlayer->getName(), getReturnMessage(rollbackResult));
				} else {
					g_logger().warn("{} - Rolled back delivered market items {} amount {} from buyer {} inbox after seller removal error", __FUNCTION__, it.id, amount, buyerPlayer->getName());
				}
				g_logger().error("[{}] failed to remove item with id {}, from player {}, errorcode: {}", __FUNCTION__, it.id, player->getName(), offerStatus.str());
			}
		}

		// If there is any error, then we will send the log and block the creation of the offer to avoid clone of items
		if (!offerStatus.str().empty()) {
			if (offerStatus.str() == "The item you tried to market is not correct. Check the item again.") {
				player->sendTextMessage(MESSAGE_MARKET, offerStatus.str());
			} else {
				player->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			}
			g_logger().error("{} - Player {} had an error accepting an offer on the market, error code: {}", __FUNCTION__, player->getName(), offerStatus.str());
			player->sendMarketEnter(player->getLastDepotId());
			return;
		}

		if (it.id == ITEM_STORE_COIN) {
			buyerPlayer->getAccount()->addCoins(CoinType::Transferable, amount, "Purchased on Market");
		}

		if (offerStatus.str().empty()) {
			player->setBankBalance(player->getBankBalance() + totalPrice);
			g_metrics().addCounter("balance_increase", totalPrice, { { "player", player->getName() }, { "context", "market_sale" } });

			if (buyerPlayer->isOffline()) {
				g_saveManager().savePlayer(buyerPlayer);
			}
		}
	} else if (offer.type == MARKETACTION_SELL) {
		std::shared_ptr<Player> sellerPlayer = game_.getPlayerByGUID(offer.playerId, true);
		if (!sellerPlayer) {
			offerStatus << "Failed to load seller player";
			return;
		}

		if (player == sellerPlayer || player->getAccount() == sellerPlayer->getAccount()) {
			player->sendTextMessage(MESSAGE_MARKET, "You cannot accept your own offer.");
			return;
		}

		if (totalPrice > (player->getBankBalance() + player->getMoney())) {
			return;
		}

		if (it.id != ITEM_STORE_COIN) {
			ReturnValue inboxCheckResult = queryItemInsertion(player, it.id, amount, offerTier);
			if (handleInboxPrecheckFailure(player, inboxCheckResult, __FUNCTION__)) {
				return;
			}
		}

		WithdrawnFunds buyerWithdrawnFunds;
		if (!withdrawMarketFunds(player, totalPrice, buyerWithdrawnFunds)) {
			player->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
			g_logger().error("{} - Failed to debit buyer funds for accepted market offer (player: {}, price: {})", __FUNCTION__, player->getName(), totalPrice);
			return;
		}

		if (it.id == ITEM_STORE_COIN) {
			player->getAccount()->addCoins(CoinType::Transferable, amount, "Purchased on Market");
		} else {
			ReturnValue inboxInsertResult = processItemInsertion(player, it.id, amount, offer.tier);
			if (inboxInsertResult != RETURNVALUE_NOERROR) {
				refundMarketFunds(player, buyerWithdrawnFunds);
				offerStatus << "Failed to add item " << it.id << " total amount " << amount << " to inbox for player " << player->getName() << " error: " << getReturnMessage(inboxInsertResult);
			}
		}

		if (offerStatus.str().empty()) {
			g_metrics().addCounter("balance_decrease", totalPrice, { { "player", player->getName() }, { "context", "market_purchase" } });
			sellerPlayer->setBankBalance(sellerPlayer->getBankBalance() + totalPrice);
			g_metrics().addCounter("balance_increase", totalPrice, { { "player", sellerPlayer->getName() }, { "context", "market_sale" } });
			if (it.id == ITEM_STORE_COIN) {
				sellerPlayer->getAccount()->registerCoinTransaction(CoinTransactionType::Remove, CoinType::Transferable, amount, "Sold on Market");
			}

			if (it.id != ITEM_STORE_COIN) {
				player->onReceiveMail();
			}

			if (sellerPlayer->isOffline()) {
				g_saveManager().savePlayer(sellerPlayer);
			}
		}
	}

	// Send market window again for update item stats and avoid item clone
	player->sendMarketEnter(player->getLastDepotId());

	if (!offerStatus.str().empty()) {
		player->sendTextMessage(MESSAGE_MARKET, "There was an error processing your offer, please contact the administrator.");
		g_logger().error("{} - Player {} had an error accepting an offer on the market, error code: {}", __FUNCTION__, player->getName(), offerStatus.str());
		return;
	}

	const int32_t marketOfferDuration = config_.getNumber(MARKET_OFFER_DURATION);

	g_marketRepository().appendHistory(player->getGUID(), (offer.type == MARKETACTION_BUY ? MARKETACTION_SELL : MARKETACTION_BUY), offer.itemId, amount, offer.price, time(nullptr), offer.tier, OFFERSTATE_ACCEPTEDEX);

	g_marketRepository().appendHistory(offer.playerId, offer.type, offer.itemId, amount, offer.price, time(nullptr), offer.tier, OFFERSTATE_ACCEPTED);

	offer.amount -= amount;

	if (offer.amount == 0) {
		g_marketRepository().deleteOffer(offer.id);
	} else {
		g_marketRepository().acceptOffer(offer.id, amount);
	}

	offer.timestamp += marketOfferDuration;
	player->sendMarketAcceptOffer(offer);
	// Exhausted for accept offer in the market
	player->updateUIExhausted();
	g_saveManager().savePlayer(player);
}


