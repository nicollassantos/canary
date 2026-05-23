/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "creatures/creatures_definitions.hpp"

#include "enums/imbuement.hpp"

class Player;
class Item;
class Imbuement;
enum Slots_t : uint8_t;

class PlayerImbuementComponent {
public:
	PlayerImbuementComponent() = delete;
	explicit PlayerImbuementComponent(Player &player) :
		m_player(player) { }

	void applyScrollImbuement(const std::shared_ptr<Item> &item, const std::shared_ptr<Item> &scrollItem);
	void createScrollImbuement(const Imbuement* imbuement);
	bool clearAllImbuements(const std::shared_ptr<Item> &item);
	void onApplyImbuement(const Imbuement* imbuement, const std::shared_ptr<Item> &item, uint8_t slot);
	void onClearImbuement(const std::shared_ptr<Item> &item, uint8_t slot);
	void openImbuementWindow(ImbuementAction action, const std::shared_ptr<Item> &item);
	void addItemImbuementStats(const Imbuement* imbuement);
	void removeItemImbuementStats(const Imbuement* imbuement);
	void updateImbuementTrackerStats() const;
	void updateDamageReductionFromItemImbuement(
		std::array<double_t, COMBAT_COUNT> &combatReductionArray, const std::shared_ptr<Item> &item, uint16_t combatTypeIndex
	) const;
	void sendImbuementResult(const std::string &message) const;
	void closeImbuementWindow() const;
	void sendInventoryImbuements(const std::map<Slots_t, std::shared_ptr<Item>> &items) const;

private:
	Player &m_player;
};
