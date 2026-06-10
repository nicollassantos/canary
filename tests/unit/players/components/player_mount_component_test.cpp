/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/appearance/mounts/mounts.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "injection_fixture.hpp"
#include "utils/const.hpp"

class PlayerMountComponentTest : public ::testing::Test {
protected:
	void SetUp() override {
		player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		fixture.kv().reset();
	}

	static std::shared_ptr<Mount> makeMount(uint8_t id, bool premium = false) {
		return std::make_shared<Mount>(id, static_cast<uint16_t>(id * 10), "TestMount", 0, premium, "ground");
	}

	std::shared_ptr<Player> player;
	InjectionFixture fixture;
};

// --- getCurrentMount / setCurrentMount ---

TEST_F(PlayerMountComponentTest, GetCurrentMount_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->mountComponent().getCurrentMount());
}

TEST_F(PlayerMountComponentTest, SetCurrentMount_RoundTrip) {
	player->mountComponent().setCurrentMount(5);
	EXPECT_EQ(5u, player->mountComponent().getCurrentMount());
}

TEST_F(PlayerMountComponentTest, SetCurrentMount_Overwrite) {
	player->mountComponent().setCurrentMount(3);
	player->mountComponent().setCurrentMount(7);
	EXPECT_EQ(7u, player->mountComponent().getCurrentMount());
}

// --- isRandomMounted / setRandomMount ---

TEST_F(PlayerMountComponentTest, IsRandomMounted_ReturnsZero_Initially) {
	EXPECT_EQ(0u, player->mountComponent().isRandomMounted());
}

TEST_F(PlayerMountComponentTest, SetRandomMount_RoundTrip) {
	player->mountComponent().setRandomMount(1);
	EXPECT_EQ(1u, player->mountComponent().isRandomMounted());
}

TEST_F(PlayerMountComponentTest, SetRandomMount_ClearValue) {
	player->mountComponent().setRandomMount(1);
	player->mountComponent().setRandomMount(0);
	EXPECT_EQ(0u, player->mountComponent().isRandomMounted());
}

// --- getLastMount ---

TEST_F(PlayerMountComponentTest, GetLastMount_ReturnsZero_WhenNoStorageOrKV) {
	EXPECT_EQ(0u, player->mountComponent().getLastMount());
}

TEST_F(PlayerMountComponentTest, GetLastMount_ReturnsStorage_WhenPositive) {
	player->addStorageValue(PSTRG_MOUNTS_CURRENTMOUNT, 7);
	EXPECT_EQ(7u, player->mountComponent().getLastMount());
}

TEST_F(PlayerMountComponentTest, GetLastMount_FallsBackToKV_WhenStorageIsDefault) {
	// PSTRG_MOUNTS_CURRENTMOUNT not set (default = -1 → ≤0), so falls back to kv
	player->kv()->set("last-mount", 4);
	EXPECT_EQ(4u, player->mountComponent().getLastMount());
}

// --- hasMount ---

TEST_F(PlayerMountComponentTest, HasMount_ReturnsFalse_WhenBitNotSet) {
	auto mount = makeMount(1, false);
	EXPECT_FALSE(player->mountComponent().hasMount(mount));
}

TEST_F(PlayerMountComponentTest, HasMount_ReturnsTrue_WhenBitSet) {
	// id=1, tmpMountId=0, key=PSTRG_MOUNTS_RANGE_START+0, bit=1<<0
	auto mount = makeMount(1, false);
	player->addStorageValue(PSTRG_MOUNTS_RANGE_START, 1);
	EXPECT_TRUE(player->mountComponent().hasMount(mount));
}

TEST_F(PlayerMountComponentTest, HasMount_ReturnsFalse_PremiumMount_NonPremiumPlayer) {
	// Premium mount, player not premium (no group set)
	auto mount = makeMount(2, true);
	player->addStorageValue(PSTRG_MOUNTS_RANGE_START, 1 << 1); // id=2 bit set
	EXPECT_FALSE(player->mountComponent().hasMount(mount));
}

TEST_F(PlayerMountComponentTest, HasMount_ReturnsTrue_ForDifferentMountIds) {
	// id=32, tmpMountId=31, key=PSTRG_MOUNTS_RANGE_START+1, bit=1<<0
	auto mount = makeMount(32, false);
	player->addStorageValue(PSTRG_MOUNTS_RANGE_START + 1, 1);
	EXPECT_TRUE(player->mountComponent().hasMount(mount));
}
