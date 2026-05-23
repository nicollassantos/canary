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

class Player;
class Creature;
class Item;
class Thing;
class Cylinder;
enum class CylinderLink_t : uint8_t;
enum ReturnValue : uint16_t;

class PlayerCylinderComponent {
public:
	PlayerCylinderComponent() = delete;
	explicit PlayerCylinderComponent(Player &player) :
		m_player(player) { }

	ReturnValue queryAdd(int32_t index, const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t flags, const std::shared_ptr<Creature> &actor) const;
	ReturnValue queryMaxCount(int32_t index, const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t &maxQueryCount, uint32_t flags);
	ReturnValue queryRemove(const std::shared_ptr<Thing> &thing, uint32_t count, uint32_t flags, const std::shared_ptr<Creature> &actor) const;
	std::shared_ptr<Cylinder> queryDestination(int32_t &index, const std::shared_ptr<Thing> &thing, std::shared_ptr<Item> &destItem, uint32_t &flags);

	void addThing(int32_t index, const std::shared_ptr<Thing> &thing);
	void updateThing(const std::shared_ptr<Thing> &thing, uint16_t itemId, uint32_t count);
	void replaceThing(uint32_t index, const std::shared_ptr<Thing> &thing);
	void removeThing(const std::shared_ptr<Thing> &thing, uint32_t count);

	int32_t getThingIndex(const std::shared_ptr<Thing> &thing) const;
	size_t getFirstIndex() const;
	size_t getLastIndex() const;
	uint32_t getItemTypeCount(uint16_t itemId, int32_t subType = -1) const;

	void internalAddThing(const std::shared_ptr<Thing> &thing);
	void internalAddThing(uint32_t index, const std::shared_ptr<Thing> &thing);

	void postAddNotification(const std::shared_ptr<Thing> &thing, const std::shared_ptr<Cylinder> &oldParent, int32_t index, CylinderLink_t link);
	void postRemoveNotification(const std::shared_ptr<Thing> &thing, const std::shared_ptr<Cylinder> &newParent, int32_t index, CylinderLink_t link);

private:
	Player &m_player;
};
