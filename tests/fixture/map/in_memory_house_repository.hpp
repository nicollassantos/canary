/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "lib/di/container.hpp"
#include "map/house/house_repository.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <map>
	#include <utility>
	#include <vector>
#endif

namespace tests {
	/// IHouseRepository in memory: what House and the startup load store, and the owners they find.
	class InMemoryHouseRepository final : public IHouseRepository {
	public:
		InMemoryHouseRepository() = default;
		InMemoryHouseRepository(const InMemoryHouseRepository &) { }
		InMemoryHouseRepository(InMemoryHouseRepository &&) noexcept { }

		static di::extension::injector<> &install(di::extension::injector<> &injector) {
			injector.install(di::bind<IHouseRepository>.to<InMemoryHouseRepository>().in(di::singleton));
			return injector;
		}

		static InMemoryHouseRepository &installed() {
			return dynamic_cast<InMemoryHouseRepository &>(DI::port<IHouseRepository>());
		}

		std::vector<StoredHouse> loadHouses() override {
			return stored;
		}

		std::vector<HouseAccessListRow> loadAccessLists() override {
			return storedLists;
		}

		bool saveHouses(const std::vector<HouseRow> &houses, const std::vector<HouseAccessListRow> &lists) override {
			savedHouses = houses;
			savedLists = lists;
			return true;
		}

		void storeOwner(uint32_t houseId, uint32_t ownerGuid) override {
			storedOwners.emplace_back(houseId, ownerGuid);
		}

		void storeNewOwner(uint32_t houseId, int32_t newOwnerGuid) override {
			storedNewOwners.emplace_back(houseId, newOwnerGuid);
		}

		std::optional<HouseOwnerIdentity> findOwnerIdentity(uint32_t playerGuid) override {
			const auto it = identities.find(playerGuid);
			return it == identities.end() ? std::nullopt : std::optional<HouseOwnerIdentity>(it->second);
		}

		std::vector<StoredHouse> stored;
		std::vector<HouseAccessListRow> storedLists;
		std::map<uint32_t, HouseOwnerIdentity> identities;
		std::vector<std::pair<uint32_t, uint32_t>> storedOwners;
		std::vector<std::pair<uint32_t, int32_t>> storedNewOwners;
		std::vector<HouseRow> savedHouses;
		std::vector<HouseAccessListRow> savedLists;
	};
} // namespace tests
