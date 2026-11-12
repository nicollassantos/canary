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
#include "config/configmanager.hpp"
#include "lib/di/container.hpp"

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

// The production container must expose the same ConfigManager that g_configManager() loads;
// a second, never-loaded instance would silently return defaults through the port.
TEST(ConfigManagerContainerTest, PortResolvesToLoadedSingleton) {
	auto* previous = DI::getTestContainer();
	DI::setTestContainer(nullptr);

	auto &viaPort = IConfigManager::getInstance();
	auto &viaContainer = DI::get<IConfigManager>();

	DI::setTestContainer(previous);

	EXPECT_EQ(static_cast<IConfigManager*>(&g_configManager()), &viaPort);
	EXPECT_EQ(&viaPort, &viaContainer);
}

// Test containers that do not bind the port keep the pre-port behaviour:
// fall back to the global ConfigManager instead of asserting on an unbound abstract type.
TEST(ConfigManagerContainerTest, UnboundTestContainerFallsBackToGlobalConfig) {
	auto* previous = DI::getTestContainer();
	di::extension::injector<> injector {};
	DI::setTestContainer(&injector);

	auto &viaPort = IConfigManager::getInstance();

	DI::setTestContainer(previous);
	EXPECT_EQ(static_cast<IConfigManager*>(&g_configManager()), &viaPort);
}

TEST(ConfigManagerContainerTest, BoundTestContainerUsesInMemoryConfig) {
	auto* previous = DI::getTestContainer();
	di::extension::injector<> injector {};
	InMemoryConfigManager::install(injector);
	DI::setTestContainer(&injector);

	auto* viaPort = dynamic_cast<InMemoryConfigManager*>(&IConfigManager::getInstance());

	DI::setTestContainer(previous);
	EXPECT_NE(nullptr, viaPort);
}
