/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstddef>
	#include <cstdint>
	#include <vector>
#endif

class NetworkMessage;

// Sends the market price list (0xCD) again to the players online after the prices change (issue
// #2083: the client keeps the list it got at login). The list (up to ~64 KB) is built once and
// delivered in batches, so the game loop never builds or queues it for everyone in one task.
namespace items_price_broadcast {

	/// Players per batch and the gap between batches: 1000 players online get the list in ~2 s.
	inline constexpr std::size_t BatchSize = 50;
	inline constexpr uint32_t BatchIntervalMs = 100;

	/// Splits the ids, in order, into consecutive batches of at most batchSize (0 is treated as 1).
	[[nodiscard]] std::vector<std::vector<uint32_t>> makeBatches(const std::vector<uint32_t> &playerIds, std::size_t batchSize = BatchSize);

	/// Builds the list once and schedules one send per batch of the players online now. Batches still
	/// pending from an earlier call become no-ops, so a player never ends with an older list.
	void start();

	/// Sends the list to the players of the batch still online, unless a later start() replaced this
	/// generation. Returns how many players got it.
	std::size_t sendBatch(uint64_t generation, const std::vector<uint32_t> &playerIds, const NetworkMessage &message);

	/// The generation of the latest start().
	[[nodiscard]] uint64_t currentGeneration();

} // namespace items_price_broadcast
