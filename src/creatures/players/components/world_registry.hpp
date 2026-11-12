/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "game/game_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <memory>
	#include <parallel_hashmap/phmap.h>
#endif

class Creature;
class Player;

/**
 * Who is in the world and what kind of world it is, as player components see it
 * (driven port): online-player lookups, registration on login/logout and the world type.
 *
 * Same contract as CreatureFeedback: signatures mirror Game's, so call sites change only
 * the receiver (`g_game()` → `g_worldRegistry()`); the default adapts Game; tests replace
 * it with `di::bind<WorldRegistry>.to<Fake>()`; resolve per call.
 */
class WorldRegistry {
public:
	using PlayerMap = phmap::parallel_flat_hash_map<uint32_t, std::shared_ptr<Player>>;

	virtual ~WorldRegistry() = default;

	static WorldRegistry &getInstance();

	[[nodiscard]] virtual std::shared_ptr<Player> getPlayerByID(uint32_t id, bool allowOffline = false);
	[[nodiscard]] virtual std::shared_ptr<Creature> getCreatureByID(uint32_t id);
	[[nodiscard]] virtual const PlayerMap &getPlayers() const;
	virtual void addPlayer(const std::shared_ptr<Player> &player);
	virtual void removePlayer(const std::shared_ptr<Player> &player);
	virtual bool removeCreature(const std::shared_ptr<Creature> &creature, bool isLogout = true);
	virtual void checkPlayersRecord();
	[[nodiscard]] virtual WorldType_t getWorldType() const;
};

constexpr auto g_worldRegistry = WorldRegistry::getInstance;
