/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/vip_repository_db.hpp"
#include "test_env.hpp"

namespace it_vip_repo_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 40000000;
		return base + counter.fetch_add(1);
	}

	struct Fixture {
		uint32_t accountId;
		uint32_t friendGuid;
		std::string friendName;
	};

	// Account that owns the VIP list (the oncreate_accounts trigger adds its default groups) plus a target player.
	Fixture createAccountAndFriend(Database &db) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'vip_acc_{}', 'vip_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		const auto name = fmt::format("vip_friend_{}", seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('{}', {}, '')", name, seed)));
		return { seed, static_cast<uint32_t>(db.getLastInsertId()), name };
	}

	uint8_t freeGroupId(VipRepositoryDB &repository, uint32_t accountId) {
		const auto groups = repository.getVIPGroupEntries(accountId, 0);
		uint8_t candidate = 200;
		while (std::ranges::any_of(groups, [candidate](const VIPGroupEntry &group) { return group.id == candidate; })) {
			++candidate;
		}
		return candidate;
	}

	uint64_t groupMemberships(Database &db, uint32_t accountId, uint32_t guid) {
		const auto result = db.storeQuery(fmt::format("SELECT COUNT(*) AS `count` FROM `account_vipgrouplist` WHERE `account_id` = {} AND `player_id` = {}", accountId, guid));
		return result ? result->getNumber<uint64_t>("count") : 0;
	}

	TEST(VipRepositoryDBTest, AddedEntryIsListedWithTargetName) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto fixture = createAccountAndFriend(db);
			VipRepositoryDB repository(db);

			repository.addVIPEntry(fixture.accountId, fixture.friendGuid, "O'Brien's ally", 2, true);

			const auto entries = repository.getVIPEntries(fixture.accountId);
			ASSERT_EQ(1U, entries.size());
			EXPECT_EQ(fixture.friendGuid, entries.front().guid);
			EXPECT_EQ(fixture.friendName, entries.front().name);
			EXPECT_EQ("O'Brien's ally", entries.front().description);
			EXPECT_EQ(2U, entries.front().icon);
			EXPECT_TRUE(entries.front().notify);
		})();
	}

	TEST(VipRepositoryDBTest, EditAndRemoveEntry) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto fixture = createAccountAndFriend(db);
			VipRepositoryDB repository(db);
			repository.addVIPEntry(fixture.accountId, fixture.friendGuid, "old", 1, true);

			repository.editVIPEntry(fixture.accountId, fixture.friendGuid, "new", 3, false);
			const auto edited = repository.getVIPEntries(fixture.accountId);
			ASSERT_EQ(1U, edited.size());
			EXPECT_EQ("new", edited.front().description);
			EXPECT_EQ(3U, edited.front().icon);
			EXPECT_FALSE(edited.front().notify);

			repository.removeVIPEntry(fixture.accountId, fixture.friendGuid);
			EXPECT_TRUE(repository.getVIPEntries(fixture.accountId).empty());
		})();
	}

	TEST(VipRepositoryDBTest, GroupLifecycle) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto fixture = createAccountAndFriend(db);
			VipRepositoryDB repository(db);
			const auto defaults = repository.getVIPGroupEntries(fixture.accountId, 0).size();
			const auto groupId = freeGroupId(repository, fixture.accountId);

			repository.addVIPGroupEntry(groupId, fixture.accountId, "Raid's crew", true);
			auto groups = repository.getVIPGroupEntries(fixture.accountId, 0);
			ASSERT_EQ(defaults + 1, groups.size());
			auto added = std::ranges::find_if(groups, [groupId](const VIPGroupEntry &group) { return group.id == groupId; });
			ASSERT_NE(groups.end(), added);
			EXPECT_EQ("Raid's crew", added->name);
			EXPECT_TRUE(added->customizable);

			repository.editVIPGroupEntry(groupId, fixture.accountId, "Renamed", false);
			groups = repository.getVIPGroupEntries(fixture.accountId, 0);
			added = std::ranges::find_if(groups, [groupId](const VIPGroupEntry &group) { return group.id == groupId; });
			ASSERT_NE(groups.end(), added);
			EXPECT_EQ("Renamed", added->name);
			EXPECT_FALSE(added->customizable);

			repository.removeVIPGroupEntry(groupId, fixture.accountId);
			EXPECT_EQ(defaults, repository.getVIPGroupEntries(fixture.accountId, 0).size());
		})();
	}

	TEST(VipRepositoryDBTest, GroupMembershipIsAddedAndRemoved) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto fixture = createAccountAndFriend(db);
			VipRepositoryDB repository(db);
			const auto groupId = freeGroupId(repository, fixture.accountId);
			repository.addVIPGroupEntry(groupId, fixture.accountId, "Hunters", true);
			repository.addVIPEntry(fixture.accountId, fixture.friendGuid, "", 0, false);

			repository.addGuidVIPGroupEntry(groupId, fixture.accountId, fixture.friendGuid);
			EXPECT_EQ(1U, groupMemberships(db, fixture.accountId, fixture.friendGuid));

			repository.removeGuidVIPGroupEntry(fixture.accountId, fixture.friendGuid);
			EXPECT_EQ(0U, groupMemberships(db, fixture.accountId, fixture.friendGuid));
		})();
	}

} // namespace it_vip_repo_db
