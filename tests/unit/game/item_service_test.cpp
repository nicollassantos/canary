/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/containers/container.hpp"
#include "items/item.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "config/in_memory_config_manager.hpp"
#include "test_items.hpp"

namespace {
	const Position kTestPos { 200, 200, 7 };
	constexpr uint16_t kTestItemId = 100;

	std::shared_ptr<Player> makePlacedPlayer(uint32_t guid, const Position &pos) {
		auto player = std::make_shared<Player>();
		player->setName("ItemSvcTest" + std::to_string(guid));
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	void ensurePickupable(uint16_t id) {
		auto &items = Item::items.getItems();
		if (items.size() > id) {
			items[id].pickupable = true;
		}
	}

	class ItemServiceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			test_items::seedFallbackTestItems();
			ensurePickupable(kTestItemId);
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryConfigManager::install(injector);
			DI::setTestContainer(&injector);
			// Container uses g_configManager().getNumber(MAX_CONTAINER_ITEM) for m_maxItems
			auto &cfg = dynamic_cast<InMemoryConfigManager &>(DI::get<IConfigManager>());
			cfg.setNumber(MAX_CONTAINER_ITEM, 1000);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

		void SetUp() override {
			player = makePlacedPlayer(6001, kTestPos);
		}

	protected:
		Game game;
		std::shared_ptr<Player> player;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

// internalAddItem — adding item to a container succeeds
TEST_F(ItemServiceTest, InternalAddItem_ReturnsOk_WhenAddingToContainer) {
	auto container = std::make_shared<Container>(ITEM_REWARD_CONTAINER, 20, true, false);
	auto item = Item::CreateItem(kTestItemId);
	ASSERT_NE(nullptr, item);

	const ReturnValue rv = game.internalAddItem(container, item);
	EXPECT_EQ(RETURNVALUE_NOERROR, rv);
	EXPECT_TRUE(container->isHoldingItem(item));
}

// internalAddItem — fails when cylinder is null
TEST_F(ItemServiceTest, InternalAddItem_ReturnsFails_WhenCylinderIsNull) {
	auto item = Item::CreateItem(kTestItemId);
	ASSERT_NE(nullptr, item);

	const ReturnValue rv = game.internalAddItem(nullptr, item);
	EXPECT_NE(RETURNVALUE_NOERROR, rv);
}

// internalAddItem — fails when item is null
TEST_F(ItemServiceTest, InternalAddItem_ReturnsFails_WhenItemIsNull) {
	auto container = std::make_shared<Container>(ITEM_REWARD_CONTAINER, 20, true, false);

	const ReturnValue rv = game.internalAddItem(container, nullptr);
	EXPECT_NE(RETURNVALUE_NOERROR, rv);
}

// internalRemoveItem — removing item that is not inside fails
TEST_F(ItemServiceTest, InternalRemoveItem_Fails_WhenItemHasNoParent) {
	auto item = Item::CreateItem(kTestItemId);
	ASSERT_NE(nullptr, item);

	const ReturnValue rv = game.internalRemoveItem(item);
	EXPECT_NE(RETURNVALUE_NOERROR, rv);
}

// internalRemoveItem — fails when item is null
TEST_F(ItemServiceTest, InternalRemoveItem_Fails_WhenItemIsNull) {
	const ReturnValue rv = game.internalRemoveItem(nullptr);
	EXPECT_NE(RETURNVALUE_NOERROR, rv);
}

// findItemOfType — returns nullptr when cylinder is empty
TEST_F(ItemServiceTest, FindItemOfType_ReturnsNull_WhenContainerIsEmpty) {
	auto container = std::make_shared<Container>(ITEM_REWARD_CONTAINER, 20, true, false);

	const auto found = game.findItemOfType(container, kTestItemId);
	EXPECT_EQ(nullptr, found);
}

// findItemOfType — returns item when it exists in container
TEST_F(ItemServiceTest, FindItemOfType_ReturnsItem_WhenItemExistsInContainer) {
	auto container = std::make_shared<Container>(ITEM_REWARD_CONTAINER, 20, true, false);
	auto item = Item::CreateItem(kTestItemId);
	game.internalAddItem(container, item);

	const auto found = game.findItemOfType(container, kTestItemId);
	EXPECT_EQ(item, found);
}

// playerMoveItemByPlayerID — noop when player not found
TEST_F(ItemServiceTest, PlayerMoveItemByPlayerID_DoesNothing_WhenPlayerNotFound) {
	const Position fromPos { 100, 100, 7 };
	const Position toPos { 101, 100, 7 };

	EXPECT_NO_FATAL_FAILURE(game.playerMoveItemByPlayerID(0xDEADBEEF, fromPos, kTestItemId, 0, toPos, 1));
}
