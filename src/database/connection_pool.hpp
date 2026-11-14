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
	#include <chrono>
	#include <condition_variable>
	#include <cstddef>
	#include <cstdint>
	#include <functional>
	#include <memory>
	#include <mutex>
	#include <optional>
	#include <string_view>
	#include <utility>
	#include <vector>
#endif

/// A small pool of database connections, after HikariCP's model: a task borrows a connection, runs
/// its query and gives it back.
///
/// Used by DatabaseTasks so asynchronous queries (market statistics, highscores, Lua
/// db.asyncQuery) neither wait for nor hold the game's own connection: before, they shared it, and
/// the game loop waited whenever one was running. Only independent statements go through the pool
/// (no transactions, no last insert id), so any connection will do.
///
/// - `borrow` waits at most `waitTimeout` for a free connection; nothing means "use your fallback".
/// - A connection idle for `validateAfterIdle` is validated before it is lent; one older than
///   `maxLifetime` is replaced. A dead or failed connection is replaced on the next borrow, so the
///   pool heals after the database comes back.
/// - `shutdown` closes the pool: later borrows get nothing and returned connections are closed.
///   Call it after every worker that may hold a lease has stopped.
template <typename Connection>
class ConnectionPool {
public:
	using Clock = std::chrono::steady_clock;
	/// A connected connection, or nullptr when connecting failed.
	using Factory = std::function<std::unique_ptr<Connection>()>;
	/// False when the connection no longer works.
	using Validate = std::function<bool(Connection &)>;

	struct Options {
		size_t size = 2;
		std::chrono::milliseconds waitTimeout { 5000 };
		std::chrono::milliseconds maxLifetime { std::chrono::minutes(30) };
		std::chrono::milliseconds validateAfterIdle { std::chrono::seconds(30) };
		/// Called (outside the pool's lock) when a connection is replaced; `reason` is "dead",
		/// "lifetime" or "missing".
		std::function<void(std::string_view reason, bool replaced)> onReplace;
		/// Clock for tests.
		std::function<Clock::time_point()> now = [] { return Clock::now(); };
	};

	struct Stats {
		size_t idle = 0;
		size_t leased = 0;
		size_t missing = 0;
		uint64_t timeouts = 0;
	};

	class Lease {
	public:
		Lease(Lease &&other) noexcept :
			pool(std::exchange(other.pool, nullptr)), slot(std::move(other.slot)) { }
		Lease &operator=(Lease &&) = delete;
		Lease(const Lease &) = delete;
		Lease &operator=(const Lease &) = delete;

		~Lease() {
			if (pool) {
				pool->giveBack(std::move(slot));
			}
		}

		Connection &get() const {
			return *slot.connection;
		}

		Connection* operator->() const {
			return slot.connection.get();
		}

	private:
		friend class ConnectionPool;

		struct SlotData {
			std::unique_ptr<Connection> connection;
			Clock::time_point createdAt;
			Clock::time_point returnedAt;
		};

		Lease(ConnectionPool* pool, SlotData slot) :
			pool(pool), slot(std::move(slot)) { }

		ConnectionPool* pool;
		SlotData slot;
	};

	ConnectionPool(Options options, Factory factory, Validate validate) :
		options(std::move(options)), factory(std::move(factory)), validate(std::move(validate)) { }

	ConnectionPool(const ConnectionPool &) = delete;
	ConnectionPool &operator=(const ConnectionPool &) = delete;

	~ConnectionPool() {
		shutdown();
	}

	/// Opens up to `size` connections; returns how many opened. The rest are retried on demand.
	size_t start() {
		size_t opened = 0;
		for (size_t i = 0; i < options.size; ++i) {
			auto connection = factory();
			std::scoped_lock lock(mutex);
			if (connection) {
				const auto now = options.now();
				idle.push_back({ std::move(connection), now, now });
				++opened;
			} else {
				++missing;
			}
		}
		return opened;
	}

	/// A free connection, waiting up to `waitTimeout`; nothing after the timeout or once closed.
	std::optional<Lease> borrow() {
		typename Lease::SlotData slot;
		bool refill = false;
		{
			std::unique_lock lock(mutex);
			if (idle.empty() && missing > 0 && !closed) {
				// A connection failed earlier: try to open it again instead of waiting.
				--missing;
				++leased;
				refill = true;
			} else {
				if (!available.wait_for(lock, options.waitTimeout, [this] { return closed || !idle.empty(); }) || closed) {
					++timeouts;
					return std::nullopt;
				}
				slot = std::move(idle.back());
				idle.pop_back();
				++leased;
			}
		}

		if (refill) {
			auto connection = factory();
			notifyReplace("missing", connection != nullptr);
			if (!connection) {
				std::scoped_lock lock(mutex);
				--leased;
				++missing;
				return std::nullopt;
			}
			const auto now = options.now();
			return Lease(this, { std::move(connection), now, now });
		}

		const auto now = options.now();
		const bool expired = now - slot.createdAt >= options.maxLifetime;
		const bool stale = now - slot.returnedAt >= options.validateAfterIdle;
		if (expired || (stale && !validate(*slot.connection))) {
			slot.connection.reset();
			auto connection = factory();
			notifyReplace(expired ? "lifetime" : "dead", connection != nullptr);
			if (!connection) {
				std::scoped_lock lock(mutex);
				--leased;
				++missing;
				return std::nullopt;
			}
			slot = { std::move(connection), now, now };
		}
		return Lease(this, std::move(slot));
	}

	/// Closes idle connections; leases still out are closed when they come back.
	void shutdown() {
		std::vector<typename Lease::SlotData> closing;
		{
			std::scoped_lock lock(mutex);
			closed = true;
			closing.swap(idle);
		}
		available.notify_all();
	}

	[[nodiscard]] Stats stats() const {
		std::scoped_lock lock(mutex);
		return { idle.size(), leased, missing, timeouts };
	}

private:
	void giveBack(typename Lease::SlotData slot) {
		{
			std::scoped_lock lock(mutex);
			--leased;
			if (closed) {
				return; // the connection closes with `slot`
			}
			slot.returnedAt = options.now();
			idle.push_back(std::move(slot));
		}
		available.notify_one();
	}

	void notifyReplace(std::string_view reason, bool replaced) const {
		if (options.onReplace) {
			options.onReplace(reason, replaced);
		}
	}

	Options options;
	Factory factory;
	Validate validate;

	mutable std::mutex mutex;
	std::condition_variable available;
	std::vector<typename Lease::SlotData> idle;
	size_t leased = 0;
	size_t missing = 0;
	uint64_t timeouts = 0;
	bool closed = false;
};
