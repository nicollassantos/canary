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
#include <utility>

#include "creatures/players/components/player_forge_history.hpp"
#include "enums/forge_conversion.hpp"

class Player;
class Item;
enum ReturnValue : uint16_t;

class PlayerForgeComponent {
public:
	PlayerForgeComponent() = delete;
	explicit PlayerForgeComponent(Player &player) :
		m_player(player) { }

	// Momentum / transcendence
	void triggerMomentum();
	void triggerTranscendence();

	// Forge operations
	void forgeFuseItems(ForgeAction_t actionType, uint16_t firstItemId, uint8_t tier, uint16_t secondItemId, bool success, bool reduceTierLoss, bool convergence, uint8_t bonus, uint8_t coreCount);
	void forgeTransferItemTier(ForgeAction_t actionType, uint16_t donorItemId, uint8_t tier, uint16_t receiveItemId, bool convergence);
	void forgeResourceConversion(ForgeAction_t actionType);
	void forgeHistory(uint8_t page) const;
	void registerForgeHistoryDescription(ForgeHistory history);

	// Forge UI
	void sendOpenForge() const;
	void sendForgeError(ReturnValue returnValue) const;
	void sendForgeResult(ForgeAction_t actionType, uint16_t leftItemId, uint8_t leftTier, uint16_t rightItemId, uint8_t rightTier, bool success, uint8_t bonus, uint8_t coreCount, bool convergence) const;
	void sendForgeHistory(uint8_t page) const;
	void closeForgeWindow() const;

	// Forge resources
	void setForgeDusts(uint64_t amount);
	void addForgeDusts(uint64_t amount);
	void removeForgeDusts(uint64_t amount);
	uint64_t getForgeDusts() const;
	void addForgeDustLevel(uint64_t amount);
	void removeForgeDustLevel(uint64_t amount);
	uint64_t getForgeDustLevel() const;

	// Helpers (also exposed as Player methods for Lua/protocol)
	std::pair<uint64_t, uint64_t> getForgeSliversAndCores() const;
	std::shared_ptr<Item> getForgeItemFromId(uint16_t itemId, uint8_t tier) const;

private:
	Player &m_player;
};
