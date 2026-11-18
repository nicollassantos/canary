/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "game/server_config_repository.hpp"
#include "lib/di/container.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <map>
#endif

namespace tests {
	/// IServerConfigRepository in memory.
	class InMemoryServerConfigRepository final : public IServerConfigRepository {
	public:
		InMemoryServerConfigRepository() = default;
		InMemoryServerConfigRepository(const InMemoryServerConfigRepository &) { }
		InMemoryServerConfigRepository(InMemoryServerConfigRepository &&) noexcept { }

		static di::extension::injector<> &install(di::extension::injector<> &injector) {
			injector.install(di::bind<IServerConfigRepository>.to<InMemoryServerConfigRepository>().in(di::singleton));
			return injector;
		}

		static InMemoryServerConfigRepository &installed() {
			return dynamic_cast<InMemoryServerConfigRepository &>(DI::port<IServerConfigRepository>());
		}

		std::optional<std::string> find(std::string_view key) override {
			const auto it = values.find(std::string(key));
			return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
		}

		void insert(std::string_view key, std::string_view value) override {
			values.emplace(std::string(key), std::string(value));
		}

		void update(std::string_view key, std::string_view value) override {
			if (const auto it = values.find(std::string(key)); it != values.end()) {
				it->second = std::string(value);
			}
		}

		std::optional<BoostedCreatureRecord> loadBoostedCreature() override {
			return boosted;
		}

		bool storeBoostedCreature(const BoostedCreatureRecord &creature, const BoostedCreatureLook &look) override {
			boosted = creature;
			boostedLook = look;
			return true;
		}

		std::map<std::string, std::string> values;
		std::optional<BoostedCreatureRecord> boosted;
		BoostedCreatureLook boostedLook;
	};
} // namespace tests
