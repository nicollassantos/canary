#include "creatures/players/components/player_creature_event_component.hpp"

#include "creatures/players/player.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/interactions/chat.hpp"
#include "creatures/npcs/npc.hpp"
#include "game/game.hpp"
#include "game/scheduling/save_manager.hpp"
#include "items/bed.hpp"
#include "creatures/players/livestream/livestream.hpp"
#include "kv/kv.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "items/item.hpp"
#include "items/containers/container.hpp"
#include "config/configmanager.hpp"
#include "core.hpp"
#include "lua/creature/movement.hpp"

void PlayerCreatureEventComponent::onUpdateTileItem(const std::shared_ptr<Tile> &updateTile, const Position &pos, const std::shared_ptr<Item> &oldItem, const ItemType &oldType, const std::shared_ptr<Item> &newItem, const ItemType &newType) {
	m_player.Creature::onUpdateTileItem(updateTile, pos, oldItem, oldType, newItem, newType);

	if (oldItem != newItem) {
		onRemoveTileItem(updateTile, pos, oldType, oldItem);
	}

	if (m_player.tradeState != TRADE_TRANSFER) {
		if (m_player.tradeItem && oldItem == m_player.tradeItem) {
			g_game().internalCloseTrade(m_player.getPlayer());
		}
	}
}

void PlayerCreatureEventComponent::onRemoveTileItem(const std::shared_ptr<Tile> &fromTile, const Position &pos, const ItemType &iType, const std::shared_ptr<Item> &item) {
	m_player.Creature::onRemoveTileItem(fromTile, pos, iType, item);

	if (m_player.tradeState != TRADE_TRANSFER) {
		m_player.checkTradeState(item);

		if (m_player.tradeItem) {
			const auto &container = item->getContainer();
			if (container && container->isHoldingItem(m_player.tradeItem)) {
				g_game().internalCloseTrade(m_player.getPlayer());
			}
		}
	}
}

void PlayerCreatureEventComponent::onCreatureAppear(const std::shared_ptr<Creature> &creature, bool isLogin) {
	m_player.Creature::onCreatureAppear(creature, isLogin);

	if (isLogin && creature == m_player.getPlayer()) {
		onEquipInventory();

		// Refresh bosstiary tracker onLogin
		m_player.refreshCyclopediaMonsterTracker(true);
		// Refresh bestiary tracker onLogin
		m_player.refreshCyclopediaMonsterTracker(false);

		for (const auto &condition : m_player.storedConditionList) {
			m_player.addCondition(condition);
		}
		m_player.storedConditionList.clear();

		m_player.updateRegeneration();

		const auto &bed = g_game().getBedBySleeper(m_player.guid);
		if (bed) {
			bed->wakeUp(m_player.getPlayer());
		}

		auto version = m_player.client->oldProtocol ? m_player.getProtocolVersion() : CLIENT_VERSION;
		g_logger().info("{} has logged in. (Protocol: {})", m_player.name, version);

		std::string livestreamPassword;
		if (auto passwordValue = m_player.kv()->scoped("livestream-system")->get("password")) {
			livestreamPassword = passwordValue->get<std::string>();
		}

		std::string livestreamDescription;
		if (auto descriptionValue = m_player.kv()->scoped("livestream-system")->get("description")) {
			livestreamDescription = descriptionValue->get<std::string>();
		}

		uint32_t livestreamRecord = 0;
		if (auto recordValue = m_player.kv()->scoped("livestream-system")->get("live-record")) {
			const auto rawRecord = recordValue->getNumber();
			if (rawRecord > 0) {
				livestreamRecord = static_cast<uint32_t>(std::min<double>(rawRecord, std::numeric_limits<uint32_t>::max()));
			}
		}
		g_livestream().setInitialState(m_player.getPlayer(), livestreamPassword, livestreamDescription, livestreamRecord);

		if (m_player.guild) {
			m_player.guild->addMember(m_player.getPlayer());
		}

		int32_t offlineTime;
		if (m_player.getLastLogout() != 0) {
			// Not counting more than 21 days to prevent overflow when multiplying with 1000 (for milliseconds).
			offlineTime = std::min<int32_t>(time(nullptr) - m_player.getLastLogout(), 86400 * 21);
		} else {
			offlineTime = 0;
		}

		for (const auto &condition : m_player.getMuteConditions()) {
			condition->setTicks(condition->getTicks() - (offlineTime * 1000));
			if (condition->getTicks() <= 0) {
				m_player.removeCondition(condition);
			}
		}

		g_game().checkPlayersRecord();
		if (m_player.getLevel() < g_configManager().getNumber(ADVENTURERSBLESSING_LEVEL) && m_player.getVocationId() > VOCATION_NONE) {
			for (uint8_t i = 2; i <= 6; i++) {
				if (!m_player.hasBlessing(i)) {
					m_player.addBlessing(i, 1);
				}
			}
			m_player.sendBlessStatus();
		}

		if (m_player.getCurrentMount() != 0) {
			m_player.toggleMount(true);
		}

		g_game().changePlayerSpeed(m_player.getPlayer(), 0);
	}
}

void PlayerCreatureEventComponent::onRemoveCreature(const std::shared_ptr<Creature> &creature, bool isLogout) {
	m_player.Creature::onRemoveCreature(creature, isLogout);

	if (const auto &player = m_player.getPlayer(); player == creature) {
		if (isLogout) {
			onDeEquipInventory();

			if (m_player.m_party) {
				m_player.m_party->leaveParty(player, true);
			}
			if (m_player.guild) {
				m_player.guild->removeMember(player);
			}

			if (m_player.isDead()) {
				m_player.loginPosition = m_player.getTemplePosition();
			} else {
				m_player.loginPosition = m_player.getPosition();
			}
			m_player.lastLogout = time(nullptr);
			g_logger().info("{} has logged out", m_player.getName());
			g_chat().removeUserFromAllChannels(player);
			m_player.clearPartyInvitations();
		}

		if (m_player.eventWalk != 0) {
			m_player.setFollowCreature(nullptr);
		}

		if (m_player.tradePartner) {
			g_game().internalCloseTrade(player);
		}

		m_player.closeShopWindow();

		g_saveManager().savePlayer(player);
	}

	if (creature == std::dynamic_pointer_cast<Creature>(m_player.shopOwner)) {
		m_player.setShopOwner(nullptr);
		m_player.sendCloseShop();
	}
}

void PlayerCreatureEventComponent::onCreatureMove(const std::shared_ptr<Creature> &creature, const std::shared_ptr<Tile> &newTile, const Position &newPos, const std::shared_ptr<Tile> &oldTile, const Position &oldPos, bool teleport) {
	m_player.Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	const auto &followCreature = m_player.getFollowCreature();
	if (m_player.hasFollowPath && (creature == followCreature || (creature.get() == m_player.getPlayer().get() && followCreature))) {
		m_player.isUpdatingPath = false;
		m_player.updateCreatureWalk();
	}

	if (m_player.shopOwner && (creature == m_player.shopOwner || creature.get() == m_player.getPlayer().get())
	    && !m_player.shopOwner->canInteract(m_player.getPosition()) && m_player.closeShopWindow()
	    && creature.get() != m_player.getPlayer().get()) {
		return;
	}

	if (creature != m_player.getPlayer()) {
		return;
	}

	if (!teleport && oldPos.z == newPos.z) {
		m_player.updateParalyzeWalkExhaust();
	}

	if (m_player.tradeState != TRADE_TRANSFER) {
		// check if we should close trade
		if (m_player.tradeItem && !Position::areInRange<1, 1, 0>(m_player.tradeItem->getPosition(), m_player.getPosition())) {
			g_game().internalCloseTrade(m_player.getPlayer());
		}

		if (m_player.tradePartner && !Position::areInRange<2, 2, 0>(m_player.tradePartner->getPosition(), m_player.getPosition())) {
			g_game().internalCloseTrade(m_player.getPlayer());
		}
	}

	// close modal windows
	if (!m_player.modalWindows.empty()) {
		// TODO: This shouldn't be hardcoded
		for (const uint32_t modalWindowId : m_player.modalWindows) {
			if (modalWindowId == std::numeric_limits<uint32_t>::max()) {
				m_player.sendTextMessage(MESSAGE_EVENT_ADVANCE, "Offline training aborted.");
				break;
			}
		}
		m_player.modalWindows.clear();
	}

	// leave market
	if (m_player.inMarket) {
		m_player.inMarket = false;
	}

	if (m_player.m_party) {
		m_player.m_party->updateSharedExperience();
		m_player.m_party->updatePlayerStatus(m_player.getPlayer(), oldPos, newPos);
	}

	if (teleport || oldPos.z != newPos.z) {
		int32_t ticks = g_configManager().getNumber(STAIRHOP_DELAY);
		if (ticks > 0) {
			if (const auto &condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_PACIFIED, ticks, 0)) {
				m_player.addCondition(condition);
			}
		}
	}
}

void PlayerCreatureEventComponent::onEquipInventory() {
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		const auto &item = m_player.inventory[slot];
		if (item) {
			item->startDecaying();
			g_moveEvents().onPlayerEquip(m_player.getPlayer(), item, static_cast<Slots_t>(slot), false);
		}
	}
}

void PlayerCreatureEventComponent::onDeEquipInventory() {
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		const auto &item = m_player.inventory[slot];
		if (item) {
			if (g_moveEvents().onPlayerDeEquip(m_player.getPlayer(), item, static_cast<Slots_t>(slot)) == 0) {
				continue;
			}
		}
	}
}

void PlayerCreatureEventComponent::onAttackedCreatureDisappear(bool isLogout) {
	m_player.sendCancelTarget();

	if (!isLogout) {
		m_player.sendTextMessage(MESSAGE_FAILURE, "Target lost.");
	}
}

void PlayerCreatureEventComponent::onFollowCreatureDisappear(bool isLogout) {
	m_player.sendCancelTarget();

	if (!isLogout) {
		m_player.sendTextMessage(MESSAGE_FAILURE, "Target lost.");
	}
}

// Container
void PlayerCreatureEventComponent::onAddContainerItem(const std::shared_ptr<Item> &item) {
	m_player.checkTradeState(item);
}

void PlayerCreatureEventComponent::onUpdateContainerItem(const std::shared_ptr<Container> &container, const std::shared_ptr<Item> &oldItem, const std::shared_ptr<Item> &newItem) {
	if (oldItem != newItem) {
		onRemoveContainerItem(container, oldItem);
	}

	if (m_player.tradeState != TRADE_TRANSFER) {
		m_player.checkTradeState(oldItem);
	}
}

void PlayerCreatureEventComponent::onRemoveContainerItem(const std::shared_ptr<Container> &container, const std::shared_ptr<Item> &item) {
	if (m_player.tradeState != TRADE_TRANSFER) {
		m_player.checkTradeState(item);

		if (m_player.tradeItem) {
			if (m_player.tradeItem->getParent() != container && container->isHoldingItem(m_player.tradeItem)) {
				g_game().internalCloseTrade(m_player.getPlayer());
			}
		}
	}
}

void PlayerCreatureEventComponent::onCloseContainer(const std::shared_ptr<Container> &container) {
	if (!m_player.client) {
		return;
	}

	for (const auto &[containerId, containerInfo] : m_player.openContainers) {
		if (containerInfo.container == container) {
			m_player.sendCloseContainer(containerId);
		}
	}
}

void PlayerCreatureEventComponent::onSendContainer(const std::shared_ptr<Container> &container) {
	if (!m_player.client || !container) {
		return;
	}

	const bool hasParent = container->hasParent();
	for (const auto &[containerId, containerInfo] : m_player.openContainers) {
		if (containerInfo.container == container) {
			m_player.sendContainer(containerId, container, hasParent, containerInfo.index);
		}
	}
}

// close container and its child containers

void PlayerCreatureEventComponent::autoCloseContainers(const std::shared_ptr<Container> &container) {
	std::vector<uint32_t> closeList;
	for (const auto &[containerId, containerInfo] : m_player.openContainers) {
		auto tmpContainer = containerInfo.container;
		while (tmpContainer) {
			if (tmpContainer->isRemoved() || tmpContainer == container) {
				closeList.emplace_back(containerId);
				break;
			}

			tmpContainer = std::dynamic_pointer_cast<Container>(tmpContainer->getParent());
		}
	}

	for (const uint32_t containerId : closeList) {
		m_player.closeContainer(containerId);
		if (m_player.client) {
			m_player.sendCloseContainer(containerId);
		}
	}
}

// inventory
// inventory

void PlayerCreatureEventComponent::onUpdateInventoryItem(const std::shared_ptr<Item> &oldItem, const std::shared_ptr<Item> &newItem) {
	if (oldItem != newItem) {
		onRemoveInventoryItem(oldItem);
	}

	if (m_player.tradeState != TRADE_TRANSFER) {
		m_player.checkTradeState(oldItem);
	}
}

void PlayerCreatureEventComponent::onRemoveInventoryItem(const std::shared_ptr<Item> &item) {
	if (m_player.tradeState != TRADE_TRANSFER) {
		m_player.checkTradeState(item);

		if (m_player.tradeItem) {
			const auto &container = item->getContainer();
			if (container && container->isHoldingItem(m_player.tradeItem)) {
				g_game().internalCloseTrade(m_player.getPlayer());
			}
		}
	}

	m_player.checkLootContainers(item->getContainer());
}