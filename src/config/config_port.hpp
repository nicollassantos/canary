/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "config_enums.hpp"

#include <source_location>
#include <string>
#include <vector>

using OTCFeatures = std::vector<uint8_t>;

class IConfigManager {
public:
	IConfigManager() = default;
	virtual ~IConfigManager() = default;

	IConfigManager(const IConfigManager &) = delete;
	void operator=(const IConfigManager &) = delete;

	static IConfigManager &getInstance();

	virtual bool load() = 0;
	virtual bool reload() = 0;
	virtual void missingConfigWarning(const char* identifier) = 0;

	virtual const std::string &setConfigFileLua(const std::string &what) = 0;
	[[nodiscard]] virtual const std::string &getConfigFileLua() const = 0;

	[[nodiscard]] virtual const std::string &getString(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const = 0;
	[[nodiscard]] virtual int32_t getNumber(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const = 0;
	[[nodiscard]] virtual bool getBoolean(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const = 0;
	[[nodiscard]] virtual float getFloat(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const = 0;
	[[nodiscard]] virtual OTCFeatures getEnabledFeaturesOTC() const = 0;
	[[nodiscard]] virtual OTCFeatures getDisabledFeaturesOTC() const = 0;
};

// g_configManager is defined in configmanager.hpp pointing to ConfigManager::getInstance.
// New code should prefer injecting IConfigManager& directly via constructor.
