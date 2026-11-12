/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/config_enums.hpp"
#include "config/in_memory_config_manager.hpp"
#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "enums/player_cyclopedia.hpp"
#include "game/cyclopedia/cyclopedia_service.hpp"
#include "game/game.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "map/house/house.hpp"

namespace {

	constexpr uint32_t HouseClientId = 64202;
	constexpr uint32_t OfflineGuid = 41;
	constexpr auto OfflineName = "Offline Heir";

	// The transfer request names its recipient; resolving an offline name loads the whole
	// character from the database on the dispatcher, so it must come after the cheap checks.
	class HouseAuctionTransferTest : public ::testing::Test {
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

			config.setBoolean(CYCLOPEDIA_HOUSE_AUCTION, true);

			house = game.map.houses.addHouse(HouseClientId);
			game.map.houses.addHouseClientId(HouseClientId, house);

			sender = std::make_shared<Player>(nullptr);
			sender->setGroup(std::make_shared<Group>());
			sender->setGUID(8);
			sender->setName("Sender");
			sender->setID();
			game.addPlayer(sender);

			service.now = [this] { return clock; };
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		bool loggedWarning(const std::string &text) const {
			const auto &logger = dynamic_cast<InMemoryLogger &>(DI::get<Logger>());
			return std::ranges::any_of(logger.logs, [&text](const auto &entry) {
				return entry.level == "warning" && entry.message.find(text) != std::string::npos;
			});
		}

		void transferTo(const std::string &name) {
			service.playerCyclopediaHouseTransfer(sender->getID(), HouseClientId, 0, name, 1'000);
		}

		Game game;
		InMemoryConfigManager config;
		CyclopediaService service { game, config };
		InMemoryPlayerRepository* repository = nullptr;
		std::shared_ptr<House> house;
		std::shared_ptr<Player> sender;
		int64_t clock = 1'000'000;

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// Regression: any player could make the server load an offline character per packet.
TEST_F(HouseAuctionTransferTest, NonOwnerDoesNotLoadTheRecipient) {
	house->setState(CyclopediaHouseState::Rented);

	transferTo(OfflineName);

	EXPECT_EQ(0U, repository->playerLoads);
	EXPECT_EQ(CyclopediaHouseState::Rented, house->getState());
	EXPECT_TRUE(house->getBidderName().empty());
}

TEST_F(HouseAuctionTransferTest, HouseNotRentedDoesNotLoadTheRecipient) {
	transferTo(OfflineName);

	EXPECT_EQ(0U, repository->playerLoads);
	EXPECT_EQ(CyclopediaHouseState::Available, house->getState());
}

TEST_F(HouseAuctionTransferTest, OverlongRecipientNameIsRefused) {
	transferTo(std::string(NETWORKMESSAGE_PLAYERNAME_MAXLENGTH + 1, 'a'));

	EXPECT_TRUE(loggedWarning("recipient name"));
	EXPECT_EQ(0U, repository->playerLoads);
}

TEST_F(HouseAuctionTransferTest, TransferRequestsAreThrottledPerPlayer) {
	transferTo(OfflineName);
	transferTo(OfflineName);
	EXPECT_EQ(1U, service.throttledHouseTransfers());

	clock += CyclopediaService::HouseTransferCooldownMs;
	transferTo(OfflineName);
	EXPECT_EQ(1U, service.throttledHouseTransfers());
}
