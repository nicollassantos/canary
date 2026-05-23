/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_imbuement_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/item.hpp"
#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "server/network/protocol/protocolgame.hpp"

namespace {
	[[nodiscard]] bool hasMatchingImbuementInOtherSlot(const std::shared_ptr<Item> &item, uint8_t ignoredSlot, const Imbuement* imbuement) {
		for (uint8_t slot = 0; slot < item->getImbuementSlot(); slot++) {
			if (slot == ignoredSlot) {
				continue;
			}
			ImbuementInfo existingImbuement;
			if (!item->getImbuementInfo(slot, &existingImbuement) || !existingImbuement.imbuement) {
				continue;
			}
			if (existingImbuement.imbuement->getName() == imbuement->getName()) {
				return true;
			}
		}
		return false;
	}
} // namespace

void PlayerImbuementComponent::applyScrollImbuement(const std::shared_ptr<Item> &item, const std::shared_ptr<Item> &scrollItem) {
	if (!item || !scrollItem) {
		return;
	}

	const auto freeImbuementSlot = item->getFreeImbuementSlot();
	if (freeImbuementSlot < 0) {
		return;
	}

	const Imbuement* imbuement = g_imbuements().getImbuementByScrollID(scrollItem->getID());
	if (!imbuement) {
		return;
	}

	const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());
	if (!baseImbuement) {
		return;
	}

	const auto &thisPlayer = m_player.getPlayer();
	if (!item->canAddImbuement(static_cast<uint8_t>(freeImbuementSlot), thisPlayer, imbuement)) {
		return;
	}

	if (g_game().internalRemoveItem(scrollItem, 1) != RETURNVALUE_NOERROR) {
		g_logger().error("[PlayerImbuementComponent::applyScrollImbuement] - Failed to remove scroll item {} from player {}", scrollItem->getID(), m_player.getName());
		return;
	}

	item->setImbuement(freeImbuementSlot, imbuement->getID(), baseImbuement->duration);
	g_imbuementDecay().startImbuementDecay(item);

	if (item->getParent() == m_player.getPlayer()) {
		addItemImbuementStats(imbuement);
	}
}

void PlayerImbuementComponent::createScrollImbuement(const Imbuement* imbuement) {
	if (!imbuement) {
		return;
	}

	const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());
	if (!baseImbuement) {
		return;
	}

	const auto &items = imbuement->getItems();
	for (auto &[itemId, amount] : items) {
		const auto playerItemAmount = m_player.getItemTypeCount(itemId) + m_player.getStashItemCount(itemId);
		if (playerItemAmount < amount) {
			m_player.sendImbuementResult("You don't have all necessary items.");
			return;
		}
	}

	auto emptyScrollsCount = m_player.getItemTypeCount(ITEM_EMPTY_IMBUEMENT_SCROLL) + m_player.getStashItemCount(ITEM_EMPTY_IMBUEMENT_SCROLL);
	if (emptyScrollsCount == 0) {
		m_player.sendImbuementResult("You don't have all necessary items.");
		return;
	}

	uint32_t price = baseImbuement->price;
	if (!g_game().removeMoney(m_player.getPlayer(), price, 0, true)) {
		const std::string message = fmt::format("You don't have {} gold coins.", price);

		g_logger().error("[PlayerImbuementComponent::onApplyImbuement] - An error occurred while player with name {} try to apply imbuement, player do not have money", m_player.getName());
		m_player.sendImbuementResult(message);
		return;
	}

	g_metrics().addCounter("balance_decrease", price, { { "player", m_player.getName() }, { "context", "apply_imbuement" } });

	for (const auto &[key, value] : items) {
		const uint32_t inventoryItemCount = m_player.getItemTypeCount(key);
		if (inventoryItemCount >= value) {
			if (!m_player.removeItemOfType(key, value, -1, true)) {
				g_logger().error("[PlayerImbuementComponent::createScrollImbuement] - Failed to remove {}x item {} from player {}", value, key, m_player.getName());
			}
			continue;
		}

		uint32_t mathItemCount = value;
		if (inventoryItemCount > 0 && m_player.removeItemOfType(key, inventoryItemCount, -1, false)) {
			mathItemCount = mathItemCount - inventoryItemCount;
		}

		const ItemType &itemType = Item::items[key];

		const auto withdrawItemMessage = fmt::format("Using {}x {} from your stash.", mathItemCount, itemType.name);
		m_player.withdrawItem(itemType.id, mathItemCount);
		m_player.sendTextMessage(MESSAGE_STATUS, withdrawItemMessage);
	}

	if (!m_player.removeItemCountById(ITEM_EMPTY_IMBUEMENT_SCROLL, 1, true)) {
		g_logger().error("Failed to remove empty imbuement scroll from player with name {}", m_player.getName());
		return;
	}

	const auto imbuementScrollId = imbuement->getScrollItemID();
	const auto &imbuementScroll = Item::CreateItem(imbuementScrollId, 1);
	ReturnValue ret = g_game().internalAddItem(m_player.getPlayer(), imbuementScroll, INDEX_WHEREEVER, 0);
	if (ret != RETURNVALUE_NOERROR) {
		g_logger().error("Failed to add imbuement scroll id '{}' to player with name {}", imbuementScrollId, m_player.getName());
		m_player.sendImbuementResult("Failed to add imbuement scroll. Please contact administration.");
		return;
	}

	m_player.openImbuementWindow(ImbuementAction::Scroll, nullptr);
}

bool PlayerImbuementComponent::clearAllImbuements(const std::shared_ptr<Item> &item) {
	if (!item) {
		return false;
	}

	auto itemSlots = item->getImbuementSlot();
	if (itemSlots == 0) {
		m_player.sendTextMessage(MESSAGE_FAILURE, "Sorry, not possible.");
		return false;
	}

	std::vector<std::pair<uint8_t, ImbuementInfo>> imbuementsToRemove;

	for (uint8_t slot = 0; slot < itemSlots; slot++) {
		ImbuementInfo imbuementInfo;
		if (item->getImbuementInfo(slot, &imbuementInfo) && imbuementInfo.imbuement) {
			(void)imbuementsToRemove.emplace_back(slot, imbuementInfo);
		}
	}

	if (imbuementsToRemove.empty()) {
		m_player.sendTextMessage(MESSAGE_FAILURE, "Sorry, not possible.");
		return false;
	}

	for (const auto &[slot, imbuementInfo] : imbuementsToRemove) {
		if (item->getParent() == m_player.getPlayer()) {
			removeItemImbuementStats(imbuementInfo.imbuement);
		}
		item->clearImbuement(slot, imbuementInfo.imbuement->getID());
	}

	return true;
}

void PlayerImbuementComponent::onApplyImbuement(const Imbuement* imbuement, const std::shared_ptr<Item> &item, uint8_t slot) {
	if (!imbuement || !item) {
		return;
	}

	const auto &thisPlayer = m_player.getPlayer();
	if (!item->canAddImbuement(slot, thisPlayer, imbuement)) {
		return;
	}

	ImbuementInfo imbuementInfo;
	if (item->getImbuementInfo(slot, &imbuementInfo)) {
		g_logger().error("[PlayerImbuementComponent::onApplyImbuement] - An error occurred while player with name {} try to apply imbuement, item already contains imbuement", m_player.getName());
		m_player.sendImbuementResult("An error occurred, please reopen imbuement window.");
		return;
	}

	if (hasMatchingImbuementInOtherSlot(item, slot, imbuement)) {
		g_logger().error("[PlayerImbuementComponent::onApplyImbuement] - Player {} attempted to apply the same imbuement in multiple slots", m_player.getName());
		m_player.sendImbuementResult("You cannot apply the same imbuement in multiple slots.");
		return;
	}

	const auto &items = imbuement->getItems();
	for (auto &[key, value] : items) {
		const ItemType &itemType = Item::items[key];
		if (m_player.getItemTypeCount(key) + m_player.getStashItemCount(itemType.id) < value) {
			m_player.sendImbuementResult("You don't have all necessary items.");
			return;
		}
	}

	const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());
	if (!baseImbuement) {
		return;
	}

	uint32_t price = baseImbuement->price;

	if (!g_game().removeMoney(thisPlayer, price, 0, true)) {
		const std::string message = fmt::format("You don't have {} gold coins.", price);

		g_logger().error("[PlayerImbuementComponent::onApplyImbuement] - An error occurred while player with name {} try to apply imbuement, player do not have money", m_player.getName());
		m_player.sendImbuementResult(message);
		return;
	}

	g_metrics().addCounter("balance_decrease", price, { { "player", m_player.getName() }, { "context", "apply_imbuement" } });

	for (auto &[key, value] : items) {
		std::stringstream withdrawItemMessage;

		const uint32_t inventoryItemCount = m_player.getItemTypeCount(key);
		if (inventoryItemCount >= value) {
			if (!m_player.removeItemOfType(key, value, -1, true)) {
				g_logger().error("[PlayerImbuementComponent::onApplyImbuement] - Failed to remove {}x item {} from player {}", value, key, m_player.getName());
			}
			continue;
		}

		uint32_t mathItemCount = value;
		if (inventoryItemCount > 0 && m_player.removeItemOfType(key, inventoryItemCount, -1, false)) {
			mathItemCount = mathItemCount - inventoryItemCount;
		}

		const ItemType &itemType = Item::items[key];

		withdrawItemMessage << "Using " << mathItemCount << "x " << itemType.name << " from your stash. ";
		(void)m_player.withdrawItem(itemType.id, mathItemCount);
		m_player.sendTextMessage(MESSAGE_STATUS, withdrawItemMessage.str());
	}

	// Update imbuement stats item if the item is equipped
	if (item->getParent() == thisPlayer) {
		ImbuementInfo oldImb;
		if (item->getImbuementInfo(slot, &oldImb) && oldImb.imbuement) {
			removeItemImbuementStats(oldImb.imbuement);
		}

		addItemImbuementStats(imbuement);
	}
	item->setImbuement(slot, imbuement->getID(), baseImbuement->duration);
	g_imbuementDecay().startImbuementDecay(item);

	m_player.openImbuementWindow(ImbuementAction::PickItem, item);
}

void PlayerImbuementComponent::onClearImbuement(const std::shared_ptr<Item> &item, uint8_t slot) {
	if (!item) {
		return;
	}

	ImbuementInfo imbuementInfo;
	if (!item->getImbuementInfo(slot, &imbuementInfo)) {
		g_logger().error("[PlayerImbuementComponent::onClearImbuement] - An error occurred while player with name {} try to apply imbuement, item not contains imbuement", m_player.getName());
		m_player.sendImbuementResult("An error occurred, please reopen imbuement window.");
		return;
	}

	const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuementInfo.imbuement->getBaseID());
	if (!baseImbuement) {
		return;
	}

	if (!g_game().removeMoney(m_player.getPlayer(), baseImbuement->removeCost, 0, true)) {
		const std::string message = fmt::format("You don't have {} gold coins.", baseImbuement->removeCost);

		g_logger().error("[PlayerImbuementComponent::onClearImbuement] - An error occurred while player with name {} try to apply imbuement, player do not have money", m_player.getName());
		m_player.sendImbuementResult(message);
		m_player.openImbuementWindow(ImbuementAction::PickItem, item);
		return;
	}
	g_metrics().addCounter("balance_decrease", baseImbuement->removeCost, { { "player", m_player.getName() }, { "context", "clear_imbuement" } });

	if (item->getParent() == m_player.getPlayer()) {
		removeItemImbuementStats(imbuementInfo.imbuement);
	}

	item->clearImbuement(slot, imbuementInfo.imbuement->getID());
	m_player.openImbuementWindow(ImbuementAction::PickItem, item);
}

void PlayerImbuementComponent::openImbuementWindow(ImbuementAction action, const std::shared_ptr<Item> &item) {
	if (!m_player.client) {
		return;
	}

	updateImbuementTrackerStats();

	m_player.client->openImbuementWindow(action, item);
}

void PlayerImbuementComponent::addItemImbuementStats(const Imbuement* imbuement) {
	if (!imbuement) {
		return;
	}

	bool requestUpdate = false;
	// Check imbuement skills
	for (int32_t skill = SKILL_FIRST; skill <= SKILL_LAST; ++skill) {
		if (imbuement->skills[skill]) {
			requestUpdate = true;
			m_player.setVarSkill(static_cast<skills_t>(skill), imbuement->skills[skill]);
		}
	}

	// Check imbuement magic level
	for (int32_t stat = STAT_FIRST; stat <= STAT_LAST; ++stat) {
		if (imbuement->stats[stat]) {
			requestUpdate = true;
			m_player.setVarStats(static_cast<stats_t>(stat), imbuement->stats[stat]);
		}
	}

	// Add imbuement speed
	if (imbuement->speed != 0) {
		g_game().changeSpeed(m_player.getPlayer(), imbuement->speed);
	}

	// Add imbuement capacity
	if (imbuement->capacity != 0) {
		requestUpdate = true;
		m_player.bonusCapacity = (m_player.capacity * imbuement->capacity) / 100;
	}

	if (requestUpdate) {
		m_player.sendStats();
		m_player.sendSkills();
	}
}

void PlayerImbuementComponent::removeItemImbuementStats(const Imbuement* imbuement) {
	if (!imbuement) {
		return;
	}

	bool requestUpdate = false;

	for (int32_t skill = SKILL_FIRST; skill <= SKILL_LAST; ++skill) {
		if (imbuement->skills[skill]) {
			requestUpdate = true;
			m_player.setVarSkill(static_cast<skills_t>(skill), -imbuement->skills[skill]);
		}
	}

	// Check imbuement magic level
	for (int32_t stat = STAT_FIRST; stat <= STAT_LAST; ++stat) {
		if (imbuement->stats[stat]) {
			requestUpdate = true;
			m_player.setVarStats(static_cast<stats_t>(stat), -imbuement->stats[stat]);
		}
	}

	// Remove imbuement speed
	if (imbuement->speed != 0) {
		g_game().changeSpeed(m_player.getPlayer(), -imbuement->speed);
	}

	// Remove imbuement capacity
	if (imbuement->capacity != 0) {
		requestUpdate = true;
		m_player.bonusCapacity = 0;
	}

	if (requestUpdate) {
		m_player.sendStats();
		m_player.sendSkills();
	}
}

void PlayerImbuementComponent::updateImbuementTrackerStats() const {
	if (!m_player.imbuementTrackerWindowOpen) {
		if (m_player.m_pendingImbuementTrackerEventId != 0) {
			g_dispatcher().stopEvent(m_player.m_pendingImbuementTrackerEventId);
			m_player.m_pendingImbuementTrackerEventId = 0;
		}
		m_player.m_hasPendingImbuementTrackerUpdate = false;
		return;
	}

	const int64_t currentTime = OTSYS_TIME();
	const int64_t elapsed = currentTime - m_player.m_lastImbuementTrackerUpdate;
	if (elapsed < 1000) {
		if (!m_player.m_hasPendingImbuementTrackerUpdate) {
			m_player.m_hasPendingImbuementTrackerUpdate = true;
			const uint32_t delay = std::max<uint32_t>(static_cast<uint32_t>(1000 - elapsed), SCHEDULER_MINTICKS);
			m_player.m_pendingImbuementTrackerEventId = g_dispatcher().scheduleEvent(
				delay,
				[playerId = m_player.getID()] {
					const auto &player = g_game().getPlayerByID(playerId);
					if (!player || player->isRemoved()) {
						return;
					}

					player->m_hasPendingImbuementTrackerUpdate = false;
					player->m_pendingImbuementTrackerEventId = 0;
					player->updateImbuementTrackerStats();
				},
				__FUNCTION__
			);
		}
		return;
	}

	m_player.m_lastImbuementTrackerUpdate = currentTime;
	g_game().playerRequestInventoryImbuements(m_player.getID(), true);
}

void PlayerImbuementComponent::updateDamageReductionFromItemImbuement(
	std::array<double_t, COMBAT_COUNT> &combatReductionArray, const std::shared_ptr<Item> &item, uint16_t combatTypeIndex
) const {
	for (uint8_t imbueSlotId = 0; imbueSlotId < item->getImbuementSlot(); imbueSlotId++) {
		ImbuementInfo imbuementInfo;
		if (item->getImbuementInfo(imbueSlotId, &imbuementInfo) && imbuementInfo.imbuement) {
			const int16_t imbuementAbsorption = imbuementInfo.imbuement->absorbPercent[combatTypeIndex];
			if (imbuementAbsorption != 0) {
				combatReductionArray[combatTypeIndex] = m_player.calculateDamageReduction(combatReductionArray[combatTypeIndex], imbuementAbsorption);
			}
		}
	}
}

void PlayerImbuementComponent::sendImbuementResult(const std::string &message) const {
	if (m_player.client) {
		m_player.client->sendImbuementResult(message);
	}
}

void PlayerImbuementComponent::closeImbuementWindow() const {
	if (m_player.client) {
		m_player.client->closeImbuementWindow();
	}
}

void PlayerImbuementComponent::sendInventoryImbuements(const std::map<Slots_t, std::shared_ptr<Item>> &items) const {
	if (m_player.client) {
		m_player.client->sendInventoryImbuements(items);
	}
}