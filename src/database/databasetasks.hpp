/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "database/commit_retry.hpp"
#include "database/connection_pool.hpp"
#include "database/database.hpp"
#include "lib/thread/thread_pool.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <atomic>
	#include <memory>
#endif

class DatabaseTasks {
public:
	DatabaseTasks(ThreadPool &threadPool, Database &db);

	// Ensures that we don't accidentally copy it
	DatabaseTasks(const DatabaseTasks &) = delete;
	DatabaseTasks &operator=(const DatabaseTasks &) = delete;

	static DatabaseTasks &getInstance();

	void execute(const std::string &query, const std::function<void(DBResult_ptr, bool)> &callback = nullptr);
	void store(const std::string &query, const std::function<void(DBResult_ptr, bool)> &callback = nullptr);

	/// Opens a connection like the game's (config.lua); nullptr when it fails.
	using ConnectionFactory = std::function<std::unique_ptr<Database>()>;

	/// Opens `connections` dedicated connections for these queries (databaseTaskConnections), so
	/// they stop sharing the game's connection; 0, or none opened, keeps the shared one. Call once,
	/// after the game's database is connected. Returns how many opened. `factory` is for tests.
	size_t startConnectionPool(size_t connections, ConnectionFactory factory = {});
	/// Opens `connections` connections only for executeTransaction (databaseSaveConnections): the
	/// logout saves and the credits deferred to them. Slow queries on the general pool (highscores,
	/// market statistics) then cannot hold a save past PlayerSaveBarrier::MaxWait. Without them,
	/// transactions use the general pool. Returns how many opened.
	size_t startSavePool(size_t connections, ConnectionFactory factory = {});
	/// Closes the dedicated connections. Call after the thread pool stopped (no task holds one).
	void shutdown();

	/// True when transactions have dedicated connections (the save pool, else the general pool).
	[[nodiscard]] bool hasDedicatedConnections() const;
	/// The pool executeTransaction uses: the save pool when it opened, else the general one.
	[[nodiscard]] ConnectionPool<Database>* transactionPool() const;

	/// Runs `statements` in one transaction on a dedicated connection (never the game's: another
	/// thread's queries would join the transaction there). While the database is unreachable or the
	/// pool has no connection it retries with backoff (commit_retry.hpp), until it commits, a
	/// statement is refused, or the pool closes / the thread pool stops (shutdown).
	/// `onFinished(committed)` runs on the worker thread. False (nothing scheduled) without a pool.
	///
	/// A retry may apply the batch again after part of it autocommitted (a reconnect inside the
	/// transaction), so it must be a snapshot that is safe to repeat: DELETE + INSERT, absolute
	/// UPDATE, upsert (the logout save; only its forge_history INSERT could gain a duplicate row).
	bool executeTransaction(std::vector<std::string> statements, std::function<void(bool committed)> onFinished);

	/// BEGIN, the statements, COMMIT on `connection` (which nothing else may use meanwhile); rolled back
	/// on the first failure and run once more if it was a deadlock victim. RetryLater when the
	/// connection or a lock failed, or the client reconnected inside the transaction.
	static CommitAttempt tryCommit(Database &connection, const std::vector<std::string> &statements);
	/// tryCommit(...) == CommitAttempt::Committed.
	static bool commitStatements(Database &connection, const std::vector<std::string> &statements);

private:
	/// Runs `work` on a pooled connection, or on the game's connection when there is no pool or no
	/// connection came free within the pool's wait.
	void withConnection(const std::function<void(Database &)> &work);

	Database &db;
	ThreadPool &threadPool;
	static std::unique_ptr<ConnectionPool<Database>> openPool(size_t connections, ConnectionFactory factory, std::string_view purpose);

	std::unique_ptr<ConnectionPool<Database>> pool;
	std::unique_ptr<ConnectionPool<Database>> savePool;
	// Read by the worker threads; set once by startConnectionPool / startSavePool.
	std::atomic<ConnectionPool<Database>*> activePool { nullptr };
	std::atomic<ConnectionPool<Database>*> activeSavePool { nullptr };
};

constexpr auto g_databaseTasks = DatabaseTasks::getInstance;
