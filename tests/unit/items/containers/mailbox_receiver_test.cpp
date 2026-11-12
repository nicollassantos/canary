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
#include "creatures/players/player.hpp"
#include "items/containers/mailbox/mailbox.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	constexpr uint32_t OfflineGuid = 51;
	constexpr auto OfflineName = "Offline Recipient";

	// A letter or parcel for an offline player loads the whole character (and saves it after
	// delivery); the label is player-written text, so a name that matches nobody must not.
	class MailboxReceiverTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryPlayerRepository::install(injector);
			DI::setTestContainer(&injector);
			repository = &dynamic_cast<InMemoryPlayerRepository &>(IPlayerRepository::getInstance());
			repository->reset();

			auto offline = std::make_shared<Player>(nullptr);
			offline->setGUID(OfflineGuid);
			offline->setName(OfflineName);
			repository->savePlayer(offline);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		bool loggedError() const {
			const auto &logger = dynamic_cast<InMemoryLogger &>(DI::get<Logger>());
			return std::ranges::any_of(logger.logs, [](const auto &entry) {
				return entry.level == "error";
			});
		}

		InMemoryPlayerRepository* repository = nullptr;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// Regression: each push of a letter to an unknown name ran a full player load and logged an
// error carrying the label text.
TEST_F(MailboxReceiverTest, UnknownNameIsNotLoaded) {
	EXPECT_EQ(nullptr, Mailbox::resolveReceiver("Nobody Here"));

	EXPECT_EQ(0U, repository->playerLoads);
	EXPECT_FALSE(loggedError());
}

TEST_F(MailboxReceiverTest, OverlongOrEmptyNameIsNotLoaded) {
	EXPECT_EQ(nullptr, Mailbox::resolveReceiver(std::string(NETWORKMESSAGE_PLAYERNAME_MAXLENGTH + 1, 'a')));
	EXPECT_EQ(nullptr, Mailbox::resolveReceiver(""));

	EXPECT_EQ(0U, repository->playerLoads);
}

TEST_F(MailboxReceiverTest, ExistingOfflinePlayerIsLoadedForDelivery) {
	const auto receiver = Mailbox::resolveReceiver(OfflineName);

	ASSERT_NE(nullptr, receiver);
	EXPECT_EQ(OfflineGuid, receiver->getGUID());
	EXPECT_EQ(1U, repository->playerLoads);
}
