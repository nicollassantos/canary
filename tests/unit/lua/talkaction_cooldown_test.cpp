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
#include "lua/creature/talkaction_cooldown.hpp"
#include "test_items.hpp"

namespace {

	std::shared_ptr<Player> makePlayer(uint32_t guid, bool access) {
		TestItems::init(); // Player() creates its inbox from the item catalog
		auto player = std::make_shared<Player>(nullptr);
		player->setGUID(guid);
		player->setID();
		auto group = std::make_shared<Group>();
		group->access = access;
		player->setGroup(group);
		return player;
	}

} // namespace

// Regression: talkactions had no interval, so a client could run one per packet (!transfer loaded
// characters, !livestream wrote a row on every message).
TEST(TalkActionCooldownTest, SecondTalkactionWithinTheIntervalIsRefused) {
	TalkActionCooldown cooldown;
	const auto player = makePlayer(1, false);

	EXPECT_TRUE(cooldown.admit(*player, 10'000));
	EXPECT_FALSE(cooldown.admit(*player, 10'000 + TalkActionCooldown::PlayerIntervalMs - 1));
	EXPECT_TRUE(cooldown.admit(*player, 10'000 + TalkActionCooldown::PlayerIntervalMs));
}

TEST(TalkActionCooldownTest, PlayersAreLimitedSeparately) {
	TalkActionCooldown cooldown;
	const auto first = makePlayer(1, false);
	const auto second = makePlayer(2, false);

	EXPECT_TRUE(cooldown.admit(*first, 10'000));
	EXPECT_TRUE(cooldown.admit(*second, 10'000));
	EXPECT_FALSE(cooldown.admit(*first, 10'001));
}

TEST(TalkActionCooldownTest, StaffIsNotLimited) {
	TalkActionCooldown cooldown;
	const auto gamemaster = makePlayer(1, true);

	EXPECT_TRUE(cooldown.admit(*gamemaster, 10'000));
	EXPECT_TRUE(cooldown.admit(*gamemaster, 10'000));
	EXPECT_TRUE(cooldown.admit(*gamemaster, 10'001));
}
