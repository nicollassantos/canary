/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "database/database.hpp"
#include "database/query_recorder.hpp"
#include "game/scheduling/player_save_barrier.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <atomic>
	#include <thread>
#endif

// The logout save is recorded on the game thread and committed elsewhere (SaveManager).
TEST(QueryRecorderTest, WritesOnThisThreadAreRecordedNotRun) {
	Database unconnected; // a run query would fail: "Database not initialized"
	std::vector<std::string> statements;
	{
		query_recorder::Scope scope(statements);
		EXPECT_TRUE(unconnected.executeQuery("DELETE FROM `player_items` WHERE `player_id` = 1"));
		DBInsert insert("INSERT INTO `player_spells` (`player_id`, `name`) VALUES ");
		ASSERT_TRUE(insert.addRow("1, 'exura'"));
		EXPECT_TRUE(insert.execute());
		EXPECT_EQ(nullptr, unconnected.storeQuery("SELECT 1")); // reads still run (and fail here)
	}
	ASSERT_EQ(2U, statements.size());
	EXPECT_EQ("DELETE FROM `player_items` WHERE `player_id` = 1", statements[0]);
	EXPECT_NE(std::string::npos, statements[1].find("INSERT INTO `player_spells`"));

	EXPECT_FALSE(unconnected.executeQuery("SELECT 1")); // the scope ended: run again (and fail)
}

TEST(QueryRecorderTest, OtherThreadsAreNotRecorded) {
	std::vector<std::string> statements;
	query_recorder::Scope scope(statements);
	bool recordedElsewhere = true;
	std::thread other([&recordedElsewhere] { recordedElsewhere = query_recorder::record("UPDATE x"); });
	other.join();
	EXPECT_FALSE(recordedElsewhere);
	EXPECT_TRUE(statements.empty());
}

// Loading a character whose logout save is still committing waits for it (relog, letters, market).
TEST(PlayerSaveBarrierTest, LoadsWaitOnlyForTheirOwnPendingSave) {
	PlayerSaveBarrier barrier;
	EXPECT_TRUE(barrier.waitForGuid(7));

	barrier.begin(7, "Swarm Kalo");
	EXPECT_TRUE(barrier.waitForGuid(8, std::chrono::milliseconds(0)));
	EXPECT_TRUE(barrier.waitForName("Someone Else", std::chrono::milliseconds(0)));

	std::atomic<bool> committed { false };
	std::thread saver([&barrier, &committed] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		committed = true;
		barrier.end(7);
	});
	EXPECT_TRUE(barrier.waitForName("swarm kalo")); // any case
	EXPECT_TRUE(committed.load());
	saver.join();
	EXPECT_EQ(0U, barrier.pending());
}

TEST(PlayerSaveBarrierTest, ALoadByGuidWaitsForTheCommit) {
	PlayerSaveBarrier barrier;
	barrier.begin(9, "Tester Lun");
	std::atomic<bool> committed { false };
	std::thread saver([&barrier, &committed] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		committed = true;
		barrier.end(9);
	});
	EXPECT_TRUE(barrier.waitForGuid(9));
	EXPECT_TRUE(committed.load());
	saver.join();
}

// The waits run on the game thread: a save stuck behind an unreachable database must not stop it.
// Each pending save holds callers only until it is `limit` old; after that every caller is answered
// at once, so a client that keeps relogging cannot stall the game thread again and again.
TEST(PlayerSaveBarrierTest, AStuckSaveHoldsCallersOnlyUntilItsDeadline) {
	using namespace std::chrono_literals;
	PlayerSaveBarrier barrier;
	barrier.begin(5, "Stuck");
	auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(barrier.waitForGuid(5, 50ms));
	const auto firstWait = std::chrono::steady_clock::now() - started;
	EXPECT_GE(firstWait, 40ms);
	EXPECT_LT(firstWait, 2s);

	started = std::chrono::steady_clock::now();
	for (int attempt = 0; attempt < 100; ++attempt) {
		EXPECT_FALSE(barrier.waitForGuid(5, 50ms));
		EXPECT_FALSE(barrier.waitForName("stuck", 50ms));
		EXPECT_FALSE(barrier.waitForAll(50ms));
	}
	EXPECT_LT(std::chrono::steady_clock::now() - started, 40ms); // past the deadline: no more waiting

	EXPECT_EQ(1U, barrier.pending()); // still pending: loads keep being refused until it commits
	barrier.end(5);
	EXPECT_TRUE(barrier.waitForGuid(5, 0ms));
}

// One stuck save marks the database path unhealthy: a fresh pending save is not waited for either,
// so characters logging out during an outage cannot each hold the game thread up to their deadline.
TEST(PlayerSaveBarrierTest, AStuckSaveMakesFresherSavesAnswerAtOnce) {
	using namespace std::chrono_literals;
	PlayerSaveBarrier barrier;
	barrier.begin(1, "Stuck");
	std::this_thread::sleep_for(60ms);
	barrier.begin(2, "Fresh");

	const auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(barrier.waitForGuid(2, 50ms));
	EXPECT_FALSE(barrier.waitForName("fresh", 50ms));
	EXPECT_LT(std::chrono::steady_clock::now() - started, 30ms);
	EXPECT_TRUE(barrier.waitForGuid(3, 50ms)); // no pending save of its own: no conflict

	barrier.end(1); // the stuck save committed: fresh saves are waited for again
	std::thread saver([&barrier] {
		std::this_thread::sleep_for(10ms);
		barrier.end(2);
	});
	EXPECT_TRUE(barrier.waitForGuid(2, 1000ms));
	saver.join();
}

// A bank credit for a character whose logout save is pending is handed to the save (no wait on the
// game thread, no race with the save's absolute balance): the saver commits it, then releases.
TEST(PlayerSaveBarrierTest, ACreditDeferredToAPendingSaveIsCommittedBeforeTheRelease) {
	PlayerSaveBarrier barrier;
	EXPECT_FALSE(barrier.deferUntilSaved(4, "credit A")); // nothing pending: the caller runs it

	barrier.begin(4, "Seller");
	EXPECT_TRUE(barrier.deferUntilSaved(4, "credit A"));
	EXPECT_FALSE(barrier.deferUntilSaved(5, "credit B")); // another character

	// The save batch finished: the deferred credit comes back and the character stays pending.
	EXPECT_EQ((std::vector<std::string> { "credit A" }), barrier.takeDeferredOrRelease(4));
	EXPECT_FALSE(barrier.waitForGuid(4, std::chrono::milliseconds(0)));
	EXPECT_TRUE(barrier.deferUntilSaved(4, "credit C")); // arrived while the credits commit
	EXPECT_EQ((std::vector<std::string> { "credit C" }), barrier.takeDeferredOrRelease(4));

	// Nothing more: released, and loads go through.
	EXPECT_TRUE(barrier.takeDeferredOrRelease(4).empty());
	EXPECT_TRUE(barrier.waitForGuid(4, std::chrono::milliseconds(0)));
	EXPECT_FALSE(barrier.deferUntilSaved(4, "credit D"));
}

// On a failure path the saver releases with end(), which hands back what is still queued to log it.
TEST(PlayerSaveBarrierTest, EndReturnsTheDeferredStatementsLeft) {
	PlayerSaveBarrier barrier;
	barrier.begin(6, "Seller");
	EXPECT_TRUE(barrier.deferUntilSaved(6, "credit"));
	EXPECT_EQ((std::vector<std::string> { "credit" }), barrier.end(6));
	EXPECT_EQ(0U, barrier.pending());
}

// Writes that change a character's row without loading it (increaseBankBalance, scripts) wait too.
TEST(PlayerSaveBarrierTest, AWriteWithoutAGuidWaitsForEveryPendingSave) {
	PlayerSaveBarrier barrier;
	EXPECT_TRUE(barrier.waitForAll());
	barrier.begin(1, "A");
	barrier.begin(2, "B");
	std::atomic<int> ended { 0 };
	std::thread saver([&barrier, &ended] {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		barrier.end(1);
		++ended;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		++ended;
		barrier.end(2);
	});
	EXPECT_TRUE(barrier.waitForAll());
	EXPECT_EQ(2, ended.load());
	saver.join();
}

// Script writes wait for (and fail closed on) pending saves only when they touch a table the save
// rewrites; other tables and reads go straight through.
TEST(PlayerSaveBarrierTest, OnlyWritesToSavedTablesAreHeldBack) {
	EXPECT_TRUE(PlayerSaveBarrier::writesSavedTables("UPDATE `players` SET `balance` = `balance` - 100 WHERE `id` = 7"));
	EXPECT_TRUE(PlayerSaveBarrier::writesSavedTables("  delete from player_items where player_id = 7"));
	EXPECT_TRUE(PlayerSaveBarrier::writesSavedTables("INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES (7, 1, 1)"));
	EXPECT_FALSE(PlayerSaveBarrier::writesSavedTables("SELECT `balance` FROM `players` WHERE `id` = 7"));
	EXPECT_FALSE(PlayerSaveBarrier::writesSavedTables("DELETE FROM `players_online` WHERE `player_id` = 7"));
	EXPECT_FALSE(PlayerSaveBarrier::writesSavedTables("INSERT INTO `player_deaths` (`player_id`) VALUES (7)"));
	EXPECT_FALSE(PlayerSaveBarrier::writesSavedTables("UPDATE `accounts` SET `premdays` = 1"));
}

