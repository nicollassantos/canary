/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "database/commit_retry.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <chrono>
	#include <vector>
#endif

using namespace std::chrono_literals;

namespace {
	// Virtual time: records each backoff instead of sleeping.
	struct RetryRun {
		std::vector<std::chrono::milliseconds> sleeps;
		std::vector<std::chrono::milliseconds> warnings;
		int attempts = 0;
		bool stop = false;

		template <typename Results>
		bool commit(const Results &results, const CommitRetryPolicy &policy = {}) {
			return commitWithRetry(
				[this, &results] {
					const auto index = static_cast<size_t>(attempts++);
					return index < results.size() ? results[index] : results.back();
				},
				[this] { return stop; },
				[this](std::chrono::milliseconds delay) { sleeps.push_back(delay); },
				[this](std::chrono::milliseconds backedOff) { warnings.push_back(backedOff); },
				policy
			);
		}
	};
} // namespace

TEST(CommitRetryTest, KeepsTryingWhileTheDatabaseIsUnreachableAndCommitsWhenItReturns) {
	RetryRun run;
	const std::vector<CommitAttempt> results { CommitAttempt::RetryLater, CommitAttempt::RetryLater, CommitAttempt::RetryLater, CommitAttempt::Committed };
	EXPECT_TRUE(run.commit(results));
	EXPECT_EQ(4, run.attempts);
	// Never spins: it backs off between attempts, doubling.
	EXPECT_EQ((std::vector<std::chrono::milliseconds> { 250ms, 500ms, 1000ms }), run.sleeps);
}

TEST(CommitRetryTest, ARefusedStatementFailsAtOnce) {
	RetryRun run;
	EXPECT_FALSE(run.commit(std::vector<CommitAttempt> { CommitAttempt::Failed }));
	EXPECT_EQ(1, run.attempts);
	EXPECT_TRUE(run.sleeps.empty());
}

TEST(CommitRetryTest, GivesUpWhenTheServerStops) {
	RetryRun run;
	run.stop = true;
	EXPECT_FALSE(run.commit(std::vector<CommitAttempt> { CommitAttempt::RetryLater }));
	EXPECT_EQ(1, run.attempts);
}

TEST(CommitRetryTest, BackoffIsCappedAndAWarningIsLoggedPeriodically) {
	RetryRun run;
	std::vector<CommitAttempt> results(20, CommitAttempt::RetryLater);
	results.push_back(CommitAttempt::Committed);
	const CommitRetryPolicy policy { 250ms, 2000ms, 10000ms };
	EXPECT_TRUE(run.commit(results, policy));
	EXPECT_EQ(2000ms, run.sleeps.back());
	for (const auto delay : run.sleeps) {
		EXPECT_LE(delay, 2000ms);
	}
	// 250 + 500 + 1000 + 17 x 2000 = 35750 ms of backoff: warned at >= 10 s, >= 20 s and >= 30 s.
	ASSERT_EQ(3U, run.warnings.size());
	EXPECT_GE(run.warnings[0], 10000ms);
	EXPECT_GE(run.warnings[2], 30000ms);
}
