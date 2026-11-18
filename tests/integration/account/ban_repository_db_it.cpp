/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "account/ban_repository_db.hpp"
#include "test_env.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <atomic>
	#include <chrono>
	#include <vector>
#endif

namespace it_ban_repository_db {

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 60000000;
		return base + counter.fetch_add(1);
	}

	struct Stored {
		uint32_t accountId;
		uint32_t guid;
		std::string name;
	};

	Stored createPlayer(Database &db) {
		const auto seed = uniqueSeed();
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, 'ban_acc_{}', 'ban_{}@test.com', '', 1, 0, 0, 0)", seed, seed, seed)));
		const auto name = fmt::format("Ban Gm {}", seed);
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ({}, {}, '')", db.escapeString(name), seed)));
		return { seed, static_cast<uint32_t>(db.getLastInsertId()), name };
	}

	// The cleanup of expired bans and the clock, made visible.
	class RecordingBanRepository final : public BanRepositoryDB {
	public:
		using BanRepositoryDB::BanRepositoryDB;
		std::vector<std::string> later;
		time_t clock = 1'800'000'000;

	protected:
		void runLater(const std::string &query) override {
			later.push_back(query);
		}
		time_t now() const override {
			return clock;
		}
	};

	TEST(BanRepositoryDBTest, AnActiveAccountBanNamesWhoBannedAndWhy) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto gm = createPlayer(db);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `account_bans` (`account_id`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES ({}, 'It''s botting', 1, 1900000000, {})", gm.accountId, gm.guid)));
			RecordingBanRepository repository(db);

			const auto ban = repository.findAccountBan(gm.accountId);
			ASSERT_TRUE(ban.has_value());
			EXPECT_EQ(gm.name, ban->bannedBy);
			EXPECT_EQ("It's botting", ban->reason);
			EXPECT_EQ(1'900'000'000, ban->expiresAt);
			EXPECT_TRUE(repository.later.empty());
			EXPECT_FALSE(repository.findAccountBan(gm.accountId + 1).has_value());
		})();
	}

	TEST(BanRepositoryDBTest, AnExpiredAccountBanIsNotReturnedAndMovesToTheHistory) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto gm = createPlayer(db);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `account_bans` (`account_id`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES ({}, 'old', 1, 1700000000, {})", gm.accountId, gm.guid)));
			RecordingBanRepository repository(db);

			EXPECT_FALSE(repository.findAccountBan(gm.accountId).has_value());
			ASSERT_EQ(2U, repository.later.size());
			for (const auto &query : repository.later) {
				EXPECT_TRUE(db.executeQuery(query)) << query;
			}
			EXPECT_EQ(nullptr, db.storeQuery(fmt::format("SELECT 1 FROM `account_bans` WHERE `account_id` = {}", gm.accountId)));
			EXPECT_NE(nullptr, db.storeQuery(fmt::format("SELECT 1 FROM `account_ban_history` WHERE `account_id` = {} AND `reason` = 'old' AND `expired_at` = 1700000000", gm.accountId)));
		})();
	}

	TEST(BanRepositoryDBTest, AddressBansAreActiveUntilTheyExpireAndPermanentOnesStay) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto gm = createPlayer(db);
			const uint32_t temporary = 0x00ABCDEF;
			const uint32_t permanent = 0x00ABCDF0;
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `ip_bans` (`ip`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES ({}, 'flood', 1, 1700000000, {}), ({}, 'abuse', 1, 0, {})", temporary, gm.guid, permanent, gm.guid)));
			RecordingBanRepository repository(db);

			EXPECT_FALSE(repository.findAddressBan(0).has_value());
			const auto stays = repository.findAddressBan(permanent);
			ASSERT_TRUE(stays.has_value());
			EXPECT_EQ(0, stays->expiresAt);
			EXPECT_EQ("abuse", stays->reason);

			repository.clock = 1'600'000'000; // before the temporary ban expires
			EXPECT_TRUE(repository.findAddressBan(temporary).has_value());
			repository.clock = 1'800'000'000;
			EXPECT_FALSE(repository.findAddressBan(temporary).has_value());
			ASSERT_EQ(1U, repository.later.size());
			EXPECT_TRUE(db.executeQuery(repository.later.front()));
			EXPECT_EQ(nullptr, db.storeQuery(fmt::format("SELECT 1 FROM `ip_bans` WHERE `ip` = {}", temporary)));
		})();
	}

	TEST(BanRepositoryDBTest, AllAddressBansLoadAndAnEmptyTableIsNotAFailure) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto gm = createPlayer(db);
			ASSERT_TRUE(db.executeQuery("DELETE FROM `ip_bans`"));
			RecordingBanRepository repository(db);
			const auto none = repository.loadAddressBans();
			ASSERT_TRUE(none.has_value());
			EXPECT_TRUE(none->empty());

			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `ip_bans` (`ip`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES (1001, 'a', 1, 0, {}), (1002, 'b', 1, 1700000000, {})", gm.guid, gm.guid)));
			const auto all = repository.loadAddressBans();
			ASSERT_TRUE(all.has_value());
			ASSERT_EQ(2U, all->size());
			for (const auto &[ip, ban] : *all) {
				EXPECT_EQ(gm.name, ban.bannedBy);
				EXPECT_EQ(ip == 1001 ? 0 : 1'700'000'000, ban.expiresAt);
			}

			// The removal deletes the row only while it is still expired.
			ASSERT_TRUE(db.executeQuery("UPDATE `ip_bans` SET `expires_at` = 1900000000 WHERE `ip` = 1001"));
			repository.forgetExpiredAddressBan(1001); // banned again since: stays
			repository.forgetExpiredAddressBan(1002); // still expired: goes
			ASSERT_EQ(2U, repository.later.size());
			for (const auto &query : repository.later) {
				EXPECT_TRUE(db.executeQuery(query)) << query;
			}
			EXPECT_NE(nullptr, db.storeQuery("SELECT 1 FROM `ip_bans` WHERE `ip` = 1001"));
			EXPECT_EQ(nullptr, db.storeQuery("SELECT 1 FROM `ip_bans` WHERE `ip` = 1002"));
		})();
	}

	TEST(BanRepositoryDBTest, ANamelockIsFoundByCharacter) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto gm = createPlayer(db);
			const auto target = createPlayer(db);
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `player_namelocks` (`player_id`, `reason`, `namelocked_at`, `namelocked_by`) VALUES ({}, 'name', 1, {})", target.guid, gm.guid)));
			RecordingBanRepository repository(db);

			EXPECT_TRUE(repository.isNamelocked(target.guid));
			EXPECT_FALSE(repository.isNamelocked(gm.guid));
		})();
	}

} // namespace it_ban_repository_db
