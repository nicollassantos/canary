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
#include "lib/metrics/metrics.hpp"

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
