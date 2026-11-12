/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "config/config_port.hpp"
#include "game/cyclopedia/cyclopedia_service.hpp"
#include "game/game.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	// CyclopediaService::updatePlayersOnline hands the online GUIDs to the player repository.
	class CyclopediaPlayersOnlineTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryPlayerRepository::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		void SetUp() override {
			repository = &dynamic_cast<InMemoryPlayerRepository &>(IPlayerRepository::getInstance());
			repository->reset();
		}

		std::shared_ptr<Player> addOnlinePlayer(uint32_t guid) {
			auto player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(guid);
			player->setName("Online " + std::to_string(guid));
			player->setID();
			game.addPlayer(player);
			return player;
		}

		static bool loggedError(const std::string &text) {
			const auto &logger = dynamic_cast<InMemoryLogger &>(g_logger());
			for (size_t index = 0; index < logger.logCount(); ++index) {
				const auto [level, message] = logger.getLogEntry(index);
				if (level == "error" && message.find(text) != std::string::npos) {
					return true;
				}
			}
			return false;
		}

		Game game;
		CyclopediaService service { game, IConfigManager::getInstance() };
		InMemoryPlayerRepository* repository = nullptr;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(CyclopediaPlayersOnlineTest, NoPlayersOnlineWritesAnEmptyList) {
	service.updatePlayersOnline();

	EXPECT_EQ(1U, repository->onlineUpdates);
	EXPECT_TRUE(repository->onlinePlayers.empty());
}

TEST_F(CyclopediaPlayersOnlineTest, OnlinePlayersAreWrittenByGuid) {
	addOnlinePlayer(11);
	addOnlinePlayer(12);

	service.updatePlayersOnline();

	auto written = repository->onlinePlayers;
	std::ranges::sort(written);
	EXPECT_EQ((std::vector<uint32_t> { 11, 12 }), written);
}

TEST_F(CyclopediaPlayersOnlineTest, FailedUpdateIsLogged) {
	repository->failOnlineUpdate = true;

	service.updatePlayersOnline();

	EXPECT_TRUE(loggedError("Failed to update players online"));
}
