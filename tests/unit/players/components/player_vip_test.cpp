/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	class PlayerVIPTest : public ::testing::Test {
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
		static std::shared_ptr<Player> makePlayer(bool premium = false) {
			auto p = std::make_shared<Player>();
			auto group = std::make_shared<Group>();
			p->setGroup(group);
			if (premium) {
				p->setFlag(PlayerFlags_t::IsAlwaysPremium);
			}
			return p;
		}

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- getMaxEntries ---

TEST_F(PlayerVIPTest, GetMaxEntries_Returns20_WhenNotPremium) {
	auto player = makePlayer(false);
	EXPECT_EQ(20u, player->vip().getMaxEntries());
}

TEST_F(PlayerVIPTest, GetMaxEntries_Returns100_WhenPremium) {
	auto player = makePlayer(true);
	EXPECT_EQ(100u, player->vip().getMaxEntries());
}

TEST_F(PlayerVIPTest, GetMaxEntries_UsesGroupLimit_WhenGroupMaxVipEntriesIsNonZero) {
	auto player = makePlayer(false);
	player->getGroup()->maxVipEntries = 150;
	EXPECT_EQ(150u, player->vip().getMaxEntries());
}

// --- getMaxGroupEntries ---

TEST_F(PlayerVIPTest, GetMaxGroupEntries_Returns0_WhenNotPremium) {
	auto player = makePlayer(false);
	EXPECT_EQ(0u, player->vip().getMaxGroupEntries());
}

TEST_F(PlayerVIPTest, GetMaxGroupEntries_Returns8_WhenPremium) {
	auto player = makePlayer(true);
	EXPECT_EQ(8u, player->vip().getMaxGroupEntries());
}

// --- exists / addInternal ---

TEST_F(PlayerVIPTest, Exists_ReturnsFalse_WhenGuidNotAdded) {
	auto player = makePlayer();
	EXPECT_FALSE(player->vip().exists(42));
}

TEST_F(PlayerVIPTest, AddInternal_AddsGuidAndExistsReturnsTrue) {
	auto player = makePlayer();
	EXPECT_TRUE(player->vip().addInternal(42));
	EXPECT_TRUE(player->vip().exists(42));
}

TEST_F(PlayerVIPTest, AddInternal_ReturnsFalse_WhenDuplicate) {
	auto player = makePlayer();
	player->vip().addInternal(42);
	EXPECT_FALSE(player->vip().addInternal(42));
}

// --- getFreeId ---

TEST_F(PlayerVIPTest, GetFreeId_Returns1_WhenNoGroupsAdded) {
	auto player = makePlayer();
	EXPECT_EQ(1u, player->vip().getFreeId());
}

TEST_F(PlayerVIPTest, GetFreeId_Returns0_WhenAllSlotsFull) {
	auto player = makePlayer();
	for (uint8_t i = 1; i <= 8; ++i) {
		player->vip().addGroupInternal(i, "group" + std::to_string(i), false);
	}
	EXPECT_EQ(0u, player->vip().getFreeId());
}

// --- addGroupInternal / getGroupByID / getGroupByName ---

TEST_F(PlayerVIPTest, AddGroupInternal_AddsGroup) {
	auto player = makePlayer();
	player->vip().addGroupInternal(1, "Friends", false);
	EXPECT_NE(nullptr, player->vip().getGroupByID(1));
}

TEST_F(PlayerVIPTest, GetGroupByID_ReturnsNull_WhenIdNotFound) {
	auto player = makePlayer();
	EXPECT_EQ(nullptr, player->vip().getGroupByID(5));
}

TEST_F(PlayerVIPTest, GetGroupByName_ReturnsGroup_WhenNameExists) {
	auto player = makePlayer();
	player->vip().addGroupInternal(1, "Friends", true);
	EXPECT_NE(nullptr, player->vip().getGroupByName("Friends"));
}

TEST_F(PlayerVIPTest, GetGroupByName_ReturnsNull_WhenNameNotFound) {
	auto player = makePlayer();
	EXPECT_EQ(nullptr, player->vip().getGroupByName("Nonexistent"));
}

TEST_F(PlayerVIPTest, AddGroupInternal_DoesNotAddDuplicateName) {
	auto player = makePlayer();
	player->vip().addGroupInternal(1, "Friends", false);
	player->vip().addGroupInternal(2, "Friends", false); // duplicate name — should be rejected
	EXPECT_EQ(nullptr, player->vip().getGroupByID(2));
}

// --- getGroupsIdGuidBelongs ---

TEST_F(PlayerVIPTest, GetGroupsIdGuidBelongs_ReturnsEmpty_WhenGuidInNoGroup) {
	auto player = makePlayer();
	player->vip().addGroupInternal(1, "Friends", false);
	EXPECT_TRUE(player->vip().getGroupsIdGuidBelongs(999).empty());
}

TEST_F(PlayerVIPTest, GetGroupsIdGuidBelongs_ReturnsAllGroups_WhenGuidAddedToMultiple) {
	auto player = makePlayer();
	player->vip().addGroupInternal(1, "Friends", false);
	player->vip().addGroupInternal(2, "Family", false);
	player->vip().addGuidToGroupInternal(1, 777);
	player->vip().addGuidToGroupInternal(2, 777);

	auto groups = player->vip().getGroupsIdGuidBelongs(777);
	ASSERT_EQ(2u, groups.size());
	EXPECT_TRUE(std::ranges::find(groups, uint8_t { 1 }) != groups.end());
	EXPECT_TRUE(std::ranges::find(groups, uint8_t { 2 }) != groups.end());
}
