/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "database/schema_hardening.hpp"
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

} // namespace it_schema_hardening
