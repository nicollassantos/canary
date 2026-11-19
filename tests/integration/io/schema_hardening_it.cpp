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
#include "database/schema_hardening.hpp"
#include "utils/tools.hpp"
#include "test_env.hpp"

// ALTER TABLE commits implicitly, so these tests run outside databaseTest and leave the schema as
// the boot step would (wanted indexes present).
namespace it_schema_hardening {

	constexpr std::string_view Table = "market_history";
	constexpr std::string_view Wanted = "solid_market_history_stats";

	std::string currentSchema(Database &db) {
		const auto result = db.storeQuery("SELECT DATABASE() AS `db`");
		return result ? result->getString("db") : std::string {};
	}

	bool indexExists(Database &db, const std::string &schema, std::string_view name) {
		return db.storeQuery(fmt::format(
				   "SELECT 1 FROM `information_schema`.`STATISTICS` WHERE `TABLE_SCHEMA` = {} AND `TABLE_NAME` = '{}' AND `INDEX_NAME` = '{}' LIMIT 1",
				   db.escapeString(schema), Table, name
			   ))
			!= nullptr;
	}

	void dropIfExists(Database &db, const std::string &schema, std::string_view name) {
		if (indexExists(db, schema, name)) {
			EXPECT_TRUE(db.executeQuery(fmt::format("ALTER TABLE `{}` DROP INDEX `{}`", Table, name)));
		}
	}

	TEST(SchemaHardeningTest, LeadingColumnsMatchRegardlessOfName) {
		const std::vector<std::vector<std::string>> existing { { "player_id", "sale" }, { "state", "itemtype", "sale", "tier", "price", "id" } };
		EXPECT_TRUE(schema_hardening::hasLeadingIndex(existing, { "state", "itemtype", "sale", "tier", "price" }));
		EXPECT_TRUE(schema_hardening::hasLeadingIndex(existing, { "player_id" }));
		EXPECT_FALSE(schema_hardening::hasLeadingIndex(existing, { "sale" }));
		EXPECT_FALSE(schema_hardening::hasLeadingIndex(existing, { "player_id", "sale", "itemtype" }));
	}

	TEST(SchemaHardeningTest, CreatesAMissingIndexOnceThenDoesNothing) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		dropIfExists(db, schema, Wanted);

		EXPECT_EQ(1U, schema_hardening::ensureIndexes(db, schema));
		EXPECT_TRUE(indexExists(db, schema, Wanted));

		EXPECT_EQ(0U, schema_hardening::ensureIndexes(db, schema));
	}

	TEST(SchemaHardeningTest, EquivalentIndexUnderAnotherNameIsKept) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		dropIfExists(db, schema, Wanted);
		ASSERT_TRUE(db.executeQuery(fmt::format("ALTER TABLE `{}` ADD INDEX `test_equivalent` (`state`, `itemtype`, `sale`, `tier`, `price`, `id`)", Table)));

		EXPECT_EQ(0U, schema_hardening::ensureIndexes(db, schema));
		EXPECT_FALSE(indexExists(db, schema, Wanted));

		dropIfExists(db, schema, "test_equivalent");
		EXPECT_EQ(1U, schema_hardening::ensureIndexes(db, schema));
	}

	// --- column widening (Phase 28.0) ---

	// 200.150.10.200 in network order, as Connection::getIP returns it: above a signed int(11).
	constexpr uint64_t HighAddress = 3356137160ULL;
	constexpr std::string_view Scratch = "solid_it_widen";

	std::string columnType(Database &db, const std::string &schema, std::string_view table, std::string_view column) {
		const auto result = db.storeQuery(fmt::format(
			"SELECT `COLUMN_TYPE` AS `column_type` FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = {} AND `TABLE_NAME` = '{}' AND `COLUMN_NAME` = '{}'",
			db.escapeString(schema), table, column
		));
		return result ? asLowerCaseString(result->getString("column_type")) : std::string {};
	}

	std::vector<schema_hardening::WantedColumnWidening> scratchWidening() {
		return { { Scratch, "ip", "DECIMAL(39,0) UNSIGNED NOT NULL", "test" } };
	}

	TEST(SchemaHardeningTest, WidenActionFollowsTheColumnType) {
		using enum schema_hardening::WidenAction;
		EXPECT_EQ(Missing, schema_hardening::widenActionFor(""));
		EXPECT_EQ(Widen, schema_hardening::widenActionFor("int"));
		EXPECT_EQ(Widen, schema_hardening::widenActionFor("INT"));
		EXPECT_EQ(Widen, schema_hardening::widenActionFor("bigint"));
		EXPECT_EQ(AlreadyWide, schema_hardening::widenActionFor("decimal"));
		EXPECT_EQ(Unexpected, schema_hardening::widenActionFor("varbinary"));
		EXPECT_EQ(Unexpected, schema_hardening::widenActionFor("varchar"));
	}

	TEST(SchemaHardeningTest, AnIntegerColumnIsWidenedOnceAndKeepsItsValues) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE IF EXISTS `{}`", Scratch)));
		ASSERT_TRUE(db.executeQuery(fmt::format("CREATE TABLE `{}` (`ip` int(11) NOT NULL, PRIMARY KEY (`ip`)) ENGINE=InnoDB", Scratch)));
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `{}` (`ip`) VALUES (16777343)", Scratch)));
		EXPECT_FALSE(db.executeQuery(fmt::format("INSERT INTO `{}` (`ip`) VALUES ({})", Scratch, HighAddress)));

		EXPECT_EQ(1U, schema_hardening::widenColumns(db, schema, scratchWidening()));
		EXPECT_EQ("decimal(39,0) unsigned", columnType(db, schema, Scratch, "ip"));
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `{}` (`ip`) VALUES ({})", Scratch, HighAddress)));
		EXPECT_TRUE(db.storeQuery(fmt::format("SELECT 1 FROM `{}` WHERE `ip` = 16777343", Scratch)) != nullptr);
		EXPECT_EQ(0U, schema_hardening::widenColumns(db, schema, scratchWidening()));

		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE `{}`", Scratch)));
	}

	TEST(SchemaHardeningTest, ASignedNegativeAddressComesBackUnsigned) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE IF EXISTS `{}`", Scratch)));
		ASSERT_TRUE(db.executeQuery(fmt::format("CREATE TABLE `{}` (`ip` int(11) NOT NULL, PRIMARY KEY (`ip`)) ENGINE=InnoDB", Scratch)));
		// The same address stored as a signed 32-bit value.
		ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `{}` (`ip`) VALUES ({})", Scratch, static_cast<int64_t>(HighAddress) - 4294967296LL)));

		EXPECT_EQ(1U, schema_hardening::widenColumns(db, schema, scratchWidening()));
		const auto result = db.storeQuery(fmt::format("SELECT `ip` FROM `{}`", Scratch));
		ASSERT_TRUE(result != nullptr);
		EXPECT_EQ(HighAddress, result->getNumber<uint64_t>("ip"));

		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE `{}`", Scratch)));
	}

	TEST(SchemaHardeningTest, AnUnexpectedTypeIsLeftAlone) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE IF EXISTS `{}`", Scratch)));
		ASSERT_TRUE(db.executeQuery(fmt::format("CREATE TABLE `{}` (`ip` varbinary(16) NOT NULL) ENGINE=InnoDB", Scratch)));

		EXPECT_EQ(0U, schema_hardening::widenColumns(db, schema, scratchWidening()));
		EXPECT_EQ("varbinary(16)", columnType(db, schema, Scratch, "ip"));

		ASSERT_TRUE(db.executeQuery(fmt::format("DROP TABLE `{}`", Scratch)));
	}

	// The real tables: an address whose last octet is 128 or more could not be banned on int(11).
	TEST(SchemaHardeningTest, AfterWideningEveryIpv4CanBeBannedAndIsFound) {
		TestDatabase::init();
		auto &db = g_database();
		const auto schema = currentSchema(db);
		// Back to upstream's type (the bans table is empty outside a test transaction).
		ASSERT_TRUE(db.executeQuery("ALTER TABLE `ip_bans` MODIFY `ip` int(11) NOT NULL"));

		schema_hardening::widenColumns(db, schema);
		EXPECT_EQ("decimal(39,0) unsigned", columnType(db, schema, "ip_bans", "ip"));
		EXPECT_EQ("decimal(39,0) unsigned", columnType(db, schema, "players", "lastip"));
		EXPECT_EQ(0U, schema_hardening::widenColumns(db, schema));

		databaseTest(db, [&db] {
			ASSERT_TRUE(db.executeQuery("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES (98765028, 'widen_acc', 'widen@test.com', '', 1, 0, 0, 0)"));
			ASSERT_TRUE(db.executeQuery("INSERT INTO `players` (`name`, `account_id`, `conditions`, `lastip`) VALUES ('Widen Gm', 98765028, '', 3356137160)"));
			const auto gm = db.getLastInsertId();
			ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `ip_bans` (`ip`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES ({}, 'bot', 1, 0, {})", HighAddress, gm)));

			BanRepositoryDB repository(db);
			const auto ban = repository.findAddressBan(static_cast<uint32_t>(HighAddress));
			ASSERT_TRUE(ban.has_value());
			EXPECT_EQ("bot", ban->reason);
			const auto all = repository.loadAddressBans();
			ASSERT_TRUE(all.has_value());
			bool listed = false;
			for (const auto &entry : *all) {
				listed = listed || entry.ip == static_cast<uint32_t>(HighAddress);
			}
			EXPECT_TRUE(listed);
		})();
	}

} // namespace it_schema_hardening
