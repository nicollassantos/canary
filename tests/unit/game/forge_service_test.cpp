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
#include "game/forge/forge_service.hpp"
#include "game/game.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "config/in_memory_config_manager.hpp"
#include "utils/utils_definitions.hpp"

namespace {

	const Position kPos { 100, 100, 7 };

	std::shared_ptr<Player> makeForgePlayer(uint32_t guid = 8001) {
		auto player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(kPos);
		tile->addThing(player);
		return player;
	}

	class ForgeServiceTest : public ::testing::Test {
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
		ForgeService forge { game, config };

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- addInfluencedMonster ---

// null monster → false immediately
TEST_F(ForgeServiceTest, AddInfluencedMonster_NullMonster_ReturnsFalse) {
	EXPECT_FALSE(forge.addInfluencedMonster(nullptr));
}

// --- makeInfluencedMonster ---

// no forgeable monsters in game → returns 0
TEST_F(ForgeServiceTest, MakeInfluencedMonster_EmptyForgeableMonsters_ReturnsZero) {
	EXPECT_EQ(0u, forge.makeInfluencedMonster());
}

// --- makeFiendishMonster ---

// no forgeable monsters in game → returns 0
TEST_F(ForgeServiceTest, MakeFiendishMonster_EmptyForgeableMonsters_ReturnsZero) {
	EXPECT_EQ(0u, forge.makeFiendishMonster());
}

// createForgeableMonsters=true but game has no monsters → clears vector, still returns 0
TEST_F(ForgeServiceTest, MakeFiendishMonster_CreateForgeableTrue_NoMonsters_ReturnsZero) {
	EXPECT_EQ(0u, forge.makeFiendishMonster(0, true));
}

