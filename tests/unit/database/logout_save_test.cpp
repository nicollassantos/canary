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
	EXPECT_EQ(std::chrono::milliseconds(0), barrier.waitForGuid(7));

	barrier.begin(7, "Swarm Kalo");
	EXPECT_EQ(std::chrono::milliseconds(0), barrier.waitForGuid(8));
	EXPECT_EQ(std::chrono::milliseconds(0), barrier.waitForName("Someone Else"));

	std::atomic<bool> committed { false };
	std::thread saver([&barrier, &committed] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		committed = true;
		barrier.end(7);
	});
	barrier.waitForName("swarm kalo"); // any case
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
	const auto waited = barrier.waitForGuid(9);
	EXPECT_TRUE(committed.load());
	EXPECT_GE(waited.count(), 40);
	saver.join();
}

// Writes that change a character's row without loading it (increaseBankBalance, scripts) wait too.
TEST(PlayerSaveBarrierTest, AWriteWithoutAGuidWaitsForEveryPendingSave) {
	PlayerSaveBarrier barrier;
	EXPECT_EQ(std::chrono::milliseconds(0), barrier.waitForAll());
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
	barrier.waitForAll();
	EXPECT_EQ(2, ended.load());
	saver.join();
}
