/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "kv/kv_sql.hpp"
#include "lib/logging/logger.hpp"
#include "test_env.hpp"

namespace it_kv_sql {

	// keysMatching through MySQL: the prefix and the suffix are literal ('_' and '%' are not wildcards), and
	// the suffix is exact (race 2367 does not match 23671).
	TEST(KVSQLTest, KeysMatchingAPrefixAndASuffixAreExact) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			for (const auto *key : { "fork_it.1.boss.cooldown.2367", "fork_it.22.boss.cooldown.2367", "fork_it.1.boss.cooldown.23671",
			                         "fork_it.1.boss.cooldown.1234", "forkXit.3.boss.cooldown.2367", "fork_it.5.boss.cooldownX2367" }) {
				ASSERT_TRUE(db.executeQuery(fmt::format("INSERT INTO `kv_store` (`key_name`, `timestamp`, `value`) VALUES ({}, 0, '')", db.escapeString(key))));
			}

			KVSQL kv(db, g_logger());
			const auto keys = kv.keysMatching("fork_it.", ".boss.cooldown.2367");
			EXPECT_EQ((std::unordered_set<std::string> { "fork_it.1.boss.cooldown.2367", "fork_it.22.boss.cooldown.2367" }), keys);
		})();
	}

} // namespace it_kv_sql
