/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/market/market_expiry.hpp"

#include "config/config_port.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/player_repository.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "game/scheduling/save_manager.hpp"
#include "items/containers/inbox/inbox.hpp"
#include "items/item.hpp"
#include "utils/tools.hpp"

namespace market_expiry {

	void processExpiredOffers(Game &game, IMarketRepository &market, IPlayerRepository &players, const std::vector<ExpiredMarketOffer> &offers) {
		for (const auto &offer : offers) {
			if (!market.moveOfferToHistory(offer.id, OFFERSTATE_EXPIRED)) {
				continue;
			}

			const auto playerId = offer.playerId;
			const auto amount = offer.amount;
			auto tier = offer.tier;
			if (offer.action == MARKETACTION_SELL) {
				const ItemType &itemType = Item::items[offer.itemId];
				if (itemType.id == 0) {
					continue;
				}

				const auto &player = game.getPlayerByGUID(playerId, true);
				if (!player) {
					continue;
				}

				const auto &playerInbox = player->getInbox();

				if (itemType.stackable) {
					uint16_t tmpAmount = amount;
					while (tmpAmount > 0) {
						uint16_t stackCount = std::min<uint16_t>(100, tmpAmount);
						const auto &item = Item::CreateItem(itemType.id, stackCount);
						if (game.internalAddItem(playerInbox, item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
							g_logger().error("[{}] Ocurred an error to add item with id {} to player {}", __FUNCTION__, itemType.id, player->getName());

							break;
						}

						if (tier != 0) {
							item->setTier(tier);
						}

						tmpAmount -= stackCount;
					}
				} else {
					int32_t subType;
					if (itemType.charges != 0) {
						subType = itemType.charges;
					} else {
						subType = -1;
					}

					for (uint16_t i = 0; i < amount; ++i) {
						const auto &item = Item::CreateItem(itemType.id, subType);
						if (game.internalAddItem(playerInbox, item, INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
							break;
						}

						if (tier != 0) {
							item->setTier(tier);
						}
					}
				}

				if (player->isOffline()) {
					g_saveManager().savePlayer(player);
				}
			} else {
				uint64_t totalPrice = offer.price * amount;

				const auto &player = game.getPlayerByGUID(playerId);
				if (player) {
					player->setBankBalance(player->getBankBalance() + totalPrice);
				} else {
					players.increaseBankBalance(playerId, totalPrice);
				}
			}
		}
	}

	void checkExpiredOffers() {
		auto &config = IConfigManager::getInstance();
		const time_t lastExpireDate = getTimeNow() - config.getNumber(MARKET_OFFER_DURATION);

		// Rows arrive later on the dispatcher; resolve the ports then instead of capturing them.
		IMarketRepository::getInstance().loadExpiredOffers(lastExpireDate, [](std::vector<ExpiredMarketOffer> offers) {
			processExpiredOffers(g_game(), IMarketRepository::getInstance(), IPlayerRepository::getInstance(), offers);
		});

		int32_t checkExpiredMarketOffersEachMinutes = config.getNumber(CHECK_EXPIRED_MARKET_OFFERS_EACH_MINUTES);
		if (checkExpiredMarketOffersEachMinutes <= 0) {
			return;
		}

		g_dispatcher().scheduleEvent(checkExpiredMarketOffersEachMinutes * 60 * 1000, checkExpiredOffers, __FUNCTION__, DispatcherLane::Maintenance);
	}

} // namespace market_expiry
