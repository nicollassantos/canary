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
class Player;
class Item;
struct Position;
enum ReturnValue : uint16_t;
enum Slots_t : uint8_t;

class TradeService {
public:
	TradeService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerRequestTrade(uint32_t playerId, const Position &pos, uint8_t stackPos, uint32_t tradePlayerId, uint16_t itemId);
	bool internalStartTrade(const std::shared_ptr<Player> &player, const std::shared_ptr<Player> &partner, const std::shared_ptr<Item> &tradeItem);
	void internalCloseTrade(const std::shared_ptr<Player> &player);

	void playerAcceptTrade(uint32_t playerId);
	void playerLookInTrade(uint32_t playerId, bool lookAtCounterOffer, uint8_t index);
	void playerCloseTrade(uint32_t playerId);
	void playerBuyItem(uint32_t playerId, uint16_t itemId, uint8_t count, uint16_t amount, bool ignoreCap = false, bool inBackpacks = false);
	void playerSellItem(uint32_t playerId, uint16_t itemId, uint8_t count, uint16_t amount, bool ignoreEquipped = false);
	void playerCloseShop(uint32_t playerId);
	void playerLookInShop(uint32_t playerId, uint16_t itemId, uint8_t count);

	static std::string getTradeErrorDescription(ReturnValue ret, const std::shared_ptr<Item> &item);

private:
	Game &game_;
	IConfigManager &config_;
};
