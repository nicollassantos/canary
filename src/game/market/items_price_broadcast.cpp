/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/market/items_price_broadcast.hpp"

#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "lib/metrics/metrics.hpp"
#include "server/network/message/networkmessage.hpp"
#include "server/network/protocol/protocolgame.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
	#include <atomic>
	#include <memory>
#endif

namespace items_price_broadcast {

	namespace {
		std::atomic<uint64_t> latestGeneration { 0 };
	}

	std::vector<std::vector<uint32_t>> makeBatches(const std::vector<uint32_t> &playerIds, std::size_t batchSize) {
		batchSize = std::max<std::size_t>(batchSize, 1);
		std::vector<std::vector<uint32_t>> batches;
		batches.reserve((playerIds.size() + batchSize - 1) / batchSize);
		for (std::size_t first = 0; first < playerIds.size(); first += batchSize) {
			const auto last = std::min(first + batchSize, playerIds.size());
			batches.emplace_back(playerIds.begin() + static_cast<std::ptrdiff_t>(first), playerIds.begin() + static_cast<std::ptrdiff_t>(last));
		}
		return batches;
	}

	uint64_t currentGeneration() {
		return latestGeneration.load();
	}

	void start() {
		metrics::method_latency measure(__METRICS_METHOD_NAME__);
		const auto generation = ++latestGeneration;

		std::vector<uint32_t> playerIds;
		playerIds.reserve(g_game().getPlayers().size());
		for (const auto &[id, _] : g_game().getPlayers()) {
			playerIds.emplace_back(id);
		}
		if (playerIds.empty()) {
			return;
		}

		auto message = std::make_shared<NetworkMessage>();
		ProtocolGame::addItemsPrice(*message);
		const std::shared_ptr<const NetworkMessage> shared = std::move(message);

		auto batches = makeBatches(playerIds);
		for (std::size_t i = 0; i < batches.size(); ++i) {
			// Only values cross into the callback: ids are resolved again when the batch runs.
			g_dispatcher().scheduleEvent(
				static_cast<uint32_t>((i + 1) * BatchIntervalMs),
				[generation, ids = std::move(batches[i]), shared] {
					sendBatch(generation, ids, *shared);
				},
				"items_price_broadcast::sendBatch"
			);
		}
	}

	std::size_t sendBatch(uint64_t generation, const std::vector<uint32_t> &playerIds, const NetworkMessage &message) {
		if (generation != latestGeneration.load()) {
			return 0;
		}

		metrics::method_latency measure(__METRICS_METHOD_NAME__);
		std::size_t sent = 0;
		for (const auto id : playerIds) {
			if (const auto &player = g_game().getPlayerByID(id)) {
				player->sendItemsPrice(message);
				++sent;
			}
		}
		return sent;
	}

} // namespace items_price_broadcast
