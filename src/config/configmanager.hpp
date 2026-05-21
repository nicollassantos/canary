/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "config/config_port.hpp"

using ConfigValue = std::variant<std::string, int32_t, bool, float>;

class ConfigManager final : public IConfigManager {
public:
	ConfigManager() = default;

	static ConfigManager &getInstance();

	bool load() override;
	bool reload() override;

	void missingConfigWarning(const char* identifier) override;

	const std::string &setConfigFileLua(const std::string &what) override {
		configFileLua = { what };
		return configFileLua;
	}
	[[nodiscard]] const std::string &getConfigFileLua() const override {
		return configFileLua;
	}

	[[nodiscard]] const std::string &getString(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const override;
	[[nodiscard]] int32_t getNumber(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const override;
	[[nodiscard]] bool getBoolean(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const override;
	[[nodiscard]] float getFloat(const ConfigKey_t &key, const std::source_location &location = std::source_location::current()) const override;
	[[nodiscard]] OTCFeatures getEnabledFeaturesOTC() const override;
	[[nodiscard]] OTCFeatures getDisabledFeaturesOTC() const override;

private:
	mutable std::unordered_map<ConfigKey_t, std::string> m_configString;
	mutable std::unordered_map<ConfigKey_t, bool> m_configBoolean;
	mutable std::unordered_map<ConfigKey_t, int32_t> m_configInteger;
	mutable std::unordered_map<ConfigKey_t, float> m_configFloat;

	std::unordered_map<ConfigKey_t, ConfigValue> configs;
	std::string loadStringConfig(lua_State* L, const ConfigKey_t &key, const char* identifier, const std::string &defaultValue);
	int32_t loadIntConfig(lua_State* L, const ConfigKey_t &key, const char* identifier, const int32_t &defaultValue);
	bool loadBoolConfig(lua_State* L, const ConfigKey_t &key, const char* identifier, const bool &defaultValue);
	float loadFloatConfig(lua_State* L, const ConfigKey_t &key, const char* identifier, const float &defaultValue);

	std::string configFileLua = { "config.lua" };
	bool loaded = false;
	OTCFeatures enabledFeaturesOTC = {};
	OTCFeatures disabledFeaturesOTC = {};
	void loadLuaOTCFeatures(lua_State* L);
};

constexpr auto g_configManager = ConfigManager::getInstance;
