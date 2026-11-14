/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "database/connection_pool.hpp"
#include "database/database.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <atomic>
	#include <thread>
#endif

namespace {

	struct FakeConnection {
		int id = 0;
		bool alive = true;
	};

	using Pool = ConnectionPool<FakeConnection>;
	using namespace std::chrono_literals;

	struct PoolFixture {
		int opened = 0;
		bool factoryFails = false;
		Pool::Clock::time_point clock = Pool::Clock::time_point {} + std::chrono::hours(1);
		std::vector<std::string> replaced;

		Pool::Options options(size_t size) {
			Pool::Options result;
			result.size = size;
			result.waitTimeout = 20ms;
			result.maxLifetime = 30min;
			result.validateAfterIdle = 30s;
			result.now = [this] { return clock; };
			result.onReplace = [this](std::string_view reason, bool) { replaced.emplace_back(reason); };
			return result;
		}

		std::unique_ptr<Pool> make(size_t size) {
			return std::make_unique<Pool>(
				options(size),
				[this]() -> std::unique_ptr<FakeConnection> {
					if (factoryFails) {
						return nullptr;
					}
					return std::make_unique<FakeConnection>(FakeConnection { ++opened, true });
				},
				[](FakeConnection &connection) { return connection.alive; }
			);
		}
	};

} // namespace

TEST(ConnectionPoolTest, LendsDifferentConnectionsAndWaitsOnlyUpToTheTimeout) {
	PoolFixture fixture;
	auto pool = fixture.make(2);
	ASSERT_EQ(2U, pool->start());

	auto first = pool->borrow();
	auto second = pool->borrow();
	ASSERT_TRUE(first && second);
	EXPECT_NE(first->get().id, second->get().id);

	EXPECT_FALSE(pool->borrow().has_value()); // both lent: the third waits 20 ms and gives up
	EXPECT_EQ(1U, pool->stats().timeouts);

	const int id = first->get().id;
	first.reset(); // back to the pool
	auto third = pool->borrow();
	ASSERT_TRUE(third);
	EXPECT_EQ(id, third->get().id);
}

TEST(ConnectionPoolTest, ADeadConnectionIsReplacedAfterIdling) {
	PoolFixture fixture;
	auto pool = fixture.make(1);
	ASSERT_EQ(1U, pool->start());
	{
		auto lease = pool->borrow();
		lease->get().alive = false; // the server dropped it
	}

	fixture.clock += 10s; // not idle long enough to be checked
	EXPECT_EQ(1, pool->borrow()->get().id);

	fixture.clock += 31s;
	auto lease = pool->borrow();
	ASSERT_TRUE(lease);
	EXPECT_EQ(2, lease->get().id);
	EXPECT_EQ((std::vector<std::string> { "dead" }), fixture.replaced);
}

TEST(ConnectionPoolTest, AnOldConnectionIsReplaced) {
	PoolFixture fixture;
	auto pool = fixture.make(1);
	ASSERT_EQ(1U, pool->start());

	fixture.clock += 31min;
	auto lease = pool->borrow();
	ASSERT_TRUE(lease);
	EXPECT_EQ(2, lease->get().id);
	EXPECT_EQ((std::vector<std::string> { "lifetime" }), fixture.replaced);
}

TEST(ConnectionPoolTest, AFailedConnectionIsOpenedAgainLater) {
	PoolFixture fixture;
	fixture.factoryFails = true;
	auto pool = fixture.make(1);
	EXPECT_EQ(0U, pool->start());
	EXPECT_EQ(1U, pool->stats().missing);

	EXPECT_FALSE(pool->borrow().has_value()); // the database is still away
	fixture.factoryFails = false;
	auto lease = pool->borrow();
	ASSERT_TRUE(lease);
	EXPECT_EQ(0U, pool->stats().missing);
}

TEST(ConnectionPoolTest, ShutdownClosesThePool) {
	PoolFixture fixture;
	auto pool = fixture.make(2);
	ASSERT_EQ(2U, pool->start());
	auto lease = pool->borrow();

	pool->shutdown();
	EXPECT_FALSE(pool->borrow().has_value());
	lease.reset(); // closed on return
	EXPECT_EQ(0U, pool->stats().idle);
	EXPECT_EQ(0U, pool->stats().leased);
}

TEST(ConnectionPoolTest, NeverLendsMoreThanItsSizeAcrossThreads) {
	PoolFixture fixture;
	auto pool = fixture.make(3);
	ASSERT_EQ(3U, pool->start());

	std::atomic<int> inUse { 0 };
	std::atomic<int> maxInUse { 0 };
	std::atomic<int> done { 0 };
	std::vector<std::thread> workers;
	for (int t = 0; t < 8; ++t) {
		workers.emplace_back([&] {
			for (int i = 0; i < 200; ++i) {
				if (auto lease = pool->borrow()) {
					const int now = ++inUse;
					int seen = maxInUse.load();
					while (now > seen && !maxInUse.compare_exchange_weak(seen, now)) { }
					--inUse;
					++done;
				}
			}
		});
	}
	for (auto &worker : workers) {
		worker.join();
	}
	EXPECT_LE(maxInUse.load(), 3);
	EXPECT_GT(done.load(), 0);
	EXPECT_EQ(3U, pool->stats().idle);
}

// A deadlock victim is rolled back whole by InnoDB, so the transaction runs once more; any other
// failure does not.
TEST(DeadlockRetryTest, OnlyADeadlockVictimRunsOnceMore) {
	int runs = 0;
	unsigned int error = Database::DeadlockError;
	EXPECT_TRUE(DBTransaction::retryOnceOnDeadlock([&] { return ++runs == 2; }, [&] { return error; }));
	EXPECT_EQ(2, runs);

	runs = 0;
	EXPECT_FALSE(DBTransaction::retryOnceOnDeadlock([&] { ++runs; return false; }, [&] { return error; }));
	EXPECT_EQ(2, runs); // twice at most

	runs = 0;
	error = 1146; // no such table
	EXPECT_FALSE(DBTransaction::retryOnceOnDeadlock([&] { ++runs; return false; }, [&] { return error; }));
	EXPECT_EQ(1, runs);

	runs = 0;
	EXPECT_TRUE(DBTransaction::retryOnceOnDeadlock([&] { ++runs; return true; }, [&] { return error; }));
	EXPECT_EQ(1, runs);
}
