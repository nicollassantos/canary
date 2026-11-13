/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/livestream/livestream.hpp"
#include "enums/object_category.hpp"
#include "game/chat/typing_throttle.hpp"
#include "game/loot/loot_service.hpp"

// Second hostile-client audit (Phase 23.5): rules for values a client controls.

// Regression: every 0x38 went to every spectator, with no interval.
TEST(TypingThrottleTest, OnlyChangesAreBroadcastAndStartingIsLimited) {
	TypingThrottle throttle;
	EXPECT_TRUE(throttle.shouldBroadcast(1, true, 10'000));
	EXPECT_FALSE(throttle.shouldBroadcast(1, true, 10'001)); // no change
	EXPECT_TRUE(throttle.shouldBroadcast(1, false, 10'002)); // stopping always passes
	EXPECT_FALSE(throttle.shouldBroadcast(1, true, 10'003)); // started again too soon
	EXPECT_FALSE(throttle.shouldBroadcast(1, false, 10'004)); // no change: still "not typing"
	EXPECT_TRUE(throttle.shouldBroadcast(1, true, 10'000 + TypingThrottle::StartIntervalMs));
}

TEST(TypingThrottleTest, AFloodBroadcastsAtMostTwicePerSecond) {
	TypingThrottle throttle;
	int broadcasts = 0;
	for (int64_t ms = 0; ms < 1000; ++ms) {
		if (throttle.shouldBroadcast(7, ms % 2 == 0, 50'000 + ms)) {
			++broadcasts;
		}
	}
	EXPECT_EQ(2, broadcasts);
}

TEST(TypingThrottleTest, AStaleTypingStateDoesNotHideTheNextStart) {
	TypingThrottle throttle;
	EXPECT_TRUE(throttle.shouldBroadcast(4, true, 1'000)); // then the player logged out typing
	EXPECT_FALSE(throttle.shouldBroadcast(4, true, 1'000 + TypingThrottle::StaleAfterMs - 1));
	EXPECT_TRUE(throttle.shouldBroadcast(4, true, 1'000 + TypingThrottle::StaleAfterMs));
}

TEST(TypingThrottleTest, AFirstStopIsNotBroadcast) {
	TypingThrottle throttle;
	EXPECT_FALSE(throttle.shouldBroadcast(3, false, 1'000));
}

TEST(TypingThrottleTest, StaleStatesArePrunedAtTheThreshold) {
	TypingThrottle throttle;
	for (uint32_t id = 0; id < TypingThrottle::PruneThreshold; ++id) {
		EXPECT_TRUE(throttle.shouldBroadcast(id, true, 1'000));
	}
	EXPECT_EQ(TypingThrottle::PruneThreshold, throttle.size());
	// Still "typing", but not updated for StaleAfterMs: dropped before the new entry.
	EXPECT_TRUE(throttle.shouldBroadcast(999'999, true, 1'000 + TypingThrottle::StaleAfterMs));
	EXPECT_EQ(1U, throttle.size());
}

// Regression: the category byte became `1 << category` (undefined from 32 on) and a map key.
TEST(LootServiceRulesTest, OnlyEnumCategoriesNameAManagedContainer) {
	EXPECT_TRUE(LootService::isManagedContainerCategory(OBJECTCATEGORY_GOLD));
	EXPECT_TRUE(LootService::isManagedContainerCategory(OBJECTCATEGORY_DEFAULT));
	EXPECT_TRUE(LootService::isManagedContainerCategory(OBJECTCATEGORY_ARMORS));
	EXPECT_FALSE(LootService::isManagedContainerCategory(static_cast<ObjectCategory_t>(26)));
	EXPECT_FALSE(LootService::isManagedContainerCategory(static_cast<ObjectCategory_t>(32)));
	EXPECT_FALSE(LootService::isManagedContainerCategory(static_cast<ObjectCategory_t>(200)));
	EXPECT_FALSE(LootService::isManagedContainerCategory(static_cast<ObjectCategory_t>(255)));
}

// Regression: the quick-loot list took up to 65535 entries, searched for every looted item.
TEST(LootServiceRulesTest, QuickLootListIsDeduplicatedAndCapped) {
	std::vector<uint16_t> list { 3031, 3035, 3031, 3043, 3035 };
	ASSERT_TRUE(LootService::normalizeQuickLootList(QUICKLOOTFILTER_ACCEPTEDLOOT, list));
	EXPECT_EQ((std::vector<uint16_t> { 3031, 3035, 3043 }), list);

	std::vector<uint16_t> atCap(LootService::MaxQuickLootListSize, 1);
	EXPECT_TRUE(LootService::normalizeQuickLootList(QUICKLOOTFILTER_SKIPPEDLOOT, atCap));

	std::vector<uint16_t> tooLong(LootService::MaxQuickLootListSize + 1, 1);
	EXPECT_FALSE(LootService::normalizeQuickLootList(QUICKLOOTFILTER_SKIPPEDLOOT, tooLong));

	std::vector<uint16_t> any { 1 };
	EXPECT_FALSE(LootService::normalizeQuickLootList(static_cast<QuickLootFilter_t>(2), any));
}

// Regression: /name was announced to the whole session with no interval, and could take the
// caster's name or an online player's.
TEST(LivestreamRenameRulesTest, RenameNeedsTheIntervalAndAFreeName) {
	constexpr int64_t now = 100'000;
	EXPECT_EQ(std::nullopt, LivestreamManager::viewerRenameRefusal("Watcher", "Caster Bob", false, 0, now));
	EXPECT_NE(std::nullopt, LivestreamManager::viewerRenameRefusal("Watcher", "Caster Bob", false, now - 1, now));
	EXPECT_EQ(std::nullopt, LivestreamManager::viewerRenameRefusal("Watcher", "Caster Bob", false, now - LivestreamManager::ViewerRenameIntervalMs, now));
	EXPECT_NE(std::nullopt, LivestreamManager::viewerRenameRefusal("caster bob", "Caster Bob", false, 0, now));
	EXPECT_NE(std::nullopt, LivestreamManager::viewerRenameRefusal("Some Player", "Caster Bob", true, 0, now));
}
