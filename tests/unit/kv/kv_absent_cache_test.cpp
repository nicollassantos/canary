/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "kv/kv.hpp"
#include "lib/logging/logger.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <functional>
	#include <future>
	#include <map>
	#include <thread>
	#include <utility>
#endif

namespace {

	// A store whose "database" is a map, counting the loads that reach it.
	class CountingKV final : public KVStore {
	public:
		CountingKV() :
			KVStore(g_logger()) { }

		std::map<std::string, ValueWrapper> database;
		size_t loads = 0;
		std::function<void()> duringLoad; // runs inside load, once
		std::function<void()> duringSave; // runs inside save, once

	protected:
		std::optional<ValueWrapper> load(const std::string &key) override {
			++loads;
			if (auto hook = std::exchange(duringLoad, nullptr)) {
				hook();
			}
			const auto it = database.find(key);
			return it == database.end() ? std::nullopt : std::optional<ValueWrapper>(it->second);
		}

		bool save(const std::string &key, const ValueWrapper &value) override {
			if (auto hook = std::exchange(duringSave, nullptr)) {
				hook();
			}
			if (value.isDeleted()) {
				database.erase(key);
			} else {
				database.insert_or_assign(key, value);
			}
			return true;
		}

		std::vector<std::string> loadPrefix(const std::string & = "") override {
			return {};
		}
	};

} // namespace

// Regression (Phase 25 load test): a missing key queried the database on every get.
TEST(KVAbsentCacheTest, AMissingKeyIsLoadedOnce) {
	CountingKV kv;
	EXPECT_FALSE(kv.get("player.1.last-mount").has_value());
	EXPECT_FALSE(kv.get("player.1.last-mount").has_value());
	EXPECT_FALSE(kv.scoped("player")->scoped("1")->get("last-mount").has_value());
	EXPECT_EQ(1U, kv.loads);
}

TEST(KVAbsentCacheTest, ASetKeyIsNoLongerAbsent) {
	CountingKV kv;
	EXPECT_FALSE(kv.get("streak").has_value());
	kv.set("streak", 3);
	ASSERT_TRUE(kv.get("streak").has_value());
	EXPECT_EQ(3, kv.get("streak")->get<int>());
	EXPECT_EQ(1U, kv.loads);
}

TEST(KVAbsentCacheTest, ARemovedKeyStaysAbsentWithoutLoading) {
	CountingKV kv;
	kv.set("taint", true);
	kv.remove("taint");
	EXPECT_FALSE(kv.get("taint").has_value());
	EXPECT_EQ(0U, kv.loads);
}

TEST(KVAbsentCacheTest, ForceLoadAsksTheDatabaseAgain) {
	CountingKV kv;
	EXPECT_FALSE(kv.get("written-elsewhere").has_value());
	kv.database.insert_or_assign("written-elsewhere", ValueWrapper(7));
	EXPECT_FALSE(kv.get("written-elsewhere").has_value()); // remembered as absent
	ASSERT_TRUE(kv.get("written-elsewhere", true).has_value());
	EXPECT_EQ(7, kv.get("written-elsewhere")->get<int>());
	EXPECT_EQ(2U, kv.loads);
}

TEST(KVAbsentCacheTest, FlushForgetsAbsentKeys) {
	CountingKV kv;
	EXPECT_FALSE(kv.get("key").has_value());
	kv.flush();
	EXPECT_FALSE(kv.get("key").has_value());
	EXPECT_EQ(2U, kv.loads);
}

TEST(KVAbsentCacheTest, TheAbsentSetIsBounded) {
	CountingKV kv;
	for (size_t i = 0; i < KVStore::MAX_ABSENT_KEYS; ++i) {
		(void)kv.get(fmt::format("k{}", i));
	}
	EXPECT_FALSE(kv.get("k0").has_value());
	EXPECT_EQ(KVStore::MAX_ABSENT_KEYS, kv.loads); // still remembered

	(void)kv.get("one-more"); // the set was full: cleared, then this key remembered
	EXPECT_FALSE(kv.get("k0").has_value());
	EXPECT_EQ(KVStore::MAX_ABSENT_KEYS + 2, kv.loads);
}

// Security review: a key leaves memory (eviction, flush) before its save reaches the database. A load
// in that window misses it; remembering that miss would hide the value until the next flush.
TEST(KVAbsentCacheTest, AMissWhileAWriteWasScheduledIsNotRemembered) {
	CountingKV kv;
	kv.duringLoad = [&kv] { kv.flush(); }; // a flush starts while the key is being loaded
	EXPECT_FALSE(kv.get("in-the-window").has_value());
	EXPECT_FALSE(kv.get("in-the-window").has_value());
	EXPECT_EQ(2U, kv.loads); // asked again: the first miss was not trusted
}

TEST(KVAbsentCacheTest, AMissWhileSavesAreInFlightIsNotRemembered) {
	CountingKV kv;
	kv.set("pending", 1);
	kv.duringSave = [&kv] { (void)kv.get("other"); }; // read while the flush is still saving
	kv.flush();
	EXPECT_FALSE(kv.get("other").has_value());
	EXPECT_EQ(2U, kv.loads);
	ASSERT_TRUE(kv.get("pending").has_value()); // the flushed value is read back from the database
	EXPECT_EQ(1, kv.get("pending")->get<int>());
}

// Second review: a save already running when the load starts and finishing during it left the
// counters as they were at the start; the miss was remembered.
TEST(KVAbsentCacheTest, AMissWhileASaveFinishedIsNotRemembered) {
	CountingKV kv;
	kv.set("pending", 1);
	std::promise<void> inSave;
	std::promise<void> proceed;
	auto proceedSignal = proceed.get_future().share();
	kv.duringSave = [&inSave, proceedSignal] {
		inSave.set_value();
		proceedSignal.wait();
	};
	std::thread flusher([&kv] { kv.flush(); });
	inSave.get_future().wait(); // the flush is saving

	kv.duringLoad = [&proceed, &flusher] {
		proceed.set_value(); // the save finishes while this key loads
		flusher.join();
	};
	EXPECT_FALSE(kv.get("other").has_value());
	EXPECT_FALSE(kv.get("other").has_value());
	EXPECT_EQ(2U, kv.loads);
}
