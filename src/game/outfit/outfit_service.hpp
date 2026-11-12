/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "utils/request_cooldown.hpp"
#include "utils/tools.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

class Game;
class IConfigManager;
class Creature;
class Player;
struct Outfit_t;
struct Position;

class OutfitService {
public:
	/// Opening the outfit window builds the whole outfit/mount list, and every change is broadcast
	/// to all spectators, so client requests are throttled per player (known spam bots loop both).
	static constexpr int64_t OutfitWindowCooldownMs = 1000;
	static constexpr int64_t OutfitChangeCooldownMs = 500;

	OutfitService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	/// Client request (0xD2): sends the outfit window, at most once per OutfitWindowCooldownMs.
	void playerRequestOutfit(uint32_t playerId);
	/// Client request (0xD3): at most one change per OutfitChangeCooldownMs. Internal callers use
	/// the Player overload, which is not throttled.
	void playerChangeOutfit(uint32_t playerId, Outfit_t outfit, bool setMount, uint8_t isMountRandomized = 0);
	void playerChangeOutfit(const std::shared_ptr<Player> &player, Outfit_t outfit, bool setMount, uint8_t isMountRandomized = 0);
	void internalCreatureChangeOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit);
	void playerSetMonsterPodium(uint32_t playerId, uint32_t monsterRaceId, const Position &pos, uint8_t stackPos, uint16_t itemId, uint8_t direction, const std::pair<uint8_t, uint8_t> &podiumAndMonsterVisible);
	void playerRotatePodium(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);

	[[nodiscard]] uint64_t throttledRequests() const {
		return throttled;
	}

	/// Milliseconds clock for the cooldowns; tests replace it.
	std::function<int64_t()> now = [] { return OTSYS_TIME(); };

private:
	Game &game_;
	IConfigManager &config_;
	RequestCooldown outfitWindowCooldown { OutfitWindowCooldownMs };
	RequestCooldown outfitChangeCooldown { OutfitChangeCooldownMs };
	uint64_t throttled = 0;
};
