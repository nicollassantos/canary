/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/pvp/expert_pvp.hpp"
#include "game/world_type_config.hpp"

TEST(WorldTypeConfigTest, OpenPvpIsPvpWorld) {
	EXPECT_EQ(WORLD_TYPE_PVP, world_type_config::parse("open-pvp"));
}

TEST(WorldTypeConfigTest, OpenPvpIsNeitherRetroNorExpert) {
	EXPECT_FALSE(ExpertPvp::isRetroPvpWorldTypeName("open-pvp"));
	EXPECT_FALSE(ExpertPvp::isExpertPvpWorldTypeName("open-pvp"));
}

TEST(WorldTypeConfigTest, UpstreamPvpVariantsArePvpWorlds) {
	EXPECT_EQ(WORLD_TYPE_PVP, world_type_config::parse("pvp"));
	EXPECT_EQ(WORLD_TYPE_PVP, world_type_config::parse("retro-pvp"));
	EXPECT_EQ(WORLD_TYPE_PVP, world_type_config::parse("expert-pvp"));
}

TEST(WorldTypeConfigTest, NonPvpWorlds) {
	EXPECT_EQ(WORLD_TYPE_NO_PVP, world_type_config::parse("no-pvp"));
	EXPECT_EQ(WORLD_TYPE_PVP_ENFORCED, world_type_config::parse("pvp-enforced"));
}

TEST(WorldTypeConfigTest, ParsingIsCaseInsensitive) {
	EXPECT_EQ(WORLD_TYPE_PVP, world_type_config::parse("Open-PvP"));
	EXPECT_EQ(WORLD_TYPE_NO_PVP, world_type_config::parse("NO-PVP"));
}

TEST(WorldTypeConfigTest, UnknownWorldTypeIsRejected) {
	EXPECT_FALSE(world_type_config::parse("").has_value());
	EXPECT_FALSE(world_type_config::parse("hardcore").has_value());
}

TEST(WorldTypeConfigTest, DisplayNameResolvesLegacyAlias) {
	EXPECT_EQ("RETRO-PVP", world_type_config::displayName("pvp"));
	EXPECT_EQ("OPEN-PVP", world_type_config::displayName("open-pvp"));
	EXPECT_EQ("EXPERT-PVP", world_type_config::displayName("Expert-PvP"));
}
