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
#include "database/databasetasks.hpp"
#include "test_env.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <chrono>
	#include <cstdlib>
	#include <fstream>
	#include <map>
	#include <set>
	#include <thread>
#endif

namespace {

	// The same credentials TestDatabase uses (TEST_ENV_FILE or tests/test.env, OS env first).
	std::map<std::string, std::string> testEnv() {
		std::map<std::string, std::string> values;
		std::string path = TESTS_ENV_DEFAULT;
		if (const char* file = std::getenv("TEST_ENV_FILE"); file && *file) {
			path = file;
		}
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line)) {
			const auto equals = line.find('=');
			if (line.empty() || line[0] == '#' || equals == std::string::npos) {
				continue;
			}
			auto value = line.substr(equals + 1);
			if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
				value = value.substr(1, value.size() - 2);
			}
			values[line.substr(0, equals)] = value;
		}
		for (const auto* key : { "TEST_DB_HOST", "TEST_DB_USER", "TEST_DB_PASSWORD", "TEST_DB_NAME", "TEST_DB_PORT", "TEST_DB_SOCKET" }) {
			if (const char* value = std::getenv(key); value && *value) {
				values[key] = value;
			}
		}
		return values;
	}

	std::unique_ptr<Database> openConnection() {
		auto env = testEnv();
		const auto get = [&env](const char* key, const char* fallback) {
			const auto it = env.find(key);
			return it == env.end() || it->second.empty() ? std::string(fallback) : it->second;
		};
		const auto host = get("TEST_DB_HOST", "127.0.0.1");
		const auto user = get("TEST_DB_USER", "root");
		const auto pass = get("TEST_DB_PASSWORD", "");
		const auto name = get("TEST_DB_NAME", "canary_test");
		const auto sock = get("TEST_DB_SOCKET", "");
		const auto port = static_cast<uint32_t>(std::strtoul(get("TEST_DB_PORT", "3306").c_str(), nullptr, 10));
		auto connection = std::make_unique<Database>();
		return connection->connect(&host, &user, &pass, &name, port, &sock) ? std::move(connection) : nullptr;
	}

	uint64_t connectionId(Database &connection) {
		const auto result = connection.storeQuery("SELECT CONNECTION_ID() AS `id`");
		return result ? result->getNumber<uint64_t>("id") : 0;
	}

	ConnectionPool<Database>::Options poolOptions(size_t size) {
		ConnectionPool<Database>::Options options;
		options.size = size;
		options.waitTimeout = std::chrono::milliseconds(200);
		return options;
	}

} // namespace

// DatabaseTasks' asynchronous queries run on these connections instead of the game's one.
TEST(ConnectionPoolIT, PooledConnectionsAreNotTheGameConnection) {
	ConnectionPool<Database> pool(poolOptions(2), [] { return openConnection(); }, [](Database &connection) { return connection.ping(); });
	ASSERT_EQ(2U, pool.start());

	auto first = pool.borrow();
	auto second = pool.borrow();
	ASSERT_TRUE(first && second);
	const std::set<uint64_t> ids { connectionId(g_database()), connectionId(first->get()), connectionId(second->get()) };
	EXPECT_EQ(3U, ids.size());
	EXPECT_FALSE(ids.contains(0));
}

// A connection the server dropped (KILL) works again when it is next lent: it is validated after
// idling (here at once), and the client reconnects or the pool opens a new one.
TEST(ConnectionPoolIT, AConnectionTheServerDroppedWorksAgain) {
	auto options = poolOptions(1);
	options.validateAfterIdle = std::chrono::milliseconds(0);
	ConnectionPool<Database> pool(std::move(options), [] { return openConnection(); }, [](Database &connection) { return connection.ping(); });
	ASSERT_EQ(1U, pool.start());

	uint64_t killed = 0;
	{
		auto lease = pool.borrow();
		ASSERT_TRUE(lease);
		killed = connectionId(lease->get());
	}
	ASSERT_NE(0U, killed);
	ASSERT_TRUE(g_database().executeQuery(fmt::format("KILL {}", killed)));

	auto lease = pool.borrow();
	ASSERT_TRUE(lease);
	const auto now = connectionId(lease->get());
	EXPECT_NE(0U, now);
	EXPECT_NE(killed, now);
}

// DBTransaction retries a deadlock victim from the error the failed query left on its thread.
TEST(ConnectionPoolIT, AFailedQueryLeavesItsErrorOnTheThread) {
	Database::clearLastErrorOnThisThread();
	EXPECT_EQ(nullptr, g_database().storeQuery("SELECT * FROM `no_such_table_for_this_test`"));
	EXPECT_EQ(1146U, Database::lastErrorOnThisThread()); // ER_NO_SUCH_TABLE
}

// The logout save commits its recorded statements on a pooled connection: all or nothing.
TEST(ConnectionPoolIT, RecordedStatementsCommitTogetherOrNotAtAll) {
	auto connection = openConnection();
	ASSERT_NE(nullptr, connection);
	ASSERT_TRUE(connection->executeQuery("CREATE TABLE IF NOT EXISTS `fork_logout_save_it` (`id` INT NOT NULL PRIMARY KEY) ENGINE=InnoDB"));
	ASSERT_TRUE(connection->executeQuery("DELETE FROM `fork_logout_save_it`"));

	EXPECT_TRUE(DatabaseTasks::commitStatements(*connection, { "INSERT INTO `fork_logout_save_it` VALUES (1)", "INSERT INTO `fork_logout_save_it` VALUES (2)" }));
	auto count = connection->storeQuery("SELECT COUNT(*) AS `n` FROM `fork_logout_save_it`");
	ASSERT_NE(nullptr, count);
	EXPECT_EQ(2U, count->getNumber<uint32_t>("n"));

	// The second statement fails (duplicate key): the first is rolled back with it.
	EXPECT_FALSE(DatabaseTasks::commitStatements(*connection, { "INSERT INTO `fork_logout_save_it` VALUES (3)", "INSERT INTO `fork_logout_save_it` VALUES (1)" }));
	count = connection->storeQuery("SELECT COUNT(*) AS `n` FROM `fork_logout_save_it`");
	ASSERT_NE(nullptr, count);
	EXPECT_EQ(2U, count->getNumber<uint32_t>("n"));

	EXPECT_TRUE(connection->executeQuery("DROP TABLE `fork_logout_save_it`"));
}

namespace {
	// Kills `session` from another connection after `delay`, while the statement there still runs.
	std::thread killLater(uint64_t session, std::chrono::milliseconds delay) {
		return std::thread([session, delay] {
			std::this_thread::sleep_for(delay);
			auto killer = openConnection();
			ASSERT_NE(nullptr, killer);
			std::ignore = killer->executeQuery(fmt::format("KILL {}", session));
		});
	}

	uint32_t rowsIn(Database &connection, const std::string &table) {
		auto count = connection.storeQuery(fmt::format("SELECT COUNT(*) AS `n` FROM `{}`", table));
		return count ? count->getNumber<uint32_t>("n") : 0;
	}
} // namespace

// A connection lost inside a transaction must not reconnect and autocommit the rest (half a save:
// the "delete old items" rolled back, the "insert current items" kept). DBTransaction on the game's
// connection: periodic, shutdown, house and guild saves.
TEST(DatabaseTransactionIT, AGameTransactionThatLosesItsConnectionWritesNothing) {
	auto &db = g_database();
	ASSERT_TRUE(db.executeQuery("CREATE TABLE IF NOT EXISTS `fork_tx_lost_game_it` (`id` INT NOT NULL PRIMARY KEY) ENGINE=InnoDB"));
	ASSERT_TRUE(db.executeQuery("DELETE FROM `fork_tx_lost_game_it`"));

	auto killer = killLater(db.sessionId(), std::chrono::milliseconds(400));
	const bool committed = DBTransaction::executeWithinTransaction([&db] {
		std::ignore = db.executeQuery("INSERT INTO `fork_tx_lost_game_it` VALUES (1)");
		std::ignore = db.executeQuery("SELECT SLEEP(3)");
		std::ignore = db.executeQuery("INSERT INTO `fork_tx_lost_game_it` VALUES (2)");
		return true;
	});
	killer.join();

	EXPECT_FALSE(committed);
	EXPECT_EQ(0U, rowsIn(db, "fork_tx_lost_game_it"));
	EXPECT_TRUE(db.executeQuery("DROP TABLE `fork_tx_lost_game_it`"));
}

