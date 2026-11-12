/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/monsters/monsters.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/ioprey.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	// Not in the bestiary: the race id a boss gets from bossRaceId.
	constexpr uint16_t BossRaceId = 64001;
	// Registered once for the whole binary; the bestiary and monster registries have no removal.
	constexpr uint16_t BestiaryRaceId = 64002;
	constexpr auto BestiaryCreature = "solid prey selection creature";

	class PreyListSelectionTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);

			player = std::make_shared<Player>();
			player->setGroup(std::make_shared<Group>());
			auto slot = std::make_unique<PreySlot>(PreySlot_First);
			slot->state = PreyDataState_ListSelection;
			ASSERT_TRUE(player->setPreySlotClass(slot));
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		const std::unique_ptr<PreySlot> &slot() const {
			return player->getPreySlotById(PreySlot_First);
		}

		void select(uint16_t raceId) const {
			g_ioprey().parsePreyAction(player, PreySlot_First, PreyAction_ListAll_Selection, PreyOption_None, -1, raceId);
		}

		static void registerBestiaryCreature(bool preyable) {
			auto mType = g_monsters().getMonsterType(BestiaryCreature, true);
			if (!mType) {
				mType = std::make_shared<MonsterType>(BestiaryCreature);
				mType->info.raceid = BestiaryRaceId;
				g_monsters().tryAddMonsterType(BestiaryCreature, mType);
				g_game().addBestiaryList(BestiaryRaceId, BestiaryCreature);
			}
			mType->info.isPreyable = preyable;
		}

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
		std::shared_ptr<Player> player;
	};

} // namespace

// Regression: the window lists the bestiary, but the server accepted any race id. A boss's
// race id has no bestiary entry, slipped past the isPreyable check and gave prey bonuses
// (damage, defense, experience, loot) against that boss.
TEST_F(PreyListSelectionTest, RaceOutsideTheBestiaryIsRefused) {
	select(BossRaceId);

	EXPECT_EQ(PreyDataState_ListSelection, slot()->state);
	EXPECT_EQ(0, slot()->selectedRaceId);
}

TEST_F(PreyListSelectionTest, UnpreyableBestiaryCreatureIsRefused) {
	registerBestiaryCreature(false);

	select(BestiaryRaceId);

	EXPECT_EQ(PreyDataState_ListSelection, slot()->state);
	EXPECT_EQ(0, slot()->selectedRaceId);
}

TEST_F(PreyListSelectionTest, PreyableBestiaryCreatureIsSelected) {
	registerBestiaryCreature(true);

	select(BestiaryRaceId);

	EXPECT_EQ(PreyDataState_Active, slot()->state);
	EXPECT_EQ(BestiaryRaceId, slot()->selectedRaceId);
}
