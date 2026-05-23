#pragma once

#include <cstdint>
#include <memory>

class Player;
class Creature;
class Item;
class Container;
class Tile;
class Position;
struct ItemType;

class PlayerCreatureEventComponent {
public:
	PlayerCreatureEventComponent() = delete;
	explicit PlayerCreatureEventComponent(Player &player) :
		m_player(player) { }

	void onUpdateTileItem(const std::shared_ptr<Tile> &tile, const Position &pos, const std::shared_ptr<Item> &oldItem, const ItemType &oldType, const std::shared_ptr<Item> &newItem, const ItemType &newType);
	void onRemoveTileItem(const std::shared_ptr<Tile> &tile, const Position &pos, const ItemType &iType, const std::shared_ptr<Item> &item);

	void onCreatureAppear(const std::shared_ptr<Creature> &creature, bool isLogin);
	void onRemoveCreature(const std::shared_ptr<Creature> &creature, bool isLogout);
	void onCreatureMove(const std::shared_ptr<Creature> &creature, const std::shared_ptr<Tile> &newTile, const Position &newPos, const std::shared_ptr<Tile> &oldTile, const Position &oldPos, bool teleport);

	void onEquipInventory();
	void onDeEquipInventory();

	void onAttackedCreatureDisappear(bool isLogout);
	void onFollowCreatureDisappear(bool isLogout);

	void onAddContainerItem(const std::shared_ptr<Item> &item);
	void onUpdateContainerItem(const std::shared_ptr<Container> &container, const std::shared_ptr<Item> &oldItem, const std::shared_ptr<Item> &newItem);
	void onRemoveContainerItem(const std::shared_ptr<Container> &container, const std::shared_ptr<Item> &item);
	void onCloseContainer(const std::shared_ptr<Container> &container);
	void onSendContainer(const std::shared_ptr<Container> &container);
	void autoCloseContainers(const std::shared_ptr<Container> &container);

	void onUpdateInventoryItem(const std::shared_ptr<Item> &oldItem, const std::shared_ptr<Item> &newItem);
	void onRemoveInventoryItem(const std::shared_ptr<Item> &item);

private:
	Player &m_player;
};
