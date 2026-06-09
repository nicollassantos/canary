/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_forge_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "items/item.hpp"
#include "items/items_classification.hpp"
#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "utils/tools.hpp"

std::pair<uint64_t, uint64_t> PlayerForgeComponent::getForgeSliversAndCores() const {
	uint64_t sliverCount = 0;
	uint64_t coreCount = 0;

	// Check items from inventory
	for (const auto &item : m_player.getAllInventoryItems()) {
		if (!item) {
			continue;
		}

		sliverCount += item->getForgeSlivers();
		coreCount += item->getForgeCores();
	}

	// Check items from stash
	for (const auto &stashToSend = m_player.getStashItems();
	     const auto &[itemId, itemCount] : stashToSend) {
		if (itemId == ITEM_FORGE_SLIVER) {
			sliverCount += itemCount;
		}
		if (itemId == ITEM_FORGE_CORE) {
			coreCount += itemCount;
		}
	}

	return std::make_pair(sliverCount, coreCount);
}

std::shared_ptr<Item> PlayerForgeComponent::getForgeItemFromId(uint16_t itemId, uint8_t tier) const {
	for (const auto &item : m_player.getAllInventoryItems(true)) {
		if (!item) {
			continue;
		}
		if (item->hasImbuements()) {
			continue;
		}

		if (item->getID() == itemId && item->getTier() == tier) {
			return item;
		}
	}

	return nullptr;
}

void PlayerForgeComponent::triggerMomentum() {
	const auto &item = m_player.getInventoryItem(CONST_SLOT_HEAD);
	if (!item) {
		return;
	}

	if (!item->getTier()) {
		return;
	}

	double_t chance = item->getMomentumChance();
	const auto &playerBoots = m_player.getInventoryItem(CONST_SLOT_FEET);
	if (playerBoots && playerBoots->getTier()) {
		double_t amplificationChange = playerBoots->getAmplificationChance() / 100;
		chance *= 1 + amplificationChange;
	}

	chance += m_player.wheel().getBonusData().momentum;
	double_t randomChance = uniform_random(0, 10000) / 100.;
	if (m_player.getZoneType() != ZONE_PROTECTION && m_player.hasCondition(CONDITION_INFIGHT) && ((OTSYS_TIME() / 1000) % 2) == 0 && chance > 0 && randomChance < chance) {
		bool triggered = false;
		auto it = m_player.conditions.begin();
		while (it != m_player.conditions.end()) {
			const auto condItem = *it;
			const ConditionType_t type = condItem->getType();
			constexpr auto maxu16 = std::numeric_limits<uint16_t>::max();
			const auto checkSpellId = condItem->getSubId();
			auto spellId = checkSpellId > maxu16 ? 0u : static_cast<uint16_t>(checkSpellId);
			const int32_t ticks = condItem->getTicks();
			const int32_t newTicks = (ticks <= 2000) ? 0 : ticks - 2000;
			triggered = true;
			if (type == CONDITION_SPELLCOOLDOWN || (type == CONDITION_SPELLGROUPCOOLDOWN && spellId > SPELLGROUP_SUPPORT)) {
				condItem->setTicks(newTicks);
				type == CONDITION_SPELLGROUPCOOLDOWN ? m_player.sendSpellGroupCooldown(static_cast<SpellGroup_t>(spellId), newTicks) : m_player.sendSpellCooldown(spellId, newTicks);
			}
			++it;
		}
		if (triggered) {
			g_game().addMagicEffect(m_player.getPosition(), CONST_ME_HOURGLASS);
			m_player.sendTextMessage(MESSAGE_ATTENTION, "Momentum was triggered.");
		}
	}
}

void PlayerForgeComponent::triggerTranscendence() {
	if (m_player.wheel().getOnThinkTimer(WheelOnThink_t::AVATAR_FORGE) > OTSYS_TIME()) {
		return;
	}

	const auto &item = m_player.getInventoryItem(CONST_SLOT_LEGS);
	if (item == nullptr) {
		return;
	}

	if (!item->getTier()) {
		return;
	}

	double_t chance = item->getTranscendenceChance();
	const auto &playerBoots = m_player.getInventoryItem(CONST_SLOT_FEET);
	if (playerBoots && playerBoots->getTier()) {
		double_t amplificationChange = playerBoots->getAmplificationChance() / 100;
		chance *= 1 + amplificationChange;
	}

	const double_t randomChance = uniform_random(0, 10000) / 100.;
	if (m_player.getZoneType() != ZONE_PROTECTION && m_player.checkLastAggressiveActionWithin(2000) && ((OTSYS_TIME() / 1000) % 2) == 0 && chance > 0 && randomChance < chance) {
		int64_t duration = g_configManager().getNumber(TRANSCENDENCE_AVATAR_DURATION);
		const auto &outfitCondition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_OUTFIT, duration, 0)->static_self_cast<ConditionOutfit>();
		Outfit_t outfit;
		outfit.lookType = m_player.getVocation()->getAvatarLookType();
		outfitCondition->setOutfit(outfit);
		m_player.addCondition(outfitCondition);
		m_player.wheel().setOnThinkTimer(WheelOnThink_t::AVATAR_FORGE, OTSYS_TIME() + duration);
		g_game().addMagicEffect(m_player.getPosition(), CONST_ME_AVATAR_APPEAR);

		m_player.sendSkills();
		m_player.sendStats();
		m_player.sendBasicData();

		m_player.sendTextMessage(MESSAGE_ATTENTION, "Transcendence was triggered.");

		// Send player data after transcendence timer expire
		const auto &task = m_player.createPlayerTask(
			std::max<uint32_t>(SCHEDULER_MINTICKS, duration),
			[playerId = m_player.getID()] {
				const auto &player = g_game().getPlayerByID(playerId);
				if (player) {
					player->sendSkills();
					player->sendStats();
					player->sendBasicData();
				}
			},
			__FUNCTION__
		);
		[[maybe_unused]] auto eventId = g_dispatcher().scheduleEvent(task);

		m_player.wheel().sendGiftOfLifeCooldown();
		g_game().reloadCreature(m_player.getPlayer());
	}
}

void PlayerForgeComponent::forgeFuseItems(ForgeAction_t actionType, uint16_t firstItemId, uint8_t tier, uint16_t secondItemId, bool success, bool reduceTierLoss, bool convergence, uint8_t bonus, uint8_t coreCount) {
	if (m_player.getFreeBackpackSlots() == 0) {
		m_player.sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		return;
	}

	ForgeHistory history;
	history.actionType = actionType;
	history.tier = tier;
	history.success = success;
	history.tierLoss = reduceTierLoss;

	const auto &firstForgingItem = getForgeItemFromId(firstItemId, tier);
	if (!firstForgingItem) {
		g_logger().error("[Log 1] Player with name {} failed to fuse item with id {}", m_player.getName(), firstItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	const auto &secondForgingItem = getForgeItemFromId(secondItemId, tier);
	if (!secondForgingItem) {
		g_logger().error("[Log 2] Player with name {} failed to fuse item with id {}", m_player.getName(), secondItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	// Pre-validate all resources before mutating player inventory.
	// All parameters (convergence, success, bonus, coreCount, tier) are already
	// known, so we can compute expected costs and abort before removing anything.
	{
		const auto configKey = convergence ? FORGE_CONVERGENCE_FUSION_DUST_COST : FORGE_FUSION_DUST_COST;
		const auto dustCost = static_cast<uint64_t>(g_configManager().getNumber(configKey));

		// Dust check: convergence always spends dust; success skips only on bonus 1.
		if ((convergence || !success || bonus != 1) && getForgeDusts() < dustCost) {
			g_logger().error("[{}] Not enough dust to forge for player {}", __FUNCTION__, m_player.getName());
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		// Core check: convergence needs no cores; success skips on bonus 2.
		if (!convergence && (!success || bonus != 2) && coreCount != 0
		    && !m_player.hasItemCountById(ITEM_FORGE_CORE, coreCount, true)) {
			g_logger().error("[{}] Not enough forge cores for player {}", __FUNCTION__, m_player.getName());
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		// Gold check: only skipped when success && bonus == 3.
		if (convergence || !success || bonus != 3) {
			bool hasMatchingClassification = false;
			uint64_t preGoldCost = 0;
			for (const auto* itemClassification : g_game().getItemsClassifications()) {
				if (!itemClassification || itemClassification->id != firstForgingItem->getClassification()) {
					continue;
				}
				hasMatchingClassification = true;
				if (!itemClassification->tiers.contains(tier + 1)) {
					g_logger().error("[{}] Tier {} not found in classification {} for player {}", __FUNCTION__, tier + 1, itemClassification->id, m_player.getName());
					sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
					return;
				}
				const auto &tierPrices = itemClassification->tiers.at(tier + 1);
				preGoldCost = convergence ? tierPrices.convergenceFusionPrice : tierPrices.regularPrice;
				break;
			}
			if (!hasMatchingClassification) {
				g_logger().error("[{}] Failed to find classification {} for player {}", __FUNCTION__, firstForgingItem->getClassification(), m_player.getName());
				sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
				return;
			}
			if (m_player.getMoney() + m_player.getBankBalance() < preGoldCost) {
				g_logger().error("[{}] Not enough money to forge for player {}", __FUNCTION__, m_player.getName());
				sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
				return;
			}
		}
	}

	const auto &exaltationChest = Item::CreateItem(ITEM_EXALTATION_CHEST, 1);
	if (!exaltationChest) {
		g_logger().error("Failed to create exaltation chest");
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	const auto &exaltationContainer = exaltationChest->getContainer();
	if (!exaltationContainer) {
		g_logger().error("Failed to create exaltation container");
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	auto returnValue = g_game().internalAddItem(m_player.getPlayer(), exaltationContainer, INDEX_WHEREEVER);
	if (returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("Failed to add exaltation chest to player with name {}", m_player.getName());
		sendForgeError(returnValue);
		return;
	}

	if (returnValue = g_game().internalRemoveItem(firstForgingItem, 1);
	    returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 1] Failed to remove forge item {} from player with name {}", firstItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	if (returnValue = g_game().internalRemoveItem(secondForgingItem, 1);
	    returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 2] Failed to remove forge item {} from player with name {}", secondItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	const auto &firstForgedItem = Item::CreateItem(firstItemId, 1);
	if (!firstForgedItem) {
		g_logger().error("[Log 3] Player with name {} failed to fuse item with id {}", m_player.getName(), firstItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	returnValue = g_game().internalAddItem(exaltationContainer, firstForgedItem, INDEX_WHEREEVER);
	if (returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 1] Failed to add forge item {} from player with name {}", firstItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	auto configKey = convergence ? FORGE_CONVERGENCE_FUSION_DUST_COST : FORGE_FUSION_DUST_COST;
	auto dustCost = static_cast<uint64_t>(g_configManager().getNumber(configKey));
	if (convergence) {
		firstForgedItem->setTier(tier + 1);
		history.dustCost = dustCost;
		setForgeDusts(getForgeDusts() - dustCost);

		uint64_t cost = 0;
		for (const auto* itemClassification : g_game().getItemsClassifications()) {
			if (!itemClassification || itemClassification->id != firstForgingItem->getClassification()) {
				continue;
			}

			for (const auto &[mapTier, mapPrice] : itemClassification->tiers) {
				if (mapTier == firstForgingItem->getTier() + 1) {
					cost = mapPrice.convergenceFusionPrice;
					break;
				}
			}
			break;
		}
		if (!g_game().removeMoney(m_player.getPlayer(), cost, 0, true)) {
			g_logger().error("[{}] Failed to remove {} gold from player with name {}", __FUNCTION__, cost, m_player.getName());
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}
		g_metrics().addCounter("balance_decrease", cost, { { "player", m_player.getName() }, { "context", "forge_convergence_fuse" } });
		history.cost = cost;
	} else {
		firstForgedItem->setTier(tier);
		const auto &secondForgedItem = Item::CreateItem(secondItemId, 1);
		if (!secondForgedItem) {
			g_logger().error("[Log 4] Player with name {} failed to fuse item with id {}", m_player.getName(), secondItemId);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		secondForgedItem->setTier(tier);
		returnValue = g_game().internalAddItem(exaltationContainer, secondForgedItem, INDEX_WHEREEVER);
		if (returnValue != RETURNVALUE_NOERROR) {
			g_logger().error("[Log 2] Failed to add forge item {} from player with name {}", secondItemId, m_player.getName());
			m_player.sendCancelMessage(getReturnMessage(returnValue));
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		if (success) {
			firstForgedItem->setTier(tier + 1);

			if (bonus != 1) {
				history.dustCost = dustCost;
				setForgeDusts(getForgeDusts() - dustCost);
			}
			if (bonus != 2) {
				if (coreCount != 0 && !m_player.removeItemCountById(ITEM_FORGE_CORE, coreCount)) {
					g_logger().error("[{}][Log 1] Failed to remove item 'id :{} count: {}' from player {}", __FUNCTION__, fmt::underlying(ITEM_FORGE_CORE), coreCount, m_player.getName());
					sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
					return;
				}
				history.coresCost = coreCount;
			}
			if (bonus != 3) {
				uint64_t cost = 0;
				for (const auto* itemClassification : g_game().getItemsClassifications()) {
					if (!itemClassification || itemClassification->id != firstForgedItem->getClassification()) {
						continue;
					}
					if (!itemClassification->tiers.contains(firstForgedItem->getTier())) {
						g_logger().error("[{}] Failed to find tier {} for item {} in classification {}", __FUNCTION__, firstForgedItem->getTier(), firstForgedItem->getClassification(), itemClassification->id);
						sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
						return;
					}
					cost = itemClassification->tiers.at(firstForgedItem->getTier()).regularPrice;
					break;
				}
				if (!g_game().removeMoney(m_player.getPlayer(), cost, 0, true)) {
					g_logger().error("[{}] Failed to remove {} gold from player with name {}", __FUNCTION__, cost, m_player.getName());
					sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
					return;
				}
				g_metrics().addCounter("balance_decrease", cost, { { "player", m_player.getName() }, { "context", "forge_fuse" } });
				history.cost = cost;
			}

			if (bonus == 4) {
				if (tier > 0) {
					secondForgedItem->setTier(tier - 1);
				}
			} else if (bonus == 6) {
				secondForgedItem->setTier(tier + 1);
			} else if (bonus == 7 && tier + 2 <= firstForgedItem->getClassification()) {
				firstForgedItem->setTier(tier + 2);
			}

			if (bonus != 4 && bonus != 5 && bonus != 6 && bonus != 8) {
				returnValue = g_game().internalRemoveItem(secondForgedItem, 1);
				if (returnValue != RETURNVALUE_NOERROR) {
					g_logger().error("[Log 6] Failed to remove forge item {} from player with name {}", secondItemId, m_player.getName());
					m_player.sendCancelMessage(getReturnMessage(returnValue));
					sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
					return;
				}
			}
		} else {
			auto isTierLost = uniform_random(1, 100) <= (reduceTierLoss ? g_configManager().getNumber(FORGE_TIER_LOSS_REDUCTION) : 100);
			if (isTierLost) {
				if (secondForgedItem->getTier() >= 1) {
					secondForgedItem->setTier(tier - 1);
				} else {
					returnValue = g_game().internalRemoveItem(secondForgedItem, 1);
					if (returnValue != RETURNVALUE_NOERROR) {
						g_logger().error("[Log 7] Failed to remove forge item {} from player with name {}", secondItemId, m_player.getName());
						m_player.sendCancelMessage(getReturnMessage(returnValue));
						sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
						return;
					}
				}
			}
			bonus = (isTierLost ? 0 : 8);
			history.coresCost = coreCount;

			if (getForgeDusts() < dustCost) {
				g_logger().error("[Log 7] Failed to remove fuse dusts from player with name {}", m_player.getName());
				sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
				return;
			} else {
				setForgeDusts(getForgeDusts() - dustCost);
			}

			if (coreCount != 0 && !m_player.removeItemCountById(ITEM_FORGE_CORE, coreCount)) {
				g_logger().error("[{}][Log 2] Failed to remove item 'id: {}, count: {}' from player {}", __FUNCTION__, fmt::underlying(ITEM_FORGE_CORE), coreCount, m_player.getName());
				sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
				return;
			}

			uint64_t cost = 0;
			for (const auto* itemClassification : g_game().getItemsClassifications()) {
				if (!itemClassification || itemClassification->id != firstForgingItem->getClassification()) {
					continue;
				}
				if (!itemClassification->tiers.contains(firstForgingItem->getTier() + 1)) {
					g_logger().error("[{}] Failed to find tier {} for item {} in classification {}", __FUNCTION__, firstForgingItem->getTier() + 1, firstForgingItem->getClassification(), itemClassification->id);
					sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
					return;
				}
				cost = itemClassification->tiers.at(firstForgingItem->getTier() + 1).regularPrice;
				break;
			}
			if (!g_game().removeMoney(m_player.getPlayer(), cost, 0, true)) {
				g_logger().error("[{}] Failed to remove {} gold from player with name {}", __FUNCTION__, cost, m_player.getName());
				sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
				return;
			}
			g_metrics().addCounter("balance_decrease", cost, { { "player", m_player.getName() }, { "context", "forge_fuse" } });

			history.cost = cost;
		}
	}

	history.firstItemName = firstForgingItem->getName();
	history.secondItemName = secondForgingItem->getName();
	history.bonus = bonus;
	history.createdAt = getTimeMsNow();
	history.convergence = convergence;
	registerForgeHistoryDescription(history);

	sendForgeResult(actionType, firstItemId, tier, secondItemId, tier + 1, success, bonus, coreCount, convergence);
}

void PlayerForgeComponent::forgeTransferItemTier(ForgeAction_t actionType, uint16_t donorItemId, uint8_t tier, uint16_t receiveItemId, bool convergence) {
	if (m_player.getFreeBackpackSlots() == 0) {
		m_player.sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		return;
	}

	ForgeHistory history;
	history.actionType = actionType;
	history.tier = tier;
	history.success = true;

	const auto &donorItem = getForgeItemFromId(donorItemId, tier);
	if (!donorItem) {
		g_logger().error("[Log 1] Player with name {} failed to transfer item with id {}", m_player.getName(), donorItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	const auto &receiveItem = getForgeItemFromId(receiveItemId, 0);
	if (!receiveItem) {
		g_logger().error("[Log 2] Player with name {} failed to transfer item with id {}", m_player.getName(), receiveItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	// Pre-validate all resources before mutating player inventory.
	auto configKey = convergence ? FORGE_CONVERGENCE_TRANSFER_DUST_COST : FORGE_TRANSFER_DUST_COST;
	auto dustCost = static_cast<uint64_t>(g_configManager().getNumber(configKey));
	if (getForgeDusts() < dustCost) {
		g_logger().error("[{}] Insufficient transfer dust for player with name {}", __FUNCTION__, m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	uint8_t coresAmount = 0;
	uint64_t cost = 0;
	bool hasMatchingClassification = false;
	for (const auto* itemClassification : g_game().getItemsClassifications()) {
		if (!itemClassification || itemClassification->id != donorItem->getClassification()) {
			continue;
		}
		hasMatchingClassification = true;
		const uint8_t toTier = convergence ? donorItem->getTier() : donorItem->getTier() - 1;
		if (!itemClassification->tiers.contains(toTier)) {
			g_logger().error("[{}] Failed to find tier {} for item {} in classification {}", __FUNCTION__, toTier, donorItem->getClassification(), itemClassification->id);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}
		const auto &tierPrices = itemClassification->tiers.at(toTier);
		cost = convergence ? tierPrices.convergenceTransferPrice : tierPrices.regularPrice;
		coresAmount = tierPrices.corePrice;
		break;
	}
	if (!hasMatchingClassification) {
		g_logger().error("[{}] Failed to find classification {} for player {}", __FUNCTION__, donorItem->getClassification(), m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	if (!m_player.hasItemCountById(ITEM_FORGE_CORE, coresAmount, true)) {
		g_logger().error("[{}] Not enough forge cores for player {}", __FUNCTION__, m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	if (m_player.getMoney() + m_player.getBankBalance() < cost) {
		g_logger().error("[{}] Not enough money to transfer for player {}", __FUNCTION__, m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	// Same reasoning as forgeFuseItems: place the exaltation chest in the
	// player's inventory only after all read-only checks pass.
	const auto &exaltationChest = Item::CreateItem(ITEM_EXALTATION_CHEST, 1);
	if (!exaltationChest) {
		g_logger().error("Exaltation chest is nullptr");
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	const auto &exaltationContainer = exaltationChest->getContainer();
	if (!exaltationContainer) {
		g_logger().error("Exaltation container is nullptr");
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	auto returnValue = g_game().internalAddItem(m_player.getPlayer(), exaltationContainer, INDEX_WHEREEVER);
	if (returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("Failed to add exaltation chest to player with name {}", m_player.getName());
		sendForgeError(returnValue);
		return;
	}

	// All resources validated — begin mutations.
	if (returnValue = g_game().internalRemoveItem(donorItem, 1);
	    returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 1] Failed to remove transfer item {} from player with name {}", donorItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	if (returnValue = g_game().internalRemoveItem(receiveItem, 1);
	    returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 2] Failed to remove transfer item {} from player with name {}", receiveItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	const auto &newReceiveItem = Item::CreateItem(receiveItemId, 1);
	if (!newReceiveItem) {
		g_logger().error("[Log 6] Player with name {} failed to fuse item with id {}", m_player.getName(), receiveItemId);
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	if (convergence) {
		newReceiveItem->setTier(tier);
	} else {
		newReceiveItem->setTier(tier - 1);
	}
	returnValue = g_game().internalAddItem(exaltationContainer, newReceiveItem, INDEX_WHEREEVER);
	if (returnValue != RETURNVALUE_NOERROR) {
		g_logger().error("[Log 7] Failed to add forge item {} from player with name {}", receiveItemId, m_player.getName());
		m_player.sendCancelMessage(getReturnMessage(returnValue));
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	setForgeDusts(getForgeDusts() - dustCost);

	if (!m_player.removeItemCountById(ITEM_FORGE_CORE, coresAmount)) {
		g_logger().error("[{}] Failed to remove item 'id: {}, count: {}' from player {}", __FUNCTION__, fmt::underlying(ITEM_FORGE_CORE), coresAmount, m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}

	if (!g_game().removeMoney(m_player.getPlayer(), cost, 0, true)) {
		g_logger().error("[{}] Failed to remove {} gold from player with name {}", __FUNCTION__, cost, m_player.getName());
		sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
		return;
	}
	history.cost = cost;
	g_metrics().addCounter("balance_decrease", cost, { { "player", m_player.getName() }, { "context", "forge_transfer" } });

	history.firstItemName = Item::items[donorItemId].name;
	history.secondItemName = newReceiveItem->getName();
	history.createdAt = getTimeMsNow();
	history.convergence = convergence;
	registerForgeHistoryDescription(history);

	sendForgeResult(actionType, donorItemId, tier, receiveItemId, convergence ? tier : tier - 1, true, 0, 0, convergence);
}

void PlayerForgeComponent::forgeResourceConversion(ForgeAction_t actionType) {
	ForgeHistory history;
	history.actionType = actionType;
	history.success = true;

	ReturnValue returnValue = RETURNVALUE_NOERROR;
	if (actionType == ForgeAction_t::DUSTTOSLIVERS) {
		auto dusts = getForgeDusts();
		auto cost = static_cast<uint16_t>(g_configManager().getNumber(FORGE_COST_ONE_SLIVER) * g_configManager().getNumber(FORGE_SLIVER_AMOUNT));
		if (cost > dusts) {
			g_logger().error("[{}] Not enough dust", __FUNCTION__);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		auto itemCount = static_cast<uint16_t>(g_configManager().getNumber(FORGE_SLIVER_AMOUNT));
		const auto &item = Item::CreateItem(ITEM_FORGE_SLIVER, itemCount);
		returnValue = g_game().internalPlayerAddItem(m_player.getPlayer(), item);
		if (returnValue != RETURNVALUE_NOERROR) {
			g_logger().error("Failed to add {} slivers to player with name {}", itemCount, m_player.getName());
			m_player.sendCancelMessage(getReturnMessage(returnValue));
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}
		history.cost = cost;
		history.gained = 3;
		setForgeDusts(dusts - cost);
	} else if (actionType == ForgeAction_t::SLIVERSTOCORES) {
		const auto &[sliverCount, coreCount] = getForgeSliversAndCores();
		auto cost = static_cast<uint16_t>(g_configManager().getNumber(FORGE_CORE_COST));
		if (cost > sliverCount) {
			g_logger().error("[{}] Not enough sliver", __FUNCTION__);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		if (!m_player.removeItemCountById(ITEM_FORGE_SLIVER, cost)) {
			g_logger().error("[{}] Failed to remove item 'id: {}, count {}' from player {}", __FUNCTION__, fmt::underlying(ITEM_FORGE_SLIVER), cost, m_player.getName());
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		if (const auto &item = Item::CreateItem(ITEM_FORGE_CORE, 1);
		    item) {
			returnValue = g_game().internalPlayerAddItem(m_player.getPlayer(), item);
		}
		if (returnValue != RETURNVALUE_NOERROR) {
			g_logger().error("Failed to add one core to player with name {}", m_player.getName());
			m_player.sendCancelMessage(getReturnMessage(returnValue));
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		history.cost = cost;
		history.gained = 1;
	} else {
		auto dustLevel = getForgeDustLevel();
		if (dustLevel >= g_configManager().getNumber(FORGE_MAX_DUST)) {
			g_logger().error("[{}] Maximum level reached", __FUNCTION__);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		const auto upgradeCost = dustLevel - 75;
		if (const auto dusts = getForgeDusts();
		    upgradeCost > dusts) {
			g_logger().error("[{}] Not enough dust", __FUNCTION__);
			sendForgeError(RETURNVALUE_CONTACTADMINISTRATOR);
			return;
		}

		history.cost = upgradeCost;
		history.gained = dustLevel;
		removeForgeDusts(upgradeCost);
		addForgeDustLevel(1);
	}

	history.createdAt = getTimeMsNow();
	registerForgeHistoryDescription(history);
	m_player.sendForgingData();
}

void PlayerForgeComponent::forgeHistory(uint8_t page) const {
	sendForgeHistory(page);
}

void PlayerForgeComponent::sendOpenForge() const {
	if (m_player.client) {
		m_player.client->sendOpenForge();
	}
}

void PlayerForgeComponent::sendForgeError(ReturnValue returnValue) const {
	if (m_player.client) {
		m_player.client->sendForgeError(returnValue);
	}
}

void PlayerForgeComponent::sendForgeResult(ForgeAction_t actionType, uint16_t leftItemId, uint8_t leftTier, uint16_t rightItemId, uint8_t rightTier, bool success, uint8_t bonus, uint8_t coreCount, bool convergence) const {
	if (m_player.client) {
		m_player.client->sendForgeResult(actionType, leftItemId, leftTier, rightItemId, rightTier, success, bonus, coreCount, convergence);
	}
}

void PlayerForgeComponent::sendForgeHistory(uint8_t page) const {
	if (m_player.client) {
		m_player.client->sendForgeHistory(page);
	}
}

void PlayerForgeComponent::closeForgeWindow() const {
	if (m_player.client) {
		m_player.client->closeForgeWindow();
	}
}

void PlayerForgeComponent::setForgeDusts(uint64_t amount) {
	m_player.forgeDusts = amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.getPreyCards(), m_player.getTaskHuntingPoints(), getForgeDusts());
	}
}

void PlayerForgeComponent::addForgeDusts(uint64_t amount) {
	m_player.forgeDusts += amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.getPreyCards(), m_player.getTaskHuntingPoints(), getForgeDusts());
	}
}

void PlayerForgeComponent::removeForgeDusts(uint64_t amount) {
	m_player.forgeDusts -= std::min(amount, m_player.forgeDusts);
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.getPreyCards(), m_player.getTaskHuntingPoints(), getForgeDusts());
	}
}

uint64_t PlayerForgeComponent::getForgeDusts() const {
	return m_player.forgeDusts;
}

void PlayerForgeComponent::addForgeDustLevel(uint64_t amount) {
	m_player.forgeDustLevel += amount;
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.getPreyCards(), m_player.getTaskHuntingPoints(), getForgeDusts());
	}
}

void PlayerForgeComponent::removeForgeDustLevel(uint64_t amount) {
	m_player.forgeDustLevel -= std::min(amount, m_player.forgeDustLevel);
	if (m_player.client) {
		m_player.client->sendResourcesBalance(m_player.getMoney(), m_player.getBankBalance(), m_player.getPreyCards(), m_player.getTaskHuntingPoints(), getForgeDusts());
	}
}

uint64_t PlayerForgeComponent::getForgeDustLevel() const {
	return m_player.forgeDustLevel;
}

void PlayerForgeComponent::registerForgeHistoryDescription(ForgeHistory history) {
	std::string successfulString = history.success ? "Successful" : "Unsuccessful";
	std::string historyTierString = history.tier > 0 ? "tier - 1" : "consumed";
	std::string price = history.bonus != 3 ? formatPrice(std::to_string(history.cost), true) : "0";
	std::stringstream detailsResponse;
	auto itemId = Item::items.getItemIdByName(history.firstItemName);
	const ItemType &itemType = Item::items[itemId];
	if (history.actionType == ForgeAction_t::FUSION) {
		if (history.success) {
			detailsResponse << fmt::format(
				"{:s}{:s} <br><br>"
				"Fusion partners:"
				"<ul> "
				"<li>"
				"First item: {:s} {:s}, tier {:s}"
				"</li>"
				"<li>"
				"Second item: {:s} {:s}, tier {:s}"
				"</li>"
				"</ul>"
				"<br>"
				"Result:"
				"<ul> "
				"<li>"
				"First item: tier + 1"
				"</li>"
				"<li>"
				"Second item: {:s}"
				"</li>"
				"</ul>"
				"<br>"
				"Invested:"
				"<ul>"
				"<li>"
				"{:d} cores"
				"</li>"
				"<li>"
				"{:d} dust"
				"</li>"
				"<li>"
				"{:s} gold"
				"</li>"
				"</ul>",
				successfulString,
				history.convergence ? " (convergence)" : "",
				itemType.article, itemType.name, std::to_string(history.tier),
				itemType.article, itemType.name, std::to_string(history.tier),
				history.bonus == 8 ? "unchanged" : "consumed",
				history.coresCost, history.dustCost, price
			);
		} else {
			detailsResponse << fmt::format(
				"{:s}{:s} <br><br>"
				"Fusion partners:"
				"<ul> "
				"<li>"
				"First item: {:s} {:s}, tier {:s}"
				"</li>"
				"<li>"
				"Second item: {:s} {:s}, tier {:s}"
				"</li>"
				"</ul>"
				"<br>"
				"Result:"
				"<ul> "
				"<li>"
				"First item: unchanged"
				"</li>"
				"<li>"
				"Second item: {:s}"
				"</li>"
				"</ul>"
				"<br>"
				"Invested:"
				"<ul>"
				"<li>"
				"{:d} cores"
				"</li>"
				"<li>"
				"100 dust"
				"</li>"
				"<li>"
				"{:s} gold"
				"</li>"
				"</ul>",
				successfulString,
				history.convergence ? " (convergence)" : "",
				itemType.article, itemType.name, std::to_string(history.tier),
				itemType.article, itemType.name, std::to_string(history.tier),
				history.bonus == 8 ? "unchanged" : historyTierString,
				history.coresCost, price
			);
		}
	} else if (history.actionType == ForgeAction_t::TRANSFER) {
		detailsResponse << fmt::format(
			"{:s}{:s} <br><br>"
			"Transfer partners:"
			"<ul> "
			"<li>"
			"First item: {:s} {:s}, tier {:s}"
			"</li>"
			"<li>"
			"Second item: {:s} {:s}, tier {:s}"
			"</li>"
			"</ul>"
			"<br>"
			"Result:"
			"<ul> "
			"<li>"
			"First item: {:s} {:s}, tier {:s}"
			"</li>"
			"<li>"
			"Second item: {:s} {:s}, {:s}"
			"</li>"
			"</ul>"
			"<br>"
			"Invested:"
			"<ul>"
			"<li>"
			"1 cores"
			"</li>"
			"<li>"
			"100 dust"
			"</li>"
			"<li>"
			"{:s} gold"
			"</li>"
			"</ul>",
			successfulString,
			history.convergence ? " (convergence)" : "",
			itemType.article, itemType.name, std::to_string(history.tier),
			itemType.article, itemType.name, std::to_string(history.tier),
			itemType.article, itemType.name, std::to_string(history.tier),
			itemType.article, itemType.name, std::to_string(history.tier),
			price
		);
	} else if (history.actionType == ForgeAction_t::DUSTTOSLIVERS) {
		detailsResponse << fmt::format("Converted {:d} dust to {:d} slivers.", history.cost, history.gained);
	} else if (history.actionType == ForgeAction_t::SLIVERSTOCORES) {
		history.actionType = ForgeAction_t::DUSTTOSLIVERS;
		detailsResponse << fmt::format("Converted {:d} slivers to {:d} exalted core.", history.cost, history.gained);
	} else if (history.actionType == ForgeAction_t::INCREASELIMIT) {
		history.actionType = ForgeAction_t::DUSTTOSLIVERS;
		detailsResponse << fmt::format("Spent {:d} dust to increase the dust limit to {:d}.", history.cost, history.gained + 1);
	} else {
		detailsResponse << "(unknown)";
	}

	history.description = detailsResponse.str();

	m_player.forgeHistory().add(history);
}
