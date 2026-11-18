/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/in_memory_player_progress_repository.hpp"
#include "creatures/players/components/player_forge_history.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "lib/logging/in_memory_logger.hpp"

// PlayerForgeHistory hands what was removed and added to the repository, and forgets it only once
// the save succeeded (a failed save keeps it for the next one).
namespace {
	class ForgeHistorySaveTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			tests::InMemoryPlayerProgressRepository::install(injector);
			DI::setTestContainer(&injector);
			repository = &tests::InMemoryPlayerProgressRepository::installed();

			player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(77);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		static ForgeHistory entry(uint32_t id, const std::string &description) {
			ForgeHistory history;
			history.id = id;
			history.description = description;
			history.createdAt = 1;
			return history;
		}

		tests::InMemoryPlayerProgressRepository* repository = nullptr;
		std::shared_ptr<Player> player;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

TEST_F(ForgeHistorySaveTest, NothingChangedSavesNothing) {
	PlayerForgeHistory history(*player);
	EXPECT_TRUE(history.save());
	EXPECT_EQ(0, repository->forgeSaves);
}

TEST_F(ForgeHistorySaveTest, ChangesAreSavedOnceAndAFailedSaveKeepsThem) {
	repository->forgeHistory[77] = { entry(1, "old") };
	PlayerForgeHistory history(*player);
	ASSERT_TRUE(history.load());
	ASSERT_EQ(1U, history.get().size());

	history.remove(1);
	history.add(entry(0, "new"));
	repository->failSaves = true;
	EXPECT_FALSE(history.save());
	repository->failSaves = false;
	EXPECT_TRUE(history.save());
	ASSERT_EQ(1U, repository->forgeHistory[77].size());
	EXPECT_EQ("new", repository->forgeHistory[77].front().description);

	EXPECT_TRUE(history.save());
	EXPECT_EQ(2, repository->forgeSaves); // the third save had nothing to write
}
