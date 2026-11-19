/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/containers/depot/depotchest.hpp"
#include "items/containers/depot/depotlocker.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "items/items_classification.hpp"

namespace {
	// Depot search "retrieve all": the items are collected by the tier of the open search and created
	// again in the player's backpack. The tier byte of the packet must not change what comes out.
	class DepotSearchRetrieveTest : public ::testing::Test {
	protected:
		static void SetUpTestSuite() {
			auto &items = Item::items.getItems();
			originalItemTypeSize = items.size();
			itemId = static_cast<uint16_t>(originalItemTypeSize + 10);
			items.resize(itemId + 1);

			auto &itemType = Item::items.getItemType(itemId);
			itemType = ItemType {};
			itemType.id = itemId;
			itemType.name = "depot search test item";
			itemType.pickupable = true;
			itemType.movable = true;
			itemType.type = ITEM_TYPE_TOOLS;
			itemType.weight = 100;
			itemType.upgradeClassification = 1;
		}

		static void TearDownTestSuite() {
			auto &items = Item::items.getItems();
			if (items.size() > originalItemTypeSize) {
				items.resize(originalItemTypeSize);
			}
		}

		void SetUp() override {
			auto group = std::make_shared<Group>();
			group->id = 1;
			group->name = "Depot search test group";
			group->maxDepotItems = 2000;
			group->maxVipEntries = 100;

			player = std::make_shared<Player>(nullptr);
			player->setGroup(group);
			player->setGUID(nextGuid++);
			player->setID();
			player->setName("DepotSearchIt" + std::to_string(player->getGUID()));
			g_game().addPlayer(player);

			const auto &backpack = Item::CreateItem(ITEM_BACKPACK, 1);
			ASSERT_NE(nullptr, backpack);
			ASSERT_EQ(RETURNVALUE_NOERROR, g_game().internalPlayerAddItem(player, backpack, false, CONST_SLOT_BACKPACK));

			player->setLastDepotId(1);
			ASSERT_NE(nullptr, player->getDepotLocker(1));
			depotBox = player->getDepotChest(1, true);
			ASSERT_NE(nullptr, depotBox);
		}

		void TearDown() override {
			if (player) {
				g_game().removePlayer(player);
			}
			player.reset();
		}

		void storeInDepot(uint8_t tier, int count) const {
			for (int i = 0; i < count; ++i) {
				const auto &item = Item::CreateItem(itemId, 1);
				ASSERT_NE(nullptr, item);
				item->setTier(tier);
				depotBox->internalAddThing(item);
			}
		}

		size_t carried(uint8_t tier) const {
			size_t total = 0;
			for (const auto &item : player->getAllInventoryItems(false)) {
				if (item && item->getID() == itemId && item->getTier() == tier) {
					++total;
				}
			}
			return total;
		}

		size_t inDepot(uint8_t tier) const {
			size_t total = 0;
			for (const auto &item : depotBox->getItemList()) {
				if (item && item->getID() == itemId && item->getTier() == tier) {
					++total;
				}
			}
			return total;
		}

		inline static size_t originalItemTypeSize = 0;
		inline static uint16_t itemId = 0;
		inline static uint32_t nextGuid = 52000;
		std::shared_ptr<Player> player;
		std::shared_ptr<DepotChest> depotBox;
	};
} // namespace

TEST_F(DepotSearchRetrieveTest, ATierOtherThanTheSearchedOneIsRefusedAndNothingChanges) {
	storeInDepot(0, 3);
	player->setDepotSearchIsOpen(itemId, 0);

	player->retrieveAllItemsFromDepotSearch(itemId, 5, true);

	EXPECT_EQ(0u, carried(5));
	EXPECT_EQ(0u, carried(0));
	EXPECT_EQ(3u, inDepot(0));
}

TEST_F(DepotSearchRetrieveTest, TheSearchedTierIsRetrievedAsItIs) {
	storeInDepot(0, 2);
	storeInDepot(2, 1);
	player->setDepotSearchIsOpen(itemId, 2);

	player->retrieveAllItemsFromDepotSearch(itemId, 2, true);

	EXPECT_EQ(1u, carried(2));
	EXPECT_EQ(0u, inDepot(2));
	EXPECT_EQ(2u, inDepot(0)); // the other tier stays
}

TEST_F(DepotSearchRetrieveTest, AnotherItemThanTheSearchedOneIsRefused) {
	storeInDepot(0, 1);
	player->setDepotSearchIsOpen(static_cast<uint16_t>(itemId - 1), 0);

	player->retrieveAllItemsFromDepotSearch(itemId, 0, true);

	EXPECT_EQ(0u, carried(0));
	EXPECT_EQ(1u, inDepot(0));
}
