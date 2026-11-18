/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

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

	/// Opens `connections` dedicated connections for these queries (databaseTaskConnections), so
	/// they stop sharing the game's connection; 0, or none opened, keeps the shared one. Call once,
	/// after the game's database is connected. Returns how many opened.
	size_t startConnectionPool(size_t connections);
	/// Closes the dedicated connections. Call after the thread pool stopped (no task holds one).
	void shutdown();

	/// True when asynchronous queries have their own connections (startConnectionPool opened some).
	[[nodiscard]] bool hasDedicatedConnections() const;

	/// Runs \`statements\` in one transaction on a dedicated connection (never the game's: another
	/// thread's queries would join the transaction there), once more if it was a deadlock victim.
	/// \`onFinished(committed)\` runs on the worker thread. False (nothing scheduled) without a pool.
	bool executeTransaction(std::vector<std::string> statements, std::function<void(bool committed)> onFinished);

	/// BEGIN, the statements, COMMIT on \`connection\` (which nothing else may use meanwhile); rolled back
	/// on the first failure and run once more if it was a deadlock victim. True when committed.
	static bool commitStatements(Database &connection, const std::vector<std::string> &statements);

private:
	/// Runs `work` on a pooled connection, or on the game's connection when there is no pool or no
	/// connection came free within the pool's wait.
	void withConnection(const std::function<void(Database &)> &work);

	Database &db;
	ThreadPool &threadPool;
	std::unique_ptr<ConnectionPool<Database>> pool;
	// Read by the worker threads; set once by startConnectionPool.
	std::atomic<ConnectionPool<Database>*> activePool { nullptr };
};

constexpr auto g_databaseTasks = DatabaseTasks::getInstance;
