/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/bosstiary/bosstiary_slot_rules.hpp"

// Which boss may go in which Bosstiary slot (a slot boss adds loot rolls in its reward chest). The
// client's slot and boss id used to be applied as sent: any boss, the second slot without its points,
// and removals or swaps without paying. The rules follow the Tibia client: an occupied slot only offers
// "Remove" at its price, the first slot opens with the first Prowess, the second at 1500 boss points.
namespace {
	using bosstiary_slot_rules::SlotChange;
	constexpr uint32_t Boosted = 900;

	bosstiary_slot_rules::SlotState state() {
		bosstiary_slot_rules::SlotState s;
		s.boostedBossId = Boosted;
		s.unlockedBosses = { 101, 102, 103, Boosted };
		s.bossPoints = 2000;
		s.removePrice = 50'000;
		return s;
	}
} // namespace

TEST(BosstiarySlotRulesTest, AnUnlockedBossGoesIntoAnEmptyOpenSlotForFree) {
	const auto decision = bosstiary_slot_rules::decide(state(), 1, 101);
	EXPECT_EQ(SlotChange::Assign, decision.change);
	EXPECT_EQ(0U, decision.price);
}

TEST(BosstiarySlotRulesTest, BossesTheClientMayNotChooseAreRefused) {
	auto s = state();
	s.slotTwo = 102;
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 555).change); // never unlocked
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, Boosted).change); // today's boosted boss
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 102).change); // already in the other slot
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 0x10065).change); // beyond the u16 race ids
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 3, 101).change); // no third slot
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 0, 101).change);
}

TEST(BosstiarySlotRulesTest, TheSecondSlotNeedsItsPointsAndTheFirstAnUnlockedBoss) {
	auto s = state();
	s.bossPoints = bosstiary_slot_rules::SecondSlotPoints - 1;
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 2, 101).change);
	s.bossPoints = bosstiary_slot_rules::SecondSlotPoints;
	EXPECT_EQ(SlotChange::Assign, bosstiary_slot_rules::decide(s, 2, 101).change);

	s.unlockedBosses = {}; // no Prowess yet: the first slot is closed
	EXPECT_FALSE(bosstiary_slot_rules::isSlotUnlocked(1, 0, s.bossPoints));
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 101).change);

	s.unlockedBosses = { Boosted }; // the first Prowess opens it, even on today's boosted boss
	EXPECT_TRUE(bosstiary_slot_rules::isSlotUnlocked(1, 1, s.bossPoints));
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, Boosted).change); // it has its own slot
}

TEST(BosstiarySlotRulesTest, AnOccupiedSlotOnlyRemovesItsBossPaidUnlessItIsTheBoostedOne) {
	auto s = state();
	s.slotOne = 101;
	auto decision = bosstiary_slot_rules::decide(s, 1, 0);
	EXPECT_EQ(SlotChange::Remove, decision.change);
	EXPECT_EQ(50'000U, decision.price);
	EXPECT_TRUE(decision.countsAsChange);

	// No swap in one packet: the client removes (paying) and then selects.
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 103).change);
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 101).change); // already there
	s.slotOne = 0;
	EXPECT_EQ(SlotChange::Refused, bosstiary_slot_rules::decide(s, 1, 0).change); // nothing to remove, nothing paid

	s.slotOne = Boosted; // a boss that became today's boosted one leaves for free, and the change is not counted
	decision = bosstiary_slot_rules::decide(s, 1, 0);
	EXPECT_EQ(SlotChange::Remove, decision.change);
	EXPECT_EQ(0U, decision.price);
	EXPECT_FALSE(decision.countsAsChange);

	s.slotOne = 101; // a free removal (the day's first) still counts
	s.removePrice = 0;
	decision = bosstiary_slot_rules::decide(s, 1, 0);
	EXPECT_EQ(0U, decision.price);
	EXPECT_TRUE(decision.countsAsChange);
}

// Tibia's manual: one free change after each server save, then a price raised with each change that day.
// removeTimes starts at 1 (the column's default, and storeBoostedBoss puts it back each day).
TEST(BosstiarySlotRulesTest, TheFirstChangeOfTheDayIsFreeAndEachNextOneCostsMore) {
	EXPECT_EQ(0U, bosstiary_slot_rules::removalPrice(1));
	EXPECT_EQ(100'000U, bosstiary_slot_rules::removalPrice(2));
	EXPECT_EQ(400'000U, bosstiary_slot_rules::removalPrice(3));
	EXPECT_EQ(700'000U, bosstiary_slot_rules::removalPrice(4));
	EXPECT_EQ(0U, bosstiary_slot_rules::removalPrice(0)); // older rows
	EXPECT_EQ(76'000'000U, bosstiary_slot_rules::removalPrice(255)); // the count saturates there (Player::addRemoveTime)
}

// The window offers exactly the bosses `decide` puts in an empty open slot.
TEST(BosstiarySlotRulesTest, TheWindowOffersExactlyTheBossesThatCanBeAssigned) {
	auto s = state();
	s.slotTwo = 102;
	const auto offered = bosstiary_slot_rules::selectableBosses(s);
	EXPECT_EQ((std::vector<uint16_t> { 101, 103 }), offered); // not the boosted boss, not the one in slot two

	for (const uint32_t boss : { 101U, 102U, 103U, Boosted, 555U }) {
		const bool isOffered = std::ranges::find(offered, static_cast<uint16_t>(boss)) != offered.end();
		EXPECT_EQ(isOffered, bosstiary_slot_rules::decide(s, 1, boss).change == SlotChange::Assign) << boss;
	}

	s.slotOne = 101; // both slots taken: one boss left to offer
	EXPECT_EQ((std::vector<uint16_t> { 103 }), bosstiary_slot_rules::selectableBosses(s));
}

TEST(BosstiarySlotRulesTest, AMasteredSlotBossAddsTwentyFivePercent) {
	EXPECT_EQ(30, bosstiary_slot_rules::slotLootBonus(30, 2));
	EXPECT_EQ(55, bosstiary_slot_rules::slotLootBonus(30, bosstiary_slot_rules::MasteryLevel));
	EXPECT_EQ(25, bosstiary_slot_rules::slotLootBonus(0, 3));
}
