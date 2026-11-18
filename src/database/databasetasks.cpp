/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "database/databasetasks.hpp"

#include "game/scheduling/dispatcher.hpp"
#include "lib/thread/thread_pool.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/logger.hpp"
#include "lib/metrics/metrics.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <mysql/errmsg.h>
	#include <thread>
#endif

DatabaseTasks::DatabaseTasks(ThreadPool &threadPool, Database &db) :
	db(db), threadPool(threadPool) {
}

DatabaseTasks &DatabaseTasks::getInstance() {
	// DI::port, like Database::getInstance: with no test binding this is the production instance,
	// built on the same Database as g_database(). inject<> would give a test container its own
	// DatabaseTasks on a separate, unconnected Database.
	return DI::port<DatabaseTasks>();
}

size_t DatabaseTasks::startConnectionPool(size_t connections) {
	if (connections == 0 || pool) {
		return 0;
	}

	ConnectionPool<Database>::Options options;
	options.size = connections;
	options.onReplace = [](std::string_view reason, bool replaced) {
		g_logger().warn("[DatabaseTasks] {} connection {}", reason, replaced ? "reopened" : "could not be reopened; using the game connection meanwhile");
		g_metrics().addCounter("database_task_connections_replaced", 1, { { "reason", std::string(reason) }, { "opened", replaced ? "yes" : "no" } });
	};
	auto created = std::make_unique<ConnectionPool<Database>>(
		std::move(options),
		[] {
			auto connection = std::make_unique<Database>();
			return connection->connect() ? std::move(connection) : nullptr;
		},
		[](Database &connection) { return connection.ping(); }
	);
	const auto opened = created->start();
	if (opened == 0) {
		g_logger().warn("[DatabaseTasks] no dedicated connection opened; asynchronous queries keep sharing the game connection");
		return 0;
	}
	pool = std::move(created);
	activePool.store(pool.get());
	g_logger().info("[DatabaseTasks] {} dedicated connection(s) for asynchronous queries", opened);
	return opened;
}

void DatabaseTasks::shutdown() {
	// The pool object stays alive (a late task gets no lease and uses the game connection).
	if (auto* current = activePool.load()) {
		current->shutdown();
	}
}

void DatabaseTasks::withConnection(const std::function<void(Database &)> &work) {
	if (auto* current = activePool.load()) {
		if (auto lease = current->borrow()) {
			work(lease->get());
			return;
		}
		g_metrics().addCounter("database_task_connection_waits_exhausted", 1, {});
	}
	work(db);
}

void DatabaseTasks::execute(const std::string &query, const std::function<void(DBResult_ptr, bool)> &callback /* nullptr */) {
	threadPool.detach_task([this, query, callback]() {
		bool success = false;
		withConnection([&](Database &connection) { success = connection.executeQuery(query); });
		if (callback != nullptr) {
			g_dispatcher().addEvent([callback, success]() { callback(nullptr, success); }, __FUNCTION__);
		}
	});
}

void DatabaseTasks::store(const std::string &query, const std::function<void(DBResult_ptr, bool)> &callback /* nullptr */) {
	threadPool.detach_task([this, query, callback]() {
		DBResult_ptr result;
		withConnection([&](Database &connection) { result = connection.storeQuery(query); });
		if (callback != nullptr) {
			g_dispatcher().addEvent([callback, result]() { callback(result, true); }, __FUNCTION__);
		}
	});
}

bool DatabaseTasks::hasDedicatedConnections() const {
	return activePool.load() != nullptr;
}

CommitAttempt DatabaseTasks::tryCommit(Database &connection, const std::vector<std::string> &statements) {
	// One attempt; on failure the transaction is rolled back and the MySQL error is returned. Through
	// Database's transaction API, so a lost connection fails the rest instead of autocommitting it.
	const auto runOnce = [&connection, &statements]() -> unsigned int {
		const auto lastError = [] {
			const auto error = Database::lastErrorOnThisThread();
			return error != 0 ? error : 1U;
		};
		Database::clearLastErrorOnThisThread();
		if (!connection.beginTransaction()) {
			return lastError();
		}
		const auto session = connection.sessionId();
		for (const auto &statement : statements) {
			if (!connection.executeQuery(statement)) {
				const auto error = lastError();
				connection.rollback();
				return error;
			}
		}
		if (!connection.commit()) {
			return lastError(); // the server ended the transaction
		}
		if (connection.sessionId() != session) {
			// Backstop: the session changed under the transaction, so part of it may have autocommitted.
			// Report a lost connection; the batch is a snapshot and runs again.
			g_metrics().addCounter("database_transaction_reconnected", 1, {});
			return CR_SERVER_LOST;
		}
		return 0;
	};

	auto error = runOnce();
	if (error == Database::DeadlockError) {
		Database::reportDeadlockRetry();
		error = runOnce();
	}
	if (error == 0) {
		return CommitAttempt::Committed;
	}
	const bool transient = Database::isRecoverableError(error) || error == Database::DeadlockError || error == Database::LockWaitTimeoutError;
	return transient ? CommitAttempt::RetryLater : CommitAttempt::Failed;
}

bool DatabaseTasks::commitStatements(Database &connection, const std::vector<std::string> &statements) {
	return tryCommit(connection, statements) == CommitAttempt::Committed;
}

bool DatabaseTasks::executeTransaction(std::vector<std::string> statements, std::function<void(bool committed)> onFinished) {
	auto* const current = activePool.load();
	if (current == nullptr) {
		return false;
	}

	// The worker runs inside this thread pool, which joins its tasks before it is gone.
	auto* const workers = &threadPool;
	threadPool.detach_task([current, workers, statements = std::move(statements), onFinished = std::move(onFinished)]() {
		// Never fall back to the game connection here: wait for a dedicated one.
		const bool committed = commitWithRetry(
			[current, &statements] {
				auto lease = current->borrow();
				return lease ? tryCommit(lease->get(), statements) : CommitAttempt::RetryLater;
			},
			[current, workers] { return current->isClosed() || workers->isStopped(); },
			[](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); },
			[&statements](std::chrono::milliseconds backedOff) {
				g_logger().warn("[DatabaseTasks::executeTransaction] a transaction of {} statements still waits for the database ({} s)", statements.size(), backedOff.count() / 1000);
				g_metrics().addCounter("database_transaction_retry_warnings", 1, {});
			}
		);
		if (onFinished) {
			onFinished(committed);
		}
	});
	return true;
}
