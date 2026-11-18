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
	#include <algorithm>
	#include <chrono>
	#include <cstdint>
#endif

/// Outcome of one attempt to commit a batch of statements.
enum class CommitAttempt : uint8_t {
	Committed,
	/// No connection, or the connection or a lock failed: the batch did not commit, try it again.
	RetryLater,
	/// The database refused a statement: another attempt would fail the same way.
	Failed,
};

struct CommitRetryPolicy {
	std::chrono::milliseconds firstBackoff { 250 };
	std::chrono::milliseconds maxBackoff { 5000 };
	std::chrono::milliseconds warnEvery { 30000 };
};

/// Repeats `attempt()` until it commits or fails for good, or `shouldStop()` (server going down).
/// Sleeps between attempts with exponential backoff, so an unreachable database is not hammered
/// and the worker does not spin, and calls `warn(backedOff)` every `warnEvery` of backoff while
/// it keeps retrying. True when committed.
template <typename Attempt, typename ShouldStop, typename Sleep, typename Warn>
bool commitWithRetry(Attempt &&attempt, ShouldStop &&shouldStop, Sleep &&sleep, Warn &&warn, const CommitRetryPolicy &policy = {}) {
	auto backoff = policy.firstBackoff;
	std::chrono::milliseconds backedOff { 0 };
	std::chrono::milliseconds sinceWarning { 0 };
	while (true) {
		switch (attempt()) {
			case CommitAttempt::Committed:
				return true;
			case CommitAttempt::Failed:
				return false;
			case CommitAttempt::RetryLater:
				break;
		}
		if (shouldStop()) {
			return false;
		}
		sleep(backoff);
		backedOff += backoff;
		sinceWarning += backoff;
		if (sinceWarning >= policy.warnEvery) {
			warn(backedOff);
			sinceWarning = std::chrono::milliseconds(0);
		}
		backoff = std::min(backoff * 2, policy.maxBackoff);
	}
}
