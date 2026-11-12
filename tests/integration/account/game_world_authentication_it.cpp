/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/configmanager.hpp"
#include "creatures/players/player_repository_db.hpp"
#include "test_env.hpp"
#include "utils/tools.hpp"

// Game-world login (IOLoginData::gameWorldAuthentication through the player repository):
// the client sends account, password and character; every mismatch must be refused.
namespace it_game_world_auth {

	constexpr uint32_t ClientIp = 0x0100007F;

	uint32_t uniqueSeed() {
		static std::atomic<uint32_t> counter { 1 };
		static const uint32_t base = (static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) & 0x0FFFFFFF) + 70000000;
		return base + counter.fetch_add(1);
	}

	struct Login {
		uint32_t accountId;
		std::string accountName;
		std::string email;
		std::string password;
		std::string character;
	};

	Login createAccountWithCharacter(Database &db, uint64_t deletion = 0) {
		const auto seed = uniqueSeed();
		Login login { seed, fmt::format("auth_acc_{}", seed), fmt::format("auth_{}@test.com", seed), fmt::format("secret-{}", seed), fmt::format("Auth Hero {}", seed) };
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES ({}, '{}', '{}', '{}', 1, 0, 0, 0)", seed, login.accountName, login.email, transformToSHA1(login.password))));
		EXPECT_TRUE(db.executeQuery(fmt::format("INSERT INTO `players` (`name`, `account_id`, `conditions`, `deletion`) VALUES ('{}', {}, '', {})", login.character, seed, deletion)));
		return login;
	}

	class GameWorldAuthenticationTest : public ::testing::Test {
	protected:
		void SetUp() override {
			if (g_configManager().getString(AUTH_TYPE) == "session") {
				GTEST_SKIP() << "authType = \"session\" in the loaded config; these tests cover password login";
			}
		}

		static bool authenticate(const std::string &descriptor, const std::string &password, std::string character, uint32_t &accountId, bool oldProtocol = false) {
			PlayerRepositoryDB repository(g_database());
			return repository.gameWorldAuthentication(descriptor, password, character, accountId, oldProtocol, ClientIp);
		}
	};

	TEST_F(GameWorldAuthenticationTest, CorrectPasswordAndOwnCharacterLogsIn) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto login = createAccountWithCharacter(db);
			uint32_t accountId = 0;

			EXPECT_TRUE(authenticate(login.email, login.password, login.character, accountId));
			EXPECT_EQ(login.accountId, accountId);
		})();
	}

	TEST_F(GameWorldAuthenticationTest, OldProtocolLogsInWithTheAccountName) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto login = createAccountWithCharacter(db);
			uint32_t accountId = 0;

			EXPECT_TRUE(authenticate(login.accountName, login.password, login.character, accountId, true));
			EXPECT_EQ(login.accountId, accountId);
		})();
	}

	TEST_F(GameWorldAuthenticationTest, WrongPasswordIsRefused) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto login = createAccountWithCharacter(db);
			uint32_t accountId = 0;

			EXPECT_FALSE(authenticate(login.email, login.password + "x", login.character, accountId));
		})();
	}

	TEST_F(GameWorldAuthenticationTest, UnknownAccountIsRefused) {
		auto &db = g_database();
		databaseTest(db, [] {
			uint32_t accountId = 0;
			EXPECT_FALSE(authenticate("nobody@test.com", "secret", "Nobody", accountId));
		})();
	}

	TEST_F(GameWorldAuthenticationTest, CharacterOfAnotherAccountIsRefused) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto attacker = createAccountWithCharacter(db);
			const auto victim = createAccountWithCharacter(db);
			uint32_t accountId = 0;

			EXPECT_FALSE(authenticate(attacker.email, attacker.password, victim.character, accountId));
		})();
	}

	TEST_F(GameWorldAuthenticationTest, CharacterNameInOtherCaseStillLogsIn) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto login = createAccountWithCharacter(db);
			uint32_t accountId = 0;

			EXPECT_TRUE(authenticate(login.email, login.password, asLowerCaseString(login.character), accountId));
		})();
	}

	// Regression: the deleted-character check never fired, because the account list skips those
	// characters and the old lookup read a missing name as "not deleted".
	TEST_F(GameWorldAuthenticationTest, CharacterMarkedForDeletionIsRefused) {
		auto &db = g_database();
		databaseTest(db, [&db] {
			const auto login = createAccountWithCharacter(db, static_cast<uint64_t>(getTimeNow()) + 86400);
			uint32_t accountId = 0;

			EXPECT_FALSE(authenticate(login.email, login.password, login.character, accountId));
		})();
	}

} // namespace it_game_world_auth
