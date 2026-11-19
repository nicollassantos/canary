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
#include "account/in_memory_account_repository.hpp"
#include "creatures/in_memory_vip_repository.hpp"
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

// --- persistence through the IVipRepository port ---

class PlayerVIPPersistenceTest : public ::testing::Test {
protected:
	static constexpr uint32_t kAccountId = 42;

	void SetUp() override {
		previousContainer = DI::getTestContainer();
		InMemoryLogger::install(injector);
		tests::InMemoryAccountRepository::install(injector);
		InMemoryVipRepository::install(injector);
		DI::setTestContainer(&injector);
		repository = &dynamic_cast<InMemoryVipRepository &>(IVipRepository::getInstance());

		player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		(void)player->setAccount(kAccountId);
		ASSERT_EQ(kAccountId, player->getAccountId());
	}

	void TearDown() override {
		DI::setTestContainer(previousContainer);
	}

	di::extension::injector<> injector {};
	di::extension::injector<>* previousContainer = nullptr;
	InMemoryVipRepository* repository = nullptr;
	std::shared_ptr<Player> player;
};

TEST_F(PlayerVIPPersistenceTest, AddPersistsEntryForAccount) {
	ASSERT_TRUE(player->vip().add(500, "Friend", VipStatus_t::Offline));
	EXPECT_TRUE(repository->hasEntry(kAccountId, 500));
}

TEST_F(PlayerVIPPersistenceTest, RemoveDeletesPersistedEntry) {
	ASSERT_TRUE(player->vip().add(500, "Friend", VipStatus_t::Offline));
	ASSERT_TRUE(player->vip().remove(500));
	EXPECT_FALSE(repository->hasEntry(kAccountId, 500));
}

TEST_F(PlayerVIPPersistenceTest, EditPersistsDescriptionAndGroups) {
	// Creating groups needs free group slots, which only premium accounts have.
	player->setFlag(PlayerFlags_t::IsAlwaysPremium);
	player->vip().addGroup("Friends");
	ASSERT_TRUE(player->vip().add(500, "Friend", VipStatus_t::Offline));
	const auto group = player->vip().getGroupByName("Friends");
	ASSERT_NE(nullptr, group);
	ASSERT_TRUE(repository->hasGroup(group->storedId, kAccountId));

	ASSERT_TRUE(player->vip().edit(500, "best friend", 3, true, { group->id }));
	EXPECT_EQ("best friend", repository->entry(kAccountId, 500).description);
	EXPECT_EQ(3U, repository->entry(kAccountId, 500).icon);
	EXPECT_TRUE(repository->isGroupMember(kAccountId, 500, group->storedId));
}

// A crafted edit lists up to 255 group ids: one write, each existing group once, and the entry leaves the
// groups it is no longer in (in memory too).
TEST_F(PlayerVIPPersistenceTest, EditWritesOnceWithEachExistingGroupOnce) {
	player->setFlag(PlayerFlags_t::IsAlwaysPremium);
	player->vip().addGroup("Friends");
	player->vip().addGroup("Hunters");
	ASSERT_TRUE(player->vip().add(500, "Friend", VipStatus_t::Offline));
	const auto friends = player->vip().getGroupByName("Friends");
	const auto hunters = player->vip().getGroupByName("Hunters");
	ASSERT_NE(nullptr, friends);
	ASSERT_NE(nullptr, hunters);

	std::vector<uint8_t> crafted;
	for (int id = 0; id < 255; ++id) {
		crafted.emplace_back(static_cast<uint8_t>(id));
	}
	crafted.emplace_back(friends->id);
	ASSERT_TRUE(player->vip().edit(500, "note", 1, false, crafted));
	EXPECT_EQ(1U, repository->replaceCalls);
	EXPECT_EQ(player->vip().getGroups().size(), repository->lastReplacedGroups.size()); // every existing group, once

	ASSERT_TRUE(player->vip().edit(500, "note", 1, false, { hunters->id }));
	EXPECT_EQ(2U, repository->replaceCalls);
	EXPECT_EQ(std::vector<uint32_t> { hunters->storedId }, repository->lastReplacedGroups);
	EXPECT_FALSE(friends->vipGroupGuids.contains(500));
	EXPECT_TRUE(hunters->vipGroupGuids.contains(500));
	EXPECT_FALSE(repository->isGroupMember(kAccountId, 500, friends->storedId));
	EXPECT_TRUE(repository->isGroupMember(kAccountId, 500, hunters->storedId));
}

// Groups loaded from the database keep their row id: the default groups come from the account trigger with
// account_vipgroups' global AUTO_INCREMENT (e.g. 7285), while the client sees 1..8. Members, renames and
// removals must use the row id; they used the client id, so default groups never kept their members.
TEST_F(PlayerVIPPersistenceTest, LoadedGroupsAreWrittenByTheirStoredIds) {
	player->setFlag(PlayerFlags_t::IsAlwaysPremium);
	player->vip().addGroupInternal(7285, "Enemies", false);
	player->vip().addGroupInternal(7290, "Raid", true);
	ASSERT_TRUE(player->vip().add(500, "Friend", VipStatus_t::Offline));
	player->vip().addGuidToStoredGroupInternal(7290, 500); // a membership row as loaded

	const auto enemies = player->vip().getGroupByName("Enemies");
	const auto raid = player->vip().getGroupByName("Raid");
	ASSERT_NE(nullptr, enemies);
	ASSERT_NE(nullptr, raid);
	EXPECT_EQ(1, enemies->id); // what the client sees
	EXPECT_EQ(7285U, enemies->storedId);
	EXPECT_TRUE(raid->vipGroupGuids.contains(500));

	ASSERT_TRUE(player->vip().edit(500, "", 0, false, { enemies->id }));
	EXPECT_EQ(std::vector<uint32_t> { 7285 }, repository->lastReplacedGroups);
	EXPECT_TRUE(repository->isGroupMember(kAccountId, 500, 7285));

	const auto created = repository->addVIPGroupEntry(kAccountId, "Raid", true); // the row the load came from
	player->vip().removeGroup(raid->id);
	EXPECT_TRUE(repository->hasGroup(created, kAccountId)); // another row: untouched
}

TEST(PlayerVIPNoAccountTest, EditWithoutAccountDoesNotDereferenceAccount) {
	auto* previous = DI::getTestContainer();
	di::extension::injector<> injector {};
	InMemoryLogger::install(injector);
	InMemoryVipRepository::install(injector);
	DI::setTestContainer(&injector);

	auto player = std::make_shared<Player>();
	player->setGroup(std::make_shared<Group>());
	ASSERT_TRUE(player->vip().addInternal(500));
	EXPECT_TRUE(player->vip().edit(500, "note", 0, false, {}));

	DI::setTestContainer(previous);
}

// --- groups edited from crafted packets ---

// Regression: an unknown group id from the packet dereferenced null and crashed the server.
TEST_F(PlayerVIPTest, EditGroup_UnknownId_IsIgnored) {
	auto p = makePlayer(true);

	EXPECT_NO_FATAL_FAILURE(p->vip().editGroup(200, "Renamed"));
	EXPECT_TRUE(p->vip().getGroups().empty());
}

TEST_F(PlayerVIPTest, EditGroup_DefaultGroupIsNotRenamed) {
	auto p = makePlayer(true);
	p->vip().addGroupInternal(1, "Enemies", false);

	p->vip().editGroup(1, "Renamed");

	EXPECT_EQ("Enemies", p->vip().getGroups().front()->name);
}

TEST_F(PlayerVIPTest, RemoveGroup_DefaultGroupIsKept) {
	auto p = makePlayer(true);
	p->vip().addGroupInternal(1, "Friends", false);

	p->vip().removeGroup(1);

	EXPECT_EQ(1U, p->vip().getGroups().size());
}

TEST_F(PlayerVIPTest, AddGroup_NonPremiumCannotCreateGroups) {
	auto p = makePlayer(false);

	p->vip().addGroup("Hunters");

	EXPECT_TRUE(p->vip().getGroups().empty());
}

TEST_F(PlayerVIPTest, AddGroup_NameIsCappedToTheColumnSize) {
	auto p = makePlayer(true);

	p->vip().addGroup(std::string(PlayerVIP::MaxTextLength * 4, 'g'));

	ASSERT_EQ(1U, p->vip().getGroups().size());
	EXPECT_EQ(PlayerVIP::MaxTextLength, p->vip().getGroups().front()->name.size());
}
