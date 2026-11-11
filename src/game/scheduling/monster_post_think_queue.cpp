/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/scheduling/monster_post_think_queue.hpp"

#include "creatures/monsters/monster.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "game/scheduling/dispatcher_policy.hpp"
#include "lib/logging/logger.hpp"

namespace {
	constexpr size_t VISIBLE_MONSTER_POST_THINK_BATCH_SIZE = 8;
	constexpr size_t BACKGROUND_MONSTER_POST_THINK_BATCH_SIZE = 64;
	constexpr size_t MONSTER_POST_THINK_QUEUE_CAPACITY = DISPATCHER_LANE_QUEUE_CAPACITY;

	struct MonsterPostThinkQueue {
		std::mutex mutex;
		std::deque<uint32_t> monsterIds;
		DispatcherAdmissionCounter admission;
		bool scheduled = false;
	};

	MonsterPostThinkQueue visibleMonsterPostThinkQueue;
	MonsterPostThinkQueue backgroundMonsterPostThinkQueue;
	std::array<std::atomic_uint64_t, 2> rejectedMonsterPostThinkTasks {};

	MonsterPostThinkQueue &getMonsterPostThinkQueue(bool playerVisible) {
		return playerVisible ? visibleMonsterPostThinkQueue : backgroundMonsterPostThinkQueue;
	}

	void observeMonsterPostThinkRejection(bool playerVisible) {
		const auto index = playerVisible ? 0 : 1;
		const auto rejected = rejectedMonsterPostThinkTasks[index].fetch_add(1, std::memory_order_relaxed) + 1;
		if (rejected == 1 || (rejected & (rejected - 1)) == 0) {
			g_logger().warn(
				"Monster post-think queue admission rejected: priority={}, capacity={}, rejected={}",
				playerVisible ? "visible" : "background",
				MONSTER_POST_THINK_QUEUE_CAPACITY,
				rejected
			);
		}
	}

	void processMonsterPostThinkQueue(bool playerVisible);

	void cancelMonsterPostThinkQueue(bool playerVisible) {
		auto &queue = getMonsterPostThinkQueue(playerVisible);
		std::deque<uint32_t> monsterIds;
		{
			std::scoped_lock lock(queue.mutex);
			monsterIds.swap(queue.monsterIds);
			queue.scheduled = false;
		}
		for (size_t index = 0; index < monsterIds.size(); ++index) {
			const bool released = queue.admission.release();
			assert(released);
			(void)released;
		}

		for (const auto monsterId : monsterIds) {
			if (const auto &monster = g_game().getMonsterByID(monsterId)) {
				monster->cancelScheduledPostThink(playerVisible);
			}
		}
	}

	bool scheduleMonsterPostThinkQueue(bool playerVisible) {
		std::function<void()> task = [playerVisible] {
			processMonsterPostThinkQueue(playerVisible);
		};
		const bool accepted = playerVisible
			? g_dispatcher().addEvent(std::move(task), "Game::visibleMonsterPostThink", 0, DispatcherLane::WorldCommit)
			: g_dispatcher().addDeferredGameplayEvent(std::move(task), "Game::backgroundMonsterPostThink");
		if (!accepted) {
			cancelMonsterPostThinkQueue(playerVisible);
		}
		return accepted;
	}

	void processMonsterPostThinkQueue(bool playerVisible) {
		auto &queue = getMonsterPostThinkQueue(playerVisible);
		const auto batchLimit = playerVisible ? VISIBLE_MONSTER_POST_THINK_BATCH_SIZE : BACKGROUND_MONSTER_POST_THINK_BATCH_SIZE;
		std::vector<uint32_t> monsterIds;
		bool shouldReschedule = false;
		{
			std::scoped_lock lock(queue.mutex);
			const auto batchSize = std::min(queue.monsterIds.size(), batchLimit);
			monsterIds.reserve(batchSize);
			for (size_t index = 0; index < batchSize; ++index) {
				monsterIds.emplace_back(queue.monsterIds.front());
				queue.monsterIds.pop_front();
			}
			shouldReschedule = !queue.monsterIds.empty();
			queue.scheduled = shouldReschedule;
		}
		for (size_t index = 0; index < monsterIds.size(); ++index) {
			const bool released = queue.admission.release();
			assert(released);
			(void)released;
		}

		for (const auto monsterId : monsterIds) {
			if (const auto &monster = g_game().getMonsterByID(monsterId)) {
				monster->executePostThink(EVENT_CREATURE_THINK_INTERVAL, playerVisible);
			}
		}

		if (shouldReschedule) {
			(void)scheduleMonsterPostThinkQueue(playerVisible);
		}
	}
}

namespace monster_post_think {
	bool enqueue(uint32_t monsterId, bool playerVisible) {
		auto &queue = getMonsterPostThinkQueue(playerVisible);
		if (!queue.admission.tryReserve(MONSTER_POST_THINK_QUEUE_CAPACITY)) {
			observeMonsterPostThinkRejection(playerVisible);
			return false;
		}

		bool shouldSchedule = false;
		try {
			std::scoped_lock lock(queue.mutex);
			queue.monsterIds.emplace_back(monsterId);
			if (!queue.scheduled) {
				queue.scheduled = true;
				shouldSchedule = true;
			}
		} catch (...) {
			const bool released = queue.admission.release();
			assert(released);
			(void)released;
			throw;
		}

		return !shouldSchedule || scheduleMonsterPostThinkQueue(playerVisible);
	}
}
