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
#include "game/loot/loot_service.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "config/in_memory_config_manager.hpp"
#include "enums/object_category.hpp"

namespace {

	const Position kPos { 100, 100, 7 };

	std::shared_ptr<Player> makeLootPlayer(uint32_t guid = 7001) {
		auto player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(kPos);
		tile->addThing(player);
		return player;
	}

	class LootServiceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryConfigManager::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		Game game;
		InMemoryConfigManager config;
		LootService loot { game, config };

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- getObjectCategory ---

// null item → OBJECTCATEGORY_NONE (immediate null guard, no items table access)
TEST_F(LootServiceTest, GetObjectCategory_NullItem_ReturnsNone) {
	EXPECT_EQ(OBJECTCATEGORY_NONE, loot.getObjectCategory(nullptr));
}

// --- internalCollectManagedItems ---

// null player → RETURNVALUE_NOTPOSSIBLE
TEST_F(LootServiceTest, InternalCollectManagedItems_NullPlayer_ReturnsNotPossible) {
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, loot.internalCollectManagedItems(nullptr, nullptr, OBJECTCATEGORY_DEFAULT));
}

// null item → RETURNVALUE_NOTPOSSIBLE
TEST_F(LootServiceTest, InternalCollectManagedItems_NullItem_ReturnsNotPossible) {
	const auto player = makeLootPlayer();
	EXPECT_EQ(RETURNVALUE_NOTPOSSIBLE, loot.internalCollectManagedItems(player, nullptr, OBJECTCATEGORY_DEFAULT));
}

// --- findManagedContainer ---

// fresh player has no managed containers → returns nullptr
TEST_F(LootServiceTest, FindManagedContainer_NoContainers_ReturnsNullptr) {
	const auto player = makeLootPlayer();
	bool fallbackConsumed = false;
	const auto container = loot.findManagedContainer(player, fallbackConsumed, OBJECTCATEGORY_DEFAULT, true);
	EXPECT_EQ(nullptr, container);
}

// player with no backpack: fallback path finds no container → nullptr, fallbackConsumed unchanged
TEST_F(LootServiceTest, FindManagedContainer_NoBackpackNoFallbackItem_ReturnsNullptr) {
	const auto player = makeLootPlayer();
	bool fallbackConsumed = false;
	// Player has no inventory backpack, so fallback lookup yields nullptr
	const auto container = loot.findManagedContainer(player, fallbackConsumed, OBJECTCATEGORY_GOLD, false);
	EXPECT_EQ(nullptr, container);
}
