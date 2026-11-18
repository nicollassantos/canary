/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2023 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "config/configmanager.hpp"
#include "database/database.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

int main(int argc, char** argv) {
	::testing::InitGoogleTest(&argc, argv);

	// Never destroyed: it owns the singletons the tests create (Game among them), and their destructors
	// resolve other singletons through this same container. Destroyed at exit, ~Game -> ~Raids ->
	// LuaScriptInterface::closeState looked LuaEnvironment up in the container being torn down, and
	// suites that touch g_game() crashed when run alone. Still reachable, so not reported as a leak.
	static auto* const injector = new di::extension::injector<> {};
	InMemoryLogger::install(*injector);
	DI::setTestContainer(injector);

	(void)g_logger();
	(void)g_configManager();
	(void)g_database();

	// Creating a Player builds its inbox from the item types. Seed them once here so every suite runs
	// on its own (--gtest_filter), not only after another suite happened to seed them.
	test_items::ensureTestItemTypes();

	// The global ConfigManager is empty until something loads it. Suites that read it (a house bid's
	// end date, container capacity) passed only after the imbuements suite had loaded one: load the
	// defaults once, so each suite runs on its own.
	(void)g_configManager().setConfigFileLua(std::string(TESTS_SOURCE_DIR) + "/tests/fixture/config/unit_defaults.lua");
	if (!g_configManager().load()) {
		return 1;
	}

	return RUN_ALL_TESTS();
}
