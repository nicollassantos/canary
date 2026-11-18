/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "lib/logging/in_memory_logger.hpp"
#include "map/house/house.hpp"
#include "map/in_memory_house_repository.hpp"

// House::setOwner records the owner through IHouseRepository and takes the owner's name and account
// from it (before, it ran SQL directly and could not be tested; tasks/solid_audit.md, phase 3).
namespace {
	class HouseOwnerTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			tests::InMemoryHouseRepository::install(injector);
			DI::setTestContainer(&injector);
			repository = &tests::InMemoryHouseRepository::installed();
			repository->identities[31] = HouseOwnerIdentity { "Owner Name", 8031 };
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		tests::InMemoryHouseRepository* repository = nullptr;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};
} // namespace

TEST_F(HouseOwnerTest, GivingAHouseRecordsItAndTakesTheOwnersNameAndAccount) {
	House house(501);
	house.setOwner(31);

	EXPECT_EQ((std::vector<std::pair<uint32_t, uint32_t>> { { 501, 31 } }), repository->storedOwners);
	EXPECT_EQ(31U, house.getOwner());
	EXPECT_EQ(8031U, house.getOwnerAccountId());
	EXPECT_EQ(CyclopediaHouseState::Rented, house.getState());
}

TEST_F(HouseOwnerTest, LoadingTheStoredOwnerDoesNotWriteItBack) {
	House house(502);
	house.setOwner(31, false);
	EXPECT_TRUE(repository->storedOwners.empty());
	EXPECT_EQ(31U, house.getOwner());
}

TEST_F(HouseOwnerTest, AnOwnerThatDoesNotExistLeavesTheHouseWithoutOwner) {
	House house(503);
	house.setOwner(99);
	EXPECT_EQ((std::vector<std::pair<uint32_t, uint32_t>> { { 503, 99 } }), repository->storedOwners);
	EXPECT_EQ(0U, house.getOwner());
}

TEST_F(HouseOwnerTest, WithoutTransferOnRestartANewOwnerTakesTheHouseAtOnce) {
	House house(504);
	house.setNewOwnerGuid(31, false); // togglehouseTransferOnRestart is off by default
	EXPECT_TRUE(repository->storedNewOwners.empty());
	EXPECT_EQ((std::vector<std::pair<uint32_t, uint32_t>> { { 504, 31 } }), repository->storedOwners);
	EXPECT_EQ(31U, house.getOwner());
}
