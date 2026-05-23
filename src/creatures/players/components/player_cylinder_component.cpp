/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_cylinder_component.hpp"

#include "config/configmanager.hpp"
#include "creatures/players/player.hpp"
#include "utils/tools.hpp"
#include "lua/creature/movement.hpp"
#include "items/item.hpp"
#include "items/containers/container.hpp"

ReturnValue PlayerCylinderComponent::queryAdd(int32_t index, const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t flags, const std::shared_ptr<Creature> &) const {
	const auto &item = thing->getItem();
	if (item == nullptr) {
		g_logger().error("[Player::queryAdd] - Item is nullptr");
		return RETURNVALUE_NOTPOSSIBLE;
	}
	if (item->hasOwner() && !item->isOwner(m_player.getPlayer())) {
		return RETURNVALUE_ITEMISNOTYOURS;
	}

	const bool childIsOwner = hasBitSet(FLAG_CHILDISOWNER, flags);
	if (childIsOwner) {
		// a child container is querying the player, just check if enough capacity
		const bool skipLimit = hasBitSet(FLAG_NOLIMIT, flags);
		if (skipLimit || m_player.hasCapacity(item, count)) {
			return RETURNVALUE_NOERROR;
		}
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	if (!item->isPickupable()) {
		return RETURNVALUE_CANNOTPICKUP;
	}

	ReturnValue ret = RETURNVALUE_NOERROR;

	const int32_t &slotPosition = item->getSlotPosition();

	bool allowPutItemsOnAmmoSlot = g_configManager().getBoolean(ENABLE_PLAYER_PUT_ITEM_IN_AMMO_SLOT);
	if (allowPutItemsOnAmmoSlot && index == CONST_SLOT_AMMO) {
		ret = RETURNVALUE_NOERROR;
	} else {
		if ((slotPosition & SLOTP_HEAD) || (slotPosition & SLOTP_NECKLACE) || (slotPosition & SLOTP_BACKPACK) || (slotPosition & SLOTP_ARMOR) || (slotPosition & SLOTP_LEGS) || (slotPosition & SLOTP_FEET) || (slotPosition & SLOTP_RING)) {
			ret = RETURNVALUE_CANNOTBEDRESSED;
		} else if (slotPosition & SLOTP_TWO_HAND) {
			ret = RETURNVALUE_PUTTHISOBJECTINBOTHHANDS;
		} else if ((slotPosition & SLOTP_RIGHT) || (slotPosition & SLOTP_LEFT)) {
			ret = RETURNVALUE_CANNOTBEDRESSED;
		}
	}

	switch (index) {
		case CONST_SLOT_HEAD: {
			if (slotPosition & SLOTP_HEAD) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_NECKLACE: {
			if (slotPosition & SLOTP_NECKLACE) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_BACKPACK: {
			if (slotPosition & SLOTP_BACKPACK) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_ARMOR: {
			if (slotPosition & SLOTP_ARMOR) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RIGHT: {
			if (slotPosition & SLOTP_RIGHT) {
				if (item->getWeaponType() != WEAPON_SHIELD && !item->isQuiver()) {
					ret = RETURNVALUE_CANNOTBEDRESSED;
				} else {
					const auto &leftItem = m_player.inventory[CONST_SLOT_LEFT];
					if (leftItem) {
						if ((leftItem->getSlotPosition() | slotPosition) & SLOTP_TWO_HAND) {
							if (item->isQuiver() && leftItem->getWeaponType() == WEAPON_DISTANCE) {
								ret = RETURNVALUE_NOERROR;
							} else {
								ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
							}
						} else {
							ret = RETURNVALUE_NOERROR;
						}
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				}
			} else if (slotPosition & SLOTP_TWO_HAND) {
				ret = RETURNVALUE_CANNOTBEDRESSED;
			} else if (m_player.inventory[CONST_SLOT_LEFT]) {
				const auto &leftItem = m_player.inventory[CONST_SLOT_LEFT];
				const WeaponType_t type = item->getWeaponType();
				const WeaponType_t leftType = leftItem ? leftItem->getWeaponType() : WEAPON_NONE;
				if (leftItem && leftItem->getSlotPosition() & SLOTP_TWO_HAND) {
					ret = RETURNVALUE_DROPTWOHANDEDITEM;
				} else if (leftItem && item == leftItem && count == item->getItemCount()) {
					ret = RETURNVALUE_NOERROR;
				} else if (leftType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
					ret = RETURNVALUE_CANONLYUSEONESHIELD;
				} else if (leftType == WEAPON_NONE || type == WEAPON_NONE || leftType == WEAPON_SHIELD || leftType == WEAPON_AMMO || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
					ret = RETURNVALUE_NOERROR;
				} else {
					ret = RETURNVALUE_CANONLYUSEONEWEAPON;
				}
			} else {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_LEFT: {
			if (item->isQuiver()) {
				ret = RETURNVALUE_CANNOTBEDRESSED;
			} else if (slotPosition & SLOTP_TWO_HAND) {
				if (m_player.inventory[CONST_SLOT_RIGHT]) {
					const WeaponType_t type = item->getWeaponType();
					// Allow equip bow when quiver is in SLOT_RIGHT
					if (type == WEAPON_DISTANCE && m_player.inventory[CONST_SLOT_RIGHT]->isQuiver()) {
						ret = RETURNVALUE_NOERROR;
					} else {
						ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
					}
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			} else if (slotPosition & SLOTP_LEFT) {
				const WeaponType_t type = item->getWeaponType();
				if (type == WEAPON_NONE || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
					ret = RETURNVALUE_CANNOTBEDRESSED;
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			} else if (m_player.inventory[CONST_SLOT_RIGHT]) {
				const auto &rightItem = m_player.inventory[CONST_SLOT_RIGHT];
				const WeaponType_t type = item->getWeaponType();
				const WeaponType_t rightType = rightItem ? rightItem->getWeaponType() : WEAPON_NONE;

				if (rightItem && rightItem->getSlotPosition() & SLOTP_TWO_HAND) {
					ret = RETURNVALUE_DROPTWOHANDEDITEM;
				} else if (rightItem && item == rightItem && count == item->getItemCount()) {
					ret = RETURNVALUE_NOERROR;
				} else if (rightType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
					ret = RETURNVALUE_CANONLYUSEONESHIELD;
				} else if (rightType == WEAPON_NONE || type == WEAPON_NONE || rightType == WEAPON_SHIELD || rightType == WEAPON_AMMO || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
					ret = RETURNVALUE_NOERROR;
				} else {
					ret = RETURNVALUE_CANONLYUSEONEWEAPON;
				}
			} else {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_LEGS: {
			if (slotPosition & SLOTP_LEGS) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_FEET: {
			if (slotPosition & SLOTP_FEET) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RING: {
			if (slotPosition & SLOTP_RING) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_AMMO: {
			if (allowPutItemsOnAmmoSlot) {
				ret = RETURNVALUE_NOERROR;
			} else {
				if ((slotPosition & SLOTP_AMMO)) {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_WHEREEVER:
		case -1:
			ret = RETURNVALUE_NOTENOUGHROOM;
			break;

		default:
			ret = RETURNVALUE_NOTPOSSIBLE;
			break;
	}

	if (ret == RETURNVALUE_NOERROR || ret == RETURNVALUE_NOTENOUGHROOM) {
		// need an exchange with source?
		const auto &inventoryItem = m_player.getInventoryItem(static_cast<Slots_t>(index));
		if (inventoryItem && (!inventoryItem->isStackable() || inventoryItem->getID() != item->getID())) {
			return RETURNVALUE_NEEDEXCHANGE;
		}

		// check if enough capacity
		if (!m_player.hasCapacity(item, count)) {
			return RETURNVALUE_NOTENOUGHCAPACITY;
		}

		if (!g_moveEvents().onPlayerEquip(m_player.getPlayer(), item, static_cast<Slots_t>(index), true)) {
			return RETURNVALUE_CANNOTBEDRESSED;
		}
	}

	return ret;
}

ReturnValue PlayerCylinderComponent::queryMaxCount(int32_t index, const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t &maxQueryCount, uint32_t flags) {
	const auto &item = thing->getItem();
	if (item == nullptr) {
		maxQueryCount = 0;
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (index == INDEX_WHEREEVER) {
		uint32_t totalSpace = 0;
		for (int32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			const auto &inventoryItem = m_player.inventory[slotIndex];
			if (inventoryItem) {
				if (const auto &subContainer = inventoryItem->getContainer()) {
					uint32_t queryCount = 0;
					subContainer->queryMaxCount(INDEX_WHEREEVER, item, item->getItemCount(), queryCount, flags);
					totalSpace += queryCount;

					// iterate through all items, including sub-containers (deep search)
					for (ContainerIterator it = subContainer->iterator(); it.hasNext(); it.advance()) {
						if (const auto &tmpContainer = (*it)->getContainer()) {
							queryCount = 0;
							tmpContainer->queryMaxCount(INDEX_WHEREEVER, item, item->getItemCount(), queryCount, flags);
							totalSpace += queryCount;
						}
					}
				} else if (inventoryItem->isStackable() && item->equals(inventoryItem) && inventoryItem->getItemCount() < inventoryItem->getStackSize()) {
					const uint32_t remainder = (inventoryItem->getStackSize() - inventoryItem->getItemCount());

					if (queryAdd(slotIndex, item, remainder, flags, nullptr) == RETURNVALUE_NOERROR) {
						totalSpace += remainder;
					}
				}
			} else if (queryAdd(slotIndex, item, item->getItemCount(), flags, nullptr) == RETURNVALUE_NOERROR) { // empty slot
				if (item->isStackable()) {
					totalSpace += item->getStackSize();
				} else {
					++totalSpace;
				}
			}
		}

		maxQueryCount = totalSpace;
	} else {
		std::shared_ptr<Item> destItem = nullptr;

		const auto &destThing = m_player.getThing(index);
		if (destThing) {
			destItem = destThing->getItem();
		}

		if (destItem) {
			if (destItem->isStackable() && item->equals(destItem) && destItem->getItemCount() < destItem->getStackSize()) {
				maxQueryCount = destItem->getStackSize() - destItem->getItemCount();
			} else {
				maxQueryCount = 0;
			}
		} else if (queryAdd(index, item, count, flags, nullptr) == RETURNVALUE_NOERROR) { // empty slot
			if (item->isStackable()) {
				maxQueryCount = item->getStackSize();
			} else {
				maxQueryCount = 1;
			}

			return RETURNVALUE_NOERROR;
		}
	}

	if (maxQueryCount < count) {
		return RETURNVALUE_NOTENOUGHROOM;
	}
	return RETURNVALUE_NOERROR;
}

ReturnValue PlayerCylinderComponent::queryRemove(const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t flags, const std::shared_ptr<Creature> & /*= nullptr */) const {
	const int32_t index = getThingIndex(thing);
	if (index == -1) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	const auto &item = thing->getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == 0 || (item->isStackable() && count > item->getItemCount())) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isMovable() && !hasBitSet(FLAG_IGNORENOTMOVABLE, flags)) {
		return RETURNVALUE_NOTMOVABLE;
	}

	return RETURNVALUE_NOERROR;
}

std::shared_ptr<Cylinder> PlayerCylinderComponent::queryDestination(int32_t &index, const std::shared_ptr<Thing> &thing, std::shared_ptr<Item> &destItem, uint32_t &flags) {
	const auto &thisPlayer = m_player.getPlayer();
	if (index == 0 /*drop to capacity window*/ || index == INDEX_WHEREEVER) {
		destItem = nullptr;

		const auto &item = thing->getItem();
		if (!item) {
			return thisPlayer;
		}

		const bool autoStack = !(flags & FLAG_IGNOREAUTOSTACK);
		const bool isStackable = item->isStackable();

		std::vector<std::shared_ptr<Container>> containers;

		for (uint32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_AMMO; ++slotIndex) {
			std::shared_ptr<Item> inventoryItem = m_player.inventory[slotIndex];
			if (inventoryItem) {
				if (inventoryItem == m_player.tradeItem) {
					continue;
				}

				if (inventoryItem == item) {
					continue;
				}

				if (autoStack && isStackable) {
					// try find an already existing item to stack with
					if (queryAdd(slotIndex, item, item->getItemCount(), 0, nullptr) == RETURNVALUE_NOERROR) {
						if (inventoryItem->equals(item) && inventoryItem->getItemCount() < inventoryItem->getStackSize()) {
							index = slotIndex;
							destItem = inventoryItem;
							return thisPlayer;
						}
					}

					if (const auto &subContainer = inventoryItem->getContainer()) {
						containers.push_back(subContainer);
					}
				} else if (const auto &subContainer = inventoryItem->getContainer()) {
					containers.push_back(subContainer);
				}
			} else if (queryAdd(slotIndex, item, item->getItemCount(), flags, nullptr) == RETURNVALUE_NOERROR) { // empty slot
				index = slotIndex;
				destItem = nullptr;
				return thisPlayer;
			}
		}

		size_t i = 0;
		while (i < containers.size()) {
			std::shared_ptr<Container> tmpContainer = containers[i++];
			if (!autoStack || !isStackable) {
				const uint32_t containerCapacity = tmpContainer->capacity();
				const uint32_t containerSize = tmpContainer->size();

				// Avoid underflow in the loop below
				if (containerSize >= containerCapacity) {
					continue;
				}

				for (uint32_t pos = 0; pos < containerCapacity; ++pos) {
					auto rv = tmpContainer->queryAdd(pos, item, item->getItemCount(), flags);
					if (rv == RETURNVALUE_NOERROR) {
						index = pos;
						destItem = nullptr;
						return tmpContainer;
					}
				}

				for (const auto &tmpContainerItem : tmpContainer->getItemList()) {
					if (const auto &subContainer = tmpContainerItem->getContainer()) {
						containers.push_back(subContainer);
					}
				}

				continue;
			}

			uint32_t n = 0;

			for (const std::shared_ptr<Item> &tmpItem : tmpContainer->getItemList()) {
				if (tmpItem == m_player.tradeItem) {
					continue;
				}

				if (tmpItem == item) {
					continue;
				}

				// try find an already existing item to stack with
				if (tmpItem->equals(item) && tmpItem->getItemCount() < tmpItem->getStackSize()) {
					index = n;
					destItem = tmpItem;
					return tmpContainer;
				}

				if (const auto &subContainer = tmpItem->getContainer()) {
					containers.push_back(subContainer);
				}

				n++;
			}

			if (n < tmpContainer->capacity() && tmpContainer->queryAdd(n, item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
				index = n;
				destItem = nullptr;
				return tmpContainer;
			}
		}

		return thisPlayer;
	}

	std::shared_ptr<Thing> destThing = m_player.getThing(index);
	if (destThing) {
		destItem = destThing->getItem();
	}

	std::shared_ptr<Item> item = thing->getItem();
	bool movingAmmoToQuiver = item && destItem && destItem->isQuiver() && item->isAmmo();
	// force shield any slot right to player cylinder
	if (index == CONST_SLOT_RIGHT && !movingAmmoToQuiver) {
		return thisPlayer;
	}

	std::shared_ptr<Cylinder> subCylinder = std::dynamic_pointer_cast<Cylinder>(destThing);
	if (subCylinder) {
		index = INDEX_WHEREEVER;
		destItem = nullptr;
		return subCylinder;
	} else {
		return thisPlayer;
	}
}

void PlayerCylinderComponent::addThing(int32_t index, const std::shared_ptr<Thing> &thing) {
	if (!thing) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (index < CONST_SLOT_FIRST || index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const auto &item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setParent(m_player.getPlayer());
	m_player.inventory[index] = item;

	// send to client
	m_player.sendInventoryItem(static_cast<Slots_t>(index), item);
}

void PlayerCylinderComponent::updateThing(const std::shared_ptr<Thing> &thing, uint16_t itemId, uint32_t count) {
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const auto &item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setID(itemId);
	item->setSubType(count);

	// send to client
	m_player.sendInventoryItem(static_cast<Slots_t>(index), item);

	// event methods
	m_player.onUpdateInventoryItem(item, item);
}

void PlayerCylinderComponent::replaceThing(uint32_t index, const std::shared_ptr<Thing> &thing) {
	if (index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const auto &oldItem = m_player.getInventoryItem(static_cast<Slots_t>(index));
	if (!oldItem) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	const auto &item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	// send to client
	m_player.sendInventoryItem(static_cast<Slots_t>(index), item);

	// event methods
	m_player.onUpdateInventoryItem(oldItem, item);

	item->setParent(m_player.getPlayer());

	m_player.inventory[index] = item;
}

void PlayerCylinderComponent::removeThing(const std::shared_ptr<Thing> &thing, uint32_t count) {
	const auto &item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (item->isStackable()) {
		if (count == item->getItemCount()) {
			// send change to client
			m_player.sendInventoryItem(static_cast<Slots_t>(index), nullptr);

			// event methods
			m_player.onRemoveInventoryItem(item);

			item->resetParent();
			m_player.inventory[index] = nullptr;
		} else {
			const auto newCount = static_cast<uint8_t>(std::max<int32_t>(0, item->getItemCount() - count));
			item->setItemCount(newCount);

			// send change to client
			m_player.sendInventoryItem(static_cast<Slots_t>(index), item);

			// event methods
			m_player.onUpdateInventoryItem(item, item);
		}
	} else {
		// send change to client
		m_player.sendInventoryItem(static_cast<Slots_t>(index), nullptr);

		// event methods
		m_player.onRemoveInventoryItem(item);

		item->resetParent();
		m_player.inventory[index] = nullptr;
	}
}

int32_t PlayerCylinderComponent::getThingIndex(const std::shared_ptr<Thing> &thing) const {
	for (uint8_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		if (m_player.inventory[i] == thing) {
			return i;
		}
	}
	return -1;
}

size_t PlayerCylinderComponent::getFirstIndex() const {
	return CONST_SLOT_FIRST;
}

size_t PlayerCylinderComponent::getLastIndex() const {
	return CONST_SLOT_LAST + 1;
}

uint32_t PlayerCylinderComponent::getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/) const {
	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		const auto &item = m_player.inventory[i];
		if (!item) {
			continue;
		}

		if (item->getID() == itemId) {
			count += Item::countByType(item, subType);
		}

		if (const auto &container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getID() == itemId) {
					count += Item::countByType(*it, subType);
				}
			}
		}
	}
	return count;
}

void PlayerCylinderComponent::internalAddThing(const std::shared_ptr<Thing> &thing) {
	internalAddThing(0, thing);
}

void PlayerCylinderComponent::internalAddThing(uint32_t index, const std::shared_ptr<Thing> &thing) {
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item) {
		return;
	}

	// index == 0 means we should equip this item at the most appropiate slot (no action required here)
	if (index >= CONST_SLOT_FIRST && index <= CONST_SLOT_LAST) {
		if (m_player.inventory[index]) {
			return;
		}

		m_player.inventory[index] = item;
		item->setParent(m_player.getPlayer());
	}
}

void PlayerCylinderComponent::postAddNotification(const std::shared_ptr<Thing> &thing, const std::shared_ptr<Cylinder> &oldParent, int32_t index, CylinderLink_t link) {
	if (link == LINK_OWNER) {
		// calling movement scripts
		g_moveEvents().onPlayerEquip(m_player.getPlayer(), thing->getItem(), static_cast<Slots_t>(index), false);
	}

	if (m_player.m_batching) {
		return;
	}

	bool requireListUpdate = true;
	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const auto &item = oldParent ? oldParent->getItem() : nullptr;
		const auto &container = item ? item->getContainer() : nullptr;
		if (container) {
			requireListUpdate = container->getHoldingPlayer() != m_player.getPlayer();
		} else {
			requireListUpdate = oldParent != m_player.getPlayer();
		}

		m_player.updateInventoryWeight();
		m_player.updateItemsLight();
		m_player.sendInventoryIds();
		m_player.sendStats();
	}

	if (const auto &item = thing->getItem()) {
		if (const auto &container = item->getContainer()) {
			m_player.onSendContainer(container);
		}

		if (m_player.shopOwner && !m_player.scheduledSaleUpdate && requireListUpdate) {
			m_player.updateSaleShopList(item);
		}
	} else if (const auto &creature = thing->getCreature()) {
		if (creature == m_player.getPlayer()) {
			m_player.closeContainersOutOfRange();
		}
	}
}

void PlayerCylinderComponent::postRemoveNotification(const std::shared_ptr<Thing> &thing, const std::shared_ptr<Cylinder> &newParent, int32_t index, CylinderLink_t link) {
	if (!thing) {
		return;
	}

	const auto &thisPlayer = m_player.getPlayer();

	const auto copyThing = thing;
	const auto copyNewParent = newParent;

	if (link == LINK_OWNER) {
		if (const auto &item = copyThing->getItem()) {
			if (g_moveEvents().onPlayerDeEquip(thisPlayer, item, static_cast<Slots_t>(index)) == 0) {
				return;
			}
		}
	}

	if (m_player.m_batching) {
		return;
	}

	bool requireListUpdate = true;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const auto &item = copyNewParent ? copyNewParent->getItem() : nullptr;
		const auto &container = item ? item->getContainer() : nullptr;
		if (container) {
			requireListUpdate = container->getHoldingPlayer() != thisPlayer;
		} else {
			requireListUpdate = copyNewParent != thisPlayer;
		}

		m_player.updateInventoryWeight();
		m_player.updateItemsLight();
		m_player.sendInventoryIds();
		m_player.sendStats();
	}

	if (const auto &item = copyThing->getItem()) {
		if (const auto &container = item->getContainer()) {
			if (container->getTopParent() != thisPlayer) {
				m_player.checkLootContainers(container);
			}
			if (m_player.shouldCloseContainer(container)) {
				m_player.autoCloseContainers(container);
			} else {
				m_player.onSendContainer(container);
			}
		}

		// force list update if item exists tier
		if (item->getTier() > 0 && !requireListUpdate) {
			requireListUpdate = true;
		}

		if (m_player.shopOwner && !m_player.scheduledSaleUpdate && requireListUpdate) {
			m_player.updateSaleShopList(item);
		}
	}
}
