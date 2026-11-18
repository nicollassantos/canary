/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/player_progress_repository.hpp"
#include "lib/di/container.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <map>
#endif

namespace tests {
	/// IPlayerProgressRepository in memory, for component tests.
	class InMemoryPlayerProgressRepository final : public IPlayerProgressRepository {
	public:
		InMemoryPlayerProgressRepository() = default;
		InMemoryPlayerProgressRepository(const InMemoryPlayerProgressRepository &) { }
		InMemoryPlayerProgressRepository(InMemoryPlayerProgressRepository &&) noexcept { }

		static di::extension::injector<> &install(di::extension::injector<> &injector) {
			injector.install(di::bind<IPlayerProgressRepository>.to<InMemoryPlayerProgressRepository>().in(di::singleton));
			return injector;
		}

		static InMemoryPlayerProgressRepository &installed() {
			return dynamic_cast<InMemoryPlayerProgressRepository &>(DI::port<IPlayerProgressRepository>());
		}

		std::vector<ForgeHistory> loadForgeHistory(uint32_t playerGuid) override {
			return forgeHistory[playerGuid];
		}

		bool saveForgeHistory(uint32_t playerGuid, const std::vector<uint32_t> &removedIds, const std::vector<ForgeHistory> &added) override {
			++forgeSaves;
			if (failSaves) {
				return false;
			}
			auto &history = forgeHistory[playerGuid];
			std::erase_if(history, [&removedIds](const ForgeHistory &entry) { return std::ranges::find(removedIds, entry.id) != removedIds.end(); });
			history.insert(history.end(), added.begin(), added.end());
			return true;
		}

		std::optional<std::string> loadWheelSlots(uint32_t playerGuid) override {
			const auto it = wheelSlots.find(playerGuid);
			return it == wheelSlots.end() ? std::nullopt : std::optional<std::string>(it->second);
		}

		bool saveWheelSlots(uint32_t playerGuid, const std::string &slots) override {
			wheelSlots[playerGuid] = slots;
			return true;
		}

		std::optional<uint32_t> findCharmPointsLeader() override {
			return charmLeader;
		}

		std::optional<uint32_t> findSkillLeader(std::string_view skillColumn) override {
			const auto it = skillLeaders.find(std::string(skillColumn));
			return it == skillLeaders.end() ? std::nullopt : std::optional<uint32_t>(it->second);
		}

		std::optional<uint32_t> countHirelings(uint32_t playerGuid) override {
			return hirelings[playerGuid];
		}

		void loadRecentDeaths(uint32_t, uint32_t, uint16_t, std::function<void(std::optional<RecentDeathsPage>)> done) override {
			done(recentDeaths);
		}

		void loadRecentPvpKills(const std::string &, uint32_t, uint16_t, std::function<void(std::optional<PvpKillsPage>)> done) override {
			done(recentKills);
		}

		std::vector<CharacterSummary> loadCharacterSummaries(const std::vector<std::string> &) override {
			return characters;
		}

		std::map<uint32_t, std::vector<ForgeHistory>> forgeHistory;
		int forgeSaves = 0;
		bool failSaves = false;
		std::map<uint32_t, std::string> wheelSlots;
		std::optional<uint32_t> charmLeader;
		std::map<std::string, uint32_t> skillLeaders;
		std::map<uint32_t, uint32_t> hirelings;
		std::optional<RecentDeathsPage> recentDeaths;
		std::optional<PvpKillsPage> recentKills;
		std::vector<CharacterSummary> characters;
	};
} // namespace tests
