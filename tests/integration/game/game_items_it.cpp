/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// Characterization tests for game.cpp item subsystem.
// These document current behavior to guard against regressions during
// the extraction of ItemService (Phase 4.3 of SOLID refactor).

#include <gtest/gtest.h>

#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "items/items.hpp"
#include "utils/utils_definitions.hpp"

namespace {

	constexpr char kTestPathSuffix[] = "/tests/integration/game/game_items_it.cpp";

	std::string detectRepoRoot() {
		std::string filePath = __FILE__;
		const auto pos = filePath.find(kTestPathSuffix);
		if (pos != std::string::npos) {
			return filePath.substr(0, pos);
		}
		const auto fallback = filePath.find("/tests/");
		if (fallback != std::string::npos) {
			return filePath.substr(0, fallback);
		}
		return {};
	}

	struct ItemScope {
		uint16_t containerId {};
		uint16_t regularItemId {};

		ItemScope() {
			auto &items = Item::items.getItems();
			originalSize = items.size();

			containerId = static_cast<uint16_t>(originalSize + 1);
			regularItemId = static_cast<uint16_t>(originalSize + 2);

			items.resize(originalSize + 10);

			auto &containerType = Item::items.getItemType(containerId);
			containerType = ItemType {};
			containerType.id = containerId;
			containerType.group = ITEM_GROUP_CONTAINER;
			containerType.type = ITEM_TYPE_CONTAINER;
			containerType.maxItems = 20;
			containerType.pickupable = true;

			auto &regularType = Item::items.getItemType(regularItemId);
			regularType = ItemType {};
			regularType.id = regularItemId;
			regularType.name = "test item";
			regularType.pickupable = true;
			regularType.movable = true;
		}

		~ItemScope() noexcept {
			try {
				auto &items = Item::items.getItems();
				if (items.size() > originalSize) {
					items.resize(originalSize);
				}
			} catch (...) { }
		}

	private:
		size_t originalSize {};
	};

	static std::shared_ptr<Player> makeTestPlayer(uint32_t guid) {
		auto player = std::make_shared<Player>(nullptr);
		auto group = std::make_shared<Group>();
		group->id = 1;
		group->name = "Test";
		group->access = false;
		group->maxDepotItems = 2000;
		group->maxVipEntries = 100;
		player->setGroup(group);
		player->setGUID(guid);
		player->setID();
		player->setName("ItemTestPlayer" + std::to_string(guid));
		return player;
	}

	class GameItemsTest : public ::testing::Test {
	protected:
		static void SetUpTestSuite() {
			suiteReady = false;
			repositoryRoot = detectRepoRoot();
			if (repositoryRoot.empty()) {
				skipReason = "Repository root not detected from test source path.";
				return;
			}
			nextGuid = 5000u;
			suiteReady = true;
		}

		void SetUp() override {
			if (!suiteReady) {
				GTEST_SKIP() << skipReason;
			}
			player = makeTestPlayer(nextGuid++);
			g_game().addPlayer(player);
		}

		void TearDown() override {
			if (player) {
				g_game().removePlayer(player);
			}
			player.reset();
		}

		std::shared_ptr<Player> player;

		inline static bool suiteReady = false;
		inline static std::string skipReason;
		inline static std::string repositoryRoot;
		inline static uint32_t nextGuid = 5000u;
	};

} // namespace

// internalAddItem: adding an item to a container succeeds and item is present
TEST_F(GameItemsTest, InternalAddItemToContainerSucceeds) {
	ItemScope scope;
	const auto container = Container::create(scope.containerId);
	ASSERT_NE(nullptr, container);

	const auto item = Item::CreateItem(scope.regularItemId, 1);
	ASSERT_NE(nullptr, item);

	const ReturnValue ret = g_game().internalAddItem(container, item);
	EXPECT_EQ(RETURNVALUE_NOERROR, ret);
	EXPECT_EQ(1u, container->size());
	EXPECT_EQ(item, container->getItemList().front());
}

// internalAddItem with test=true validates without modifying the container
TEST_F(GameItemsTest, InternalAddItemTestModeDoesNotAddItem) {
	ItemScope scope;
	const auto container = Container::create(scope.containerId);
	ASSERT_NE(nullptr, container);

	const auto item = Item::CreateItem(scope.regularItemId, 1);
	ASSERT_NE(nullptr, item);

	const ReturnValue ret = g_game().internalAddItem(container, item, INDEX_WHEREEVER, 0, true);
	EXPECT_EQ(RETURNVALUE_NOERROR, ret);
	EXPECT_EQ(0u, container->size());
}

// internalRemoveItem: item is removed from container after successful remove
TEST_F(GameItemsTest, InternalRemoveItemFromContainerSucceeds) {
	ItemScope scope;
	const auto container = Container::create(scope.containerId);
	ASSERT_NE(nullptr, container);

	const auto item = Item::CreateItem(scope.regularItemId, 1);
	ASSERT_NE(nullptr, item);

	ASSERT_EQ(RETURNVALUE_NOERROR, g_game().internalAddItem(container, item));
	ASSERT_EQ(1u, container->size());

	const ReturnValue ret = g_game().internalRemoveItem(item, 1);
	EXPECT_EQ(RETURNVALUE_NOERROR, ret);
	EXPECT_EQ(0u, container->size());
}

// findItemOfType: locates item in a nested container hierarchy
TEST_F(GameItemsTest, FindItemOfTypeFindsItemInNestedContainer) {
	ItemScope scope;
	const auto outer = Container::create(scope.containerId);
	const auto inner = Container::create(scope.containerId);
	ASSERT_NE(nullptr, outer);
	ASSERT_NE(nullptr, inner);

	ASSERT_EQ(RETURNVALUE_NOERROR, g_game().internalAddItem(outer, inner));

	const auto item = Item::CreateItem(scope.regularItemId, 1);
	ASSERT_NE(nullptr, item);
	ASSERT_EQ(RETURNVALUE_NOERROR, g_game().internalAddItem(inner, item));

	const auto found = g_game().findItemOfType(outer, scope.regularItemId, true);
	EXPECT_EQ(item, found);
}

// findItemOfType: returns nullptr when item absent
TEST_F(GameItemsTest, FindItemOfTypeReturnsNullWhenAbsent) {
	ItemScope scope;
	const auto container = Container::create(scope.containerId);
	ASSERT_NE(nullptr, container);

	const auto found = g_game().findItemOfType(container, scope.regularItemId, true);
	EXPECT_EQ(nullptr, found);
}

// internalPlayerAddItem: a custom container without slotPosition set cannot be
// equipped to a body slot — engine returns RETURNVALUE_CANNOTBEDRESSED (17).
// Characterizes that slot-based equipping requires proper item configuration.
TEST_F(GameItemsTest, InternalPlayerAddItemRequiresProperSlotConfig) {
	ItemScope scope;
	const auto container = Container::create(scope.containerId);
	ASSERT_NE(nullptr, container);

	// Custom container has no slotPosition configured → cannot equip to body slot
	const ReturnValue ret = g_game().internalPlayerAddItem(player, container, false, CONST_SLOT_BACKPACK);
	EXPECT_EQ(RETURNVALUE_CANNOTBEDRESSED, ret);
}
