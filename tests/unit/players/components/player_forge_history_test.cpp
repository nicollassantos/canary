/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/player_forge_history.hpp"
#include "creatures/players/player.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PlayerForgeHistoryTest : public ::testing::Test {
public:
	static void SetUpTestSuite() {
		previousContainer = DI::getTestContainer();
		InMemoryLogger::install(injector);
		DI::setTestContainer(&injector);
	}

	static void TearDownTestSuite() {
		DI::setTestContainer(previousContainer);
	}

protected:
	void SetUp() override {
		player = std::make_shared<Player>();
	}

	static ForgeHistory makeHistory(uint32_t id, uint64_t createdAt, bool success = true) {
		ForgeHistory h;
		h.id = id;
		h.createdAt = createdAt;
		h.success = success;
		h.description = "test";
		return h;
	}

	std::shared_ptr<Player> player;

	inline static di::extension::injector<> injector {};
	inline static di::extension::injector<>* previousContainer = nullptr;
};

// --- get ---

TEST_F(PlayerForgeHistoryTest, Get_ReturnsEmpty_Initially) {
	EXPECT_TRUE(player->forgeHistory().get().empty());
}

// --- add ---

TEST_F(PlayerForgeHistoryTest, Add_IncreasesHistorySize) {
	player->forgeHistory().add(makeHistory(1, 1000));
	EXPECT_EQ(1u, player->forgeHistory().get().size());
}

TEST_F(PlayerForgeHistoryTest, Add_StoresCorrectFields) {
	auto h = makeHistory(7, 5000, false);
	h.description = "fusion";
	player->forgeHistory().add(h);

	const auto &stored = player->forgeHistory().get().front();
	EXPECT_EQ(7u, stored.id);
	EXPECT_EQ(5000u, stored.createdAt);
	EXPECT_FALSE(stored.success);
	EXPECT_EQ("fusion", stored.description);
}

TEST_F(PlayerForgeHistoryTest, Add_MultipleEntries_AllStored) {
	player->forgeHistory().add(makeHistory(1, 100));
	player->forgeHistory().add(makeHistory(2, 200));
	player->forgeHistory().add(makeHistory(3, 300));
	EXPECT_EQ(3u, player->forgeHistory().get().size());
}

TEST_F(PlayerForgeHistoryTest, Add_DeduplicatesTimestamp_WhenCreatedAtCollides) {
	player->forgeHistory().add(makeHistory(1, 1000));
	player->forgeHistory().add(makeHistory(2, 1000)); // same createdAt → bumped to 1001
	player->forgeHistory().add(makeHistory(3, 1000)); // bumped to 1002

	const auto &history = player->forgeHistory().get();
	ASSERT_EQ(3u, history.size());

	std::set<uint64_t> timestamps;
	for (const auto &h : history) {
		timestamps.insert(h.createdAt);
	}
	EXPECT_EQ(3u, timestamps.size()); // all unique
	EXPECT_TRUE(timestamps.contains(1000));
	EXPECT_TRUE(timestamps.contains(1001));
	EXPECT_TRUE(timestamps.contains(1002));
}

// --- remove ---

TEST_F(PlayerForgeHistoryTest, Remove_ReducesHistorySize) {
	player->forgeHistory().add(makeHistory(1, 100));
	player->forgeHistory().add(makeHistory(2, 200));
	player->forgeHistory().remove(1);
	EXPECT_EQ(1u, player->forgeHistory().get().size());
}

TEST_F(PlayerForgeHistoryTest, Remove_RemovesCorrectEntry) {
	player->forgeHistory().add(makeHistory(1, 100));
	player->forgeHistory().add(makeHistory(2, 200));
	player->forgeHistory().remove(1);

	const auto &history = player->forgeHistory().get();
	ASSERT_EQ(1u, history.size());
	EXPECT_EQ(2u, history.front().id);
}

TEST_F(PlayerForgeHistoryTest, Remove_IsNoOp_WhenIdNotFound) {
	player->forgeHistory().add(makeHistory(1, 100));
	player->forgeHistory().remove(999);
	EXPECT_EQ(1u, player->forgeHistory().get().size());
}
