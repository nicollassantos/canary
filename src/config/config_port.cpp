/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "config/config_port.hpp"
#include "config/configmanager.hpp"
#include "lib/di/container.hpp"

IConfigManager &IConfigManager::getInstance() {
	// A test container that does not bind the port would assert on the abstract type;
	// keep the global ConfigManager there, exactly what g_configManager() returns.
	if (DI::getTestContainer() && !DI::isBoundInTestContainer<IConfigManager>()) {
		return ConfigManager::getInstance();
	}
	return inject<IConfigManager>();
}
