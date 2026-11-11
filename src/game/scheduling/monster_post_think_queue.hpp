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
	#include <cstdint>
#endif

/**
 * Bounded, lane-aware batching of monster post-think work on the dispatcher.
 *
 * Queue state is process-wide. Scheduled callbacks capture only the priority
 * flag and re-resolve monsters by ID, so stale entries become no-ops.
 */
namespace monster_post_think {
	/**
	 * @brief Queues a monster for post-think processing.
	 * @param monsterId Runtime monster ID, re-resolved when the batch runs.
	 * @param playerVisible Selects the visible (WorldCommit) or background (deferred) queue.
	 * @return False when admission is rejected or the batch could not be scheduled.
	 */
	[[nodiscard]] bool enqueue(uint32_t monsterId, bool playerVisible);
}
