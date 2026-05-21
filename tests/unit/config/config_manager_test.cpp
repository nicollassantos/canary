/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/in_memory_config_manager.hpp"
#include "config/config_enums.hpp"

TEST(InMemoryConfigManagerTest, GetBooleanReturnsSetValue) {
	InMemoryConfigManager cfg;
	cfg.setBoolean(ALLOW_CHANGEOUTFIT, true);
	EXPECT_TRUE(cfg.getBoolean(ALLOW_CHANGEOUTFIT));
}

TEST(InMemoryConfigManagerTest, GetBooleanReturnsFalseWhenUnset) {
	InMemoryConfigManager cfg;
	EXPECT_FALSE(cfg.getBoolean(ALLOW_CHANGEOUTFIT));
}

TEST(InMemoryConfigManagerTest, GetStringReturnsSetValue) {
	InMemoryConfigManager cfg;
	cfg.setString(SERVER_MOTD, "hello world");
	EXPECT_EQ("hello world", cfg.getString(SERVER_MOTD));
}

TEST(InMemoryConfigManagerTest, GetStringReturnsEmptyWhenUnset) {
	InMemoryConfigManager cfg;
	EXPECT_EQ("", cfg.getString(SERVER_MOTD));
}

TEST(InMemoryConfigManagerTest, GetNumberReturnsSetValue) {
	InMemoryConfigManager cfg;
	cfg.setNumber(MAX_PLAYERS, 42);
	EXPECT_EQ(42, cfg.getNumber(MAX_PLAYERS));
}

TEST(InMemoryConfigManagerTest, GetFloatReturnsSetValue) {
	InMemoryConfigManager cfg;
	cfg.setFloat(RATE_EXPERIENCE, 2.5f);
	EXPECT_FLOAT_EQ(2.5f, cfg.getFloat(RATE_EXPERIENCE));
}

TEST(InMemoryConfigManagerTest, ResetClearsAllValues) {
	InMemoryConfigManager cfg;
	cfg.setBoolean(ALLOW_CHANGEOUTFIT, true);
	cfg.setNumber(MAX_PLAYERS, 99);
	cfg.reset();
	EXPECT_FALSE(cfg.getBoolean(ALLOW_CHANGEOUTFIT));
	EXPECT_EQ(0, cfg.getNumber(MAX_PLAYERS));
}
