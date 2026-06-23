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
#include "items/tile.hpp"

namespace {

	const Position kPos { 100, 100, 7 };

	std::shared_ptr<Player> makeReadyPlayer() {
		auto player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		player->setID();
		auto tile = std::make_shared<DynamicTile>(kPos);
		tile->addThing(player); // sets groundSpeed = 150
		player->setSpeed(0);    // triggers updateCalculatedStepSpeed
		return player;
	}

}

// 14.7a: Enum values are distinct (API contract test)
TEST(WalkStartPolicyTest, EnumValues_AreDistinct) {
	EXPECT_NE(
		Creature::WalkStartPolicy::RespectDelay,
		Creature::WalkStartPolicy::ImmediateWhenReady
	);
}

// 14.7b: No walk delay + stepDuration > 0 → RespectDelay returns full step duration
TEST(WalkStartPolicyTest, RespectDelay_NoActiveDelay_ReturnsFullStepDuration) {
	const auto player = makeReadyPlayer();
	// lastStep == 0 → getWalkDelay() == 0; stepDuration > 0 (baseSpeed=110, groundSpeed=150)
	const int64_t ticks = player->getEventStepTicks(Creature::WalkStartPolicy::RespectDelay);
	EXPECT_GT(ticks, 1) << "RespectDelay must return full step duration, not immediate";
}

// 14.7c: No walk delay + stepDuration > 0 → ImmediateWhenReady returns 1 (immediate)
TEST(WalkStartPolicyTest, ImmediateWhenReady_NoActiveDelay_ReturnsOne) {
	const auto player = makeReadyPlayer();
	const int64_t ticks = player->getEventStepTicks(Creature::WalkStartPolicy::ImmediateWhenReady);
	EXPECT_EQ(1, ticks) << "ImmediateWhenReady must schedule step immediately when no delay is active";
}

// 14.7d: ImmediateWhenReady ticks < RespectDelay ticks (ordering invariant)
TEST(WalkStartPolicyTest, ImmediateWhenReady_IsAlwaysFasterThan_RespectDelay_WhenNoDelay) {
	const auto player = makeReadyPlayer();
	const int64_t immediate = player->getEventStepTicks(Creature::WalkStartPolicy::ImmediateWhenReady);
	const int64_t respectful = player->getEventStepTicks(Creature::WalkStartPolicy::RespectDelay);
	EXPECT_LT(immediate, respectful);
}
