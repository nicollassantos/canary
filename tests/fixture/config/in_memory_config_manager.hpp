/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */
#pragma once

#include <string>
#include <unordered_map>

#include "config/config_port.hpp"
#include "test_injection.hpp"
#include "lib/di/container.hpp"

namespace di = boost::di;

class InMemoryConfigManager final : public IConfigManager {
public:
	InMemoryConfigManager() = default;
	InMemoryConfigManager(const InMemoryConfigManager &) { }
	InMemoryConfigManager(InMemoryConfigManager &&) { }

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IConfigManager>.to<InMemoryConfigManager>().in(di::singleton));
		return injector;
	}

	InMemoryConfigManager &reset() {
		strings.clear();
		numbers.clear();
		booleans.clear();
		floats.clear();
		return *this;
	}

	void setString(ConfigKey_t key, const std::string &value) { strings[key] = value; }
	void setNumber(ConfigKey_t key, int32_t value) { numbers[key] = value; }
	void setBoolean(ConfigKey_t key, bool value) { booleans[key] = value; }
	void setFloat(ConfigKey_t key, float value) { floats[key] = value; }

	bool load() override { return true; }
	bool reload() override { return true; }
	void missingConfigWarning(const char*) override { }

	const std::string &setConfigFileLua(const std::string &what) override {
		configFileLua = what;
		return configFileLua;
	}
	[[nodiscard]] const std::string &getConfigFileLua() const override { return configFileLua; }

	[[nodiscard]] const std::string &getString(const ConfigKey_t &key, const std::source_location & = std::source_location::current()) const override {
		auto it = strings.find(key);
		if (it != strings.end()) {
			return it->second;
		}
		static const std::string empty;
		return empty;
	}

	[[nodiscard]] int32_t getNumber(const ConfigKey_t &key, const std::source_location & = std::source_location::current()) const override {
		auto it = numbers.find(key);
		return it != numbers.end() ? it->second : 0;
	}

	[[nodiscard]] bool getBoolean(const ConfigKey_t &key, const std::source_location & = std::source_location::current()) const override {
		auto it = booleans.find(key);
		return it != booleans.end() ? it->second : false;
	}

	[[nodiscard]] float getFloat(const ConfigKey_t &key, const std::source_location & = std::source_location::current()) const override {
		auto it = floats.find(key);
		return it != floats.end() ? it->second : 0.0f;
	}

	[[nodiscard]] OTCFeatures getEnabledFeaturesOTC() const override { return {}; }
	[[nodiscard]] OTCFeatures getDisabledFeaturesOTC() const override { return {}; }

private:
	std::unordered_map<ConfigKey_t, std::string> strings;
	std::unordered_map<ConfigKey_t, int32_t> numbers;
	std::unordered_map<ConfigKey_t, bool> booleans;
	std::unordered_map<ConfigKey_t, float> floats;
	std::string configFileLua;
};

template <>
struct TestInjection<IConfigManager> {
	using type = InMemoryConfigManager;
};
