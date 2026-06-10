/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/movement/teleport_guard.hpp"

#include "creatures/creature.hpp"
#include "items/item.hpp"
#include "lib/logging/logger.hpp"
#include "utils/tools.hpp"

static constexpr uint64_t TELEPORT_SPAM_WINDOW_MS = 1000;
static constexpr uint16_t TELEPORT_SPAM_LIMIT = 10;
static constexpr uint64_t TELEPORT_SPAM_BLOCK_MS = 1000;
static constexpr uint64_t TELEPORT_SUSTAINED_WINDOW_MS = 10000;
static constexpr uint16_t TELEPORT_SUSTAINED_LIMIT = 30;
static constexpr uint64_t TELEPORT_SUSTAINED_BLOCK_MS = 5000;
static constexpr uint64_t TELEPORT_GUARD_STATE_TTL_MS = 60000;
static constexpr uint64_t TELEPORT_LOG_INITIAL_DELAY_MS = 1000;
static constexpr uint64_t TELEPORT_LOG_MAX_DELAY_MS = 60000;
static constexpr size_t TELEPORT_MAX_TRACKED_THINGS = 4096;
static constexpr uint64_t TELEPORT_CREATURE_KEY_TAG = 0x1000000000000000ULL;
static constexpr uint64_t TELEPORT_ITEM_KEY_TAG = 0x2000000000000000ULL;
static constexpr uint64_t TELEPORT_THING_KEY_TAG = 0x3000000000000000ULL;

namespace {
	std::unordered_set<const Thing*> &teleportStack() {
		static thread_local std::unordered_set<const Thing*> stack;
		return stack;
	}

	uint64_t pointerKey(uint64_t tag, const void* ptr) {
		return tag ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
	}
} // namespace

void TeleportGuard::StackCleaner::operator()(const Thing* thing) const {
	if (!teleportStack().erase(thing)) {
		g_logger().error("[TeleportGuard] Stack cleanup failed for thing ptr {}", fmt::ptr(thing));
	}
}

TeleportGuard::StackGuard TeleportGuard::tryEnterStack(const Thing* thing) {
	if (!teleportStack().insert(thing).second) {
		return nullptr;
	}
	return StackGuard(thing, StackCleaner {});
}

uint64_t TeleportGuard::guardKey(const std::shared_ptr<Thing> &thing, const Thing* rawThing) {
	if (const auto creature = thing->getCreature()) {
		return TELEPORT_CREATURE_KEY_TAG | creature->getID();
	}
	if (const auto item = thing->getItem()) {
		return pointerKey(TELEPORT_ITEM_KEY_TAG, item.get());
	}
	return pointerKey(TELEPORT_THING_KEY_TAG, rawThing);
}

void TeleportGuard::pruneStates(uint64_t now) {
	if (m_guards.size() <= TELEPORT_MAX_TRACKED_THINGS) {
		return;
	}
	for (auto it = m_guards.begin(); it != m_guards.end();) {
		if (it->second.lastSeenMs + TELEPORT_GUARD_STATE_TTL_MS <= now) {
			it = m_guards.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = m_guards.begin(); m_guards.size() > TELEPORT_MAX_TRACKED_THINGS && it != m_guards.end();) {
		it = m_guards.erase(it);
	}
}

bool TeleportGuard::shouldBlockRate(uint64_t key, uint64_t now) {
	std::scoped_lock lock(m_mutex);
	pruneStates(now);

	auto &state = m_guards[key];
	state.lastSeenMs = now;
	if (state.logDelayMs == 0) {
		state.logDelayMs = TELEPORT_LOG_INITIAL_DELAY_MS;
	}

	if (state.blockedUntilMs > now) {
		return true;
	}

	bool exceededBurst = false;
	if (state.windowStartMs == 0 || state.windowStartMs + TELEPORT_SPAM_WINDOW_MS <= now) {
		state.windowStartMs = now;
		state.windowCount = 1;
	} else {
		++state.windowCount;
		exceededBurst = state.windowCount > TELEPORT_SPAM_LIMIT;
	}

	bool exceededSustained = false;
	if (state.sustainedWindowStartMs == 0 || state.sustainedWindowStartMs + TELEPORT_SUSTAINED_WINDOW_MS <= now) {
		state.sustainedWindowStartMs = now;
		state.sustainedWindowCount = 1;
	} else {
		++state.sustainedWindowCount;
		exceededSustained = state.sustainedWindowCount > TELEPORT_SUSTAINED_LIMIT;
	}

	if (exceededBurst || exceededSustained) {
		state.blockedUntilMs = now + (exceededSustained ? TELEPORT_SUSTAINED_BLOCK_MS : TELEPORT_SPAM_BLOCK_MS);
		return true;
	}
	return false;
}

TeleportGuardLogSnapshot TeleportGuard::recordBlock(uint64_t key, uint64_t now) {
	std::scoped_lock lock(m_mutex);
	pruneStates(now);

	auto &state = m_guards[key];
	state.lastSeenMs = now;
	if (state.logDelayMs == 0) {
		state.logDelayMs = TELEPORT_LOG_INITIAL_DELAY_MS;
	}
	++state.blockedEvents;

	if (state.nextLogMs > now) {
		++state.suppressedLogs;
		return {};
	}

	TeleportGuardLogSnapshot snap;
	snap.shouldLog = true;
	snap.blockedEvents = state.blockedEvents;
	snap.suppressedLogs = state.suppressedLogs;

	state.suppressedLogs = 0;
	state.nextLogMs = now + state.logDelayMs;
	state.logDelayMs = std::min<uint64_t>(state.logDelayMs * 2, TELEPORT_LOG_MAX_DELAY_MS);
	return snap;
}

void TeleportGuard::logBlock(const std::shared_ptr<Thing> &thing, TeleportGuardReason reason, const TeleportGuardLogSnapshot &snap) const {
	if (!snap.shouldLog) {
		return;
	}

	const char* reasonName = reason == TeleportGuardReason::Recursion ? "recursive" : "rate-limited";

	if (const auto creature = thing->getCreature()) {
		g_logger().error(
			"[TeleportGuard] Blocked {} teleport for creature {} at {} (blocked: {}, suppressed: {})",
			reasonName, creature->getName(), creature->getPosition().toString(), snap.blockedEvents, snap.suppressedLogs
		);
		return;
	}
	if (const auto item = thing->getItem()) {
		g_logger().error(
			"[TeleportGuard] Blocked {} teleport for item {} at {} (blocked: {}, suppressed: {})",
			reasonName, item->getID(), item->getPosition().toString(), snap.blockedEvents, snap.suppressedLogs
		);
		return;
	}
	g_logger().error(
		"[TeleportGuard] Blocked {} teleport for unknown thing {} (blocked: {}, suppressed: {})",
		reasonName, fmt::ptr(thing.get()), snap.blockedEvents, snap.suppressedLogs
	);
}

void TeleportGuard::reset() {
	std::scoped_lock lock(m_mutex);
	m_guards.clear();
}

TeleportGuard &g_teleportGuard() {
	static TeleportGuard instance;
	return instance;
}
