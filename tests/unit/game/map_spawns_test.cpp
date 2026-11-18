/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/monsters/spawns/spawn_monster.hpp"
#include "creatures/npcs/spawns/spawn_npc.hpp"
#include "map/map.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <memory>
	#include <vector>
#endif

// Spawns cancel their dispatcher event when destroyed, so shutdown drops all of them while the
// dispatcher lives (Phase 25.8). The custom-map lists were missed before and outlived it.
TEST(MapSpawnsTest, ClearSpawnsDropsTheMainAndEveryCustomMapSpawn) {
	Map map;
	const Position position(100, 100, 7);
	std::vector<std::weak_ptr<SpawnMonster>> monsterSpawns;
	std::vector<std::weak_ptr<SpawnNpc>> npcSpawns;
	monsterSpawns.emplace_back(map.spawnsMonster.getspawnMonsterList().emplace_back(std::make_shared<SpawnMonster>(position, 5)));
	npcSpawns.emplace_back(map.spawnsNpc.getSpawnNpcList().emplace_back(std::make_shared<SpawnNpc>(position, 5)));
	for (const auto index : { 0, 17, 49 }) {
		monsterSpawns.emplace_back(map.spawnsMonsterCustomMaps[index].getspawnMonsterList().emplace_back(std::make_shared<SpawnMonster>(position, 5)));
		npcSpawns.emplace_back(map.spawnsNpcCustomMaps[index].getSpawnNpcList().emplace_back(std::make_shared<SpawnNpc>(position, 5)));
	}

	map.clearSpawns();
	map.clearSpawns(); // idempotent

	for (const auto &spawn : monsterSpawns) {
		EXPECT_TRUE(spawn.expired());
	}
	for (const auto &spawn : npcSpawns) {
		EXPECT_TRUE(spawn.expired());
	}
	EXPECT_TRUE(map.spawnsMonster.getspawnMonsterList().empty());
	for (auto &spawns : map.spawnsNpcCustomMaps) {
		EXPECT_TRUE(spawns.getSpawnNpcList().empty());
	}
}
