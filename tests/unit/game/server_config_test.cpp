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
#include "game/game.hpp"
#include "game/in_memory_server_config_repository.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "utils/tools.hpp"

// Game reads and writes its server-wide values (motd number, players record, boosted creature)
// through IServerConfigRepository (before, raw SQL in game.cpp).
namespace {
	class ServerConfigTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			tests::InMemoryServerConfigRepository::install(injector);
			DI::setTestContainer(&injector);
			repository = &tests::InMemoryServerConfigRepository::installed();
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		Game game;
		tests::InMemoryServerConfigRepository* repository = nullptr;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

TEST_F(ServerConfigTest, AFreshDatabaseGetsTheDefaults) {
	game.loadMotdNum();
	game.loadPlayersRecord();
	EXPECT_EQ(std::optional<std::string>("0"), repository->find("motd_num"));
	EXPECT_EQ(std::optional<std::string>(""), repository->find("motd_hash"));
	EXPECT_EQ(std::optional<std::string>("0"), repository->find("players_record"));
	EXPECT_EQ(0U, game.getPlayersRecord());
}

TEST_F(ServerConfigTest, AChangedMotdGetsTheNextNumberAndIsSavedWithItsHash) {
	repository->values = { { "motd_num", "7" }, { "motd_hash", "hash of an older motd" }, { "players_record", "321" } };
	game.loadMotdNum();
	game.loadPlayersRecord();
	EXPECT_EQ(8U, game.getMotdNum());
	EXPECT_EQ(321U, game.getPlayersRecord());

	game.saveMotdNum();
	EXPECT_EQ(std::optional<std::string>("8"), repository->find("motd_num"));
	EXPECT_EQ(std::optional<std::string>(transformToSHA1(g_configManager().getString(SERVER_MOTD))), repository->find("motd_hash"));

	game.loadMotdNum(); // same motd now: the number stays
	EXPECT_EQ(8U, game.getMotdNum());
}

TEST_F(ServerConfigTest, TodaysBoostedCreatureIsKept) {
	const auto now = getTimeNow();
	const auto today = static_cast<uint16_t>(localtime(&now)->tm_mday);
	repository->boosted = BoostedCreatureRecord { today, "Dragon", 39 };
	game.loadBoostedCreature();
	EXPECT_EQ("Dragon", game.getBoostedMonsterName());
	EXPECT_EQ(today, repository->boosted->day); // nothing chosen again
}
