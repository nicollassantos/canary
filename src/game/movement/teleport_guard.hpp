/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class Thing;

enum class TeleportGuardReason : uint8_t {
	Recursion,
	RateLimit,
};

struct TeleportGuardLogSnapshot {
	bool shouldLog = false;
	uint32_t blockedEvents = 0;
	uint32_t suppressedLogs = 0;
};

class TeleportGuard {
public:
	struct StackCleaner {
		void operator()(const Thing* thing) const;
	};

	using StackGuard = std::unique_ptr<const Thing, StackCleaner>;

	TeleportGuard() = default;
	~TeleportGuard() = default;

	TeleportGuard(const TeleportGuard &) = delete;
	TeleportGuard &operator=(const TeleportGuard &) = delete;

	// Returns an RAII guard owning `thing`'s slot in the recursion stack.
	// Returns nullptr if `thing` is already in the stack (recursion detected).
	StackGuard tryEnterStack(const Thing* thing);

	bool shouldBlockRate(uint64_t key, uint64_t now);
	TeleportGuardLogSnapshot recordBlock(uint64_t key, uint64_t now);

	static uint64_t guardKey(const std::shared_ptr<Thing> &thing, const Thing* rawThing);

	void logBlock(const std::shared_ptr<Thing> &thing, TeleportGuardReason reason, const TeleportGuardLogSnapshot &snapshot) const;

	// Clears all rate-limit state (tests only).
	void reset();

private:
	struct GuardState {
		uint32_t blockedEvents = 0;
		uint32_t suppressedLogs = 0;
		uint64_t nextLogMs = 0;
		uint64_t logDelayMs = 0;
		uint64_t windowStartMs = 0;
		uint16_t windowCount = 0;
		uint64_t sustainedWindowStartMs = 0;
		uint16_t sustainedWindowCount = 0;
		uint64_t blockedUntilMs = 0;
		uint64_t lastSeenMs = 0;
	};

	void pruneStates(uint64_t now);

	std::unordered_map<uint64_t, GuardState> m_guards;
	std::mutex m_mutex;
};

TeleportGuard &g_teleportGuard();
