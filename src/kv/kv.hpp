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
	#include <string>
	#include <mutex>
	#include <initializer_list>
	#include <parallel_hashmap/phmap.h>
	#include <optional>
	#include <unordered_set>
	#include <iomanip>
	#include <list>
	#include <utility>
#endif

#include "kv/value_wrapper.hpp"

class KV : public std::enable_shared_from_this<KV> {
public:
	virtual ~KV() = default;
	virtual void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) = 0;
	virtual void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) = 0;
	virtual void set(const std::string &key, const ValueWrapper &value) = 0;

	virtual std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) = 0;

	virtual bool saveAll() {
		return true;
	}

	virtual std::shared_ptr<KV> scoped(const std::string &scope) = 0;

	virtual std::unordered_set<std::string> keys(const std::string &prefix = "") = 0;

	void remove(const std::string &key);

	virtual void flush() {
		saveAll();
	}

	static std::string generateUUID() {
		std::lock_guard<std::mutex> lock(mutex_);

		const auto now = std::chrono::system_clock::now().time_since_epoch();
		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

		if (milliseconds != lastTimestamp_) {
			counter_ = 0;
			lastTimestamp_ = milliseconds;
		} else {
			++counter_;
		}

		std::stringstream ss;
		ss << std::setw(20) << std::setfill('0') << milliseconds << "-"
		   << std::setw(12) << std::setfill('0') << counter_;

		return ss.str();
	}

private:
	static int64_t lastTimestamp_;
	static uint64_t counter_;
	static std::mutex mutex_;
};

class KVStore : public KV {
public:
	static constexpr size_t MAX_SIZE = 1000000;
	/// Keys known to be missing from the database, so a repeated get does not query it again (see get).
	static constexpr size_t MAX_ABSENT_KEYS = 200000;
	static KVStore &getInstance();

	explicit KVStore(Logger &logger) :
		logger(logger) { }

	void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) override;
	void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) override;
	void set(const std::string &key, const ValueWrapper &value) override;

	std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) override;

	void flush() override {
		std::vector<std::pair<std::string, ValueWrapper>> snapshot;
		{
			std::scoped_lock lock(mutex_);
			snapshot.reserve(store_.size() + pendingEvictions_.size());
			for (const auto &[k, v] : store_) {
				snapshot.emplace_back(k, v.first);
			}
			snapshot.insert(snapshot.end(), pendingEvictions_.begin(), pendingEvictions_.end());
			store_.clear();
			pendingEvictions_.clear();
			absent_.clear();
			++writeGeneration_;
			++savesInFlight_;
		}
		for (const auto &[k, v] : snapshot) {
			save(k, v);
		}
		std::scoped_lock lock(mutex_);
		--savesInFlight_;
		++writeGeneration_;
	}

	std::shared_ptr<KV> scoped(const std::string &scope) final;
	std::unordered_set<std::string> keys(const std::string &prefix = "") override;
	/// Full keys that start with `prefix` and end with `suffix`, from the cache and the database (keys() selects
	/// only a prefix: player.<guid>.boss.cooldown.<race> has the guid in the middle). Keys deleted in the cache
	/// are left out.
	std::unordered_set<std::string> keysMatching(const std::string &prefix, const std::string &suffix);

protected:
	phmap::parallel_flat_hash_map<std::string, std::pair<ValueWrapper, std::list<std::string>::iterator>> getStore() {
		std::scoped_lock lock(mutex_);
		phmap::parallel_flat_hash_map<std::string, std::pair<ValueWrapper, std::list<std::string>::iterator>> copy;
		for (const auto &[key, value] : store_) {
			copy.try_emplace(key, value);
		}
		return copy;
	}

protected:
	Logger &logger;

	virtual std::optional<ValueWrapper> load(const std::string &key) = 0;
	virtual bool save(const std::string &key, const ValueWrapper &value) = 0;
	virtual std::vector<std::string> loadPrefix(const std::string &prefix = "") = 0;
	/// Stored full keys that start with `prefix` and end with `suffix` (keysMatching); none by default.
	virtual std::vector<std::string> loadMatching(const std::string & /*prefix*/, const std::string & /*suffix*/) {
		return {};
	}

private:
	void setLocked(const std::string &key, const ValueWrapper &value);
	void processEvictions();

	phmap::parallel_flat_hash_map<std::string, std::pair<ValueWrapper, std::list<std::string>::iterator>> store_;
	std::list<std::string> lruQueue_;
	std::mutex mutex_;
	// Evicted entries pending persistence; accessed under mutex_
	std::vector<std::pair<std::string, ValueWrapper>> pendingEvictions_;
	// Keys a load did not find; accessed under mutex_. Never saved. A get of a missing key used to run
	// a synchronous query every time (every login read the cooldown of every boss and the daily
	// reward streak, every outfit window the last mount, every Soul War kill five taints): ~200
	// queries/s with 100 players in the Phase 25 load test.
	phmap::flat_hash_set<std::string> absent_;
	// A key leaves store_ (eviction, flush) before its save reaches the database; a load in that window
	// misses it. writeGeneration_ moves when a batch of saves starts and when it ends, so a miss is
	// remembered only if no save started or finished while it loaded and none is still running
	// (savesInFlight_ == 0). Both under mutex_.
	uint64_t writeGeneration_ = 0;
	size_t savesInFlight_ = 0;
};

class ScopedKV final : public KV {
public:
	ScopedKV(Logger &logger, KVStore &rootKV, std::string prefix) :
		logger(logger), rootKV_(rootKV), prefix_(std::move(prefix)) { }

	void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) override {
		rootKV_.set(buildKey(key), init_list);
	}
	void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) override {
		rootKV_.set(buildKey(key), init_list);
	}
	void set(const std::string &key, const ValueWrapper &value) override {
		rootKV_.set(buildKey(key), value);
	}

	std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) override {
		return rootKV_.get(buildKey(key), forceLoad);
	}

	template <typename T>
	T get(const std::string &key, bool forceLoad = false) {
		const auto optValue = get(key, forceLoad);
		if (optValue.has_value()) {
			return optValue->get<T>();
		}
		return T {};
	}

	bool saveAll() override {
		return rootKV_.saveAll();
	}

	std::shared_ptr<KV> scoped(const std::string &scope) override {
		logger.trace("ScopedKV::scoped({})", buildKey(scope));
		return std::make_shared<ScopedKV>(logger, rootKV_, buildKey(scope));
	}

	std::unordered_set<std::string> keys(const std::string &prefix = "") override {
		return rootKV_.keys(buildKey(prefix));
	}

private:
	std::string buildKey(const std::string &key) const {
		return fmt::format("{}.{}", prefix_, key);
	}

	Logger &logger;
	KVStore &rootKV_;
	std::string prefix_;
};

constexpr auto g_kv = KVStore::getInstance;
