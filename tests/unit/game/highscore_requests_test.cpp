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
#include "game/game.hpp"
#include "game/highscore/highscore_service.hpp"
#include "io/fake_highscore_repository.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "server/server_definitions.hpp"
#include "test_items.hpp"

namespace {

	constexpr uint32_t AllVocations = 0xFFFFFFFF;

	// HighscoreService asks the repository for a page and handles the rows when they arrive.
	class HighscoreRequestsTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			FakeHighscoreRepository::install(injector);
			DI::setTestContainer(&injector);
			// Player(nullptr) builds an inbox, which needs the item types.
			TestItems::init();
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		void SetUp() override {
			repository = &FakeHighscoreRepository::installed();
			repository->reset();
			player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(77);
			player->setName("Highscore Player");
			player->setID();
		}

		void TearDown() override {
			// The deferred callback resolves the global game; keep it free of test players.
			if (registered) {
				g_game().removePlayer(player);
			}
		}

		void registerInGlobalGame() {
			g_game().addPlayer(player);
			registered = true;
		}

		static HighscorePage pageWithOneRow() {
			HighscorePage page;
			page.page = 1;
			page.total = 1;
			page.characters.emplace_back("Someone", 100, 1, 1, 10, 0, "");
			return page;
		}

		HighscoreService service;
		FakeHighscoreRepository* repository = nullptr;
		std::shared_ptr<Player> player;
		bool registered = false;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(HighscoreRequestsTest, EntriesRequestCarriesCategoryPageAndServerVocation) {
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, static_cast<uint8_t>(HighscoreCategories_t::MAGIC_LEVEL), VOCATION_SORCERER_CIP, "", 3, 20);

	ASSERT_TRUE(repository->lastRequest.has_value());
	EXPECT_FALSE(repository->lastRequest->ourRank);
	EXPECT_EQ("maglevel", repository->lastRequest->categoryName);
	EXPECT_EQ(3U, repository->lastRequest->page);
	EXPECT_EQ(20, repository->lastRequest->entriesPerPage);
	EXPECT_EQ(static_cast<uint32_t>(VOCATION_SORCERER), repository->lastRequest->vocation);
	EXPECT_TRUE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}

TEST_F(HighscoreRequestsTest, OurRankRequestCarriesThePlayerGuid) {
	service.playerHighscores(player, HIGHSCORE_OURRANK, static_cast<uint8_t>(HighscoreCategories_t::EXPERIENCE), AllVocations, "", 1, 20);

	ASSERT_TRUE(repository->lastRequest.has_value());
	EXPECT_TRUE(repository->lastRequest->ourRank);
	EXPECT_EQ("experience", repository->lastRequest->categoryName);
	EXPECT_EQ(77U, repository->lastRequest->playerGUID);
	EXPECT_EQ(AllVocations, repository->lastRequest->vocation);
}

TEST_F(HighscoreRequestsTest, ZeroEntriesPerPageNeverReachesTheRepository) {
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 0);

	EXPECT_FALSE(repository->lastRequest.has_value());
	EXPECT_FALSE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}

// Page 0 only comes from a crafted packet; it must not reach the database.
TEST_F(HighscoreRequestsTest, PageZeroIsRejectedBeforeTheRepository) {
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 0, 20);

	EXPECT_FALSE(repository->lastRequest.has_value());
	EXPECT_FALSE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}

TEST_F(HighscoreRequestsTest, OurRankIgnoresThePageField) {
	service.playerHighscores(player, HIGHSCORE_OURRANK, 0, AllVocations, "", 0, 20);

	EXPECT_TRUE(repository->lastRequest.has_value());
}

TEST_F(HighscoreRequestsTest, UnbuildableQueryLeavesNoPendingTask) {
	repository->buildsQuery = false;

	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 20);

	EXPECT_FALSE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
	EXPECT_EQ(nullptr, repository->pending);
}

TEST_F(HighscoreRequestsTest, PlayerWithRequestInFlightIsIgnored) {
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 20);
	repository->reset();

	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 2, 20);

	EXPECT_FALSE(repository->lastRequest.has_value());
}

TEST_F(HighscoreRequestsTest, DeliveredRowsClearTheTaskThroughTheGlobalGame) {
	registerInGlobalGame();
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 20);
	ASSERT_TRUE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));

	repository->deliver(pageWithOneRow());

	EXPECT_FALSE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}

TEST_F(HighscoreRequestsTest, EmptyResultAlsoClearsTheTask) {
	registerInGlobalGame();
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 20);

	repository->deliver(std::nullopt);

	EXPECT_FALSE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}

TEST_F(HighscoreRequestsTest, RowsForAPlayerWhoLoggedOutAreDropped) {
	service.playerHighscores(player, HIGHSCORE_GETENTRIES, 0, AllVocations, "", 1, 20);

	// The player is not registered in the global game, as after a logout.
	EXPECT_NO_THROW(repository->deliver(pageWithOneRow()));
	EXPECT_TRUE(player->hasAsyncOngoingTask(PlayerAsyncTask_Highscore));
}
