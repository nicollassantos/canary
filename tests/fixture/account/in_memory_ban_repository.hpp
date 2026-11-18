/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "account/ban_repository.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <map>
	#include <set>
#endif

namespace tests {
	/// IBanRepository in memory, for login rule tests.
	class InMemoryBanRepository final : public IBanRepository {
	public:
		std::optional<BanInfo> findAccountBan(uint32_t accountId) override {
			const auto it = accountBans.find(accountId);
			return it == accountBans.end() ? std::nullopt : std::optional<BanInfo>(it->second);
		}

		std::optional<BanInfo> findAddressBan(uint32_t ip) override {
			const auto it = addressBans.find(ip);
			return it == addressBans.end() ? std::nullopt : std::optional<BanInfo>(it->second);
		}

		bool isNamelocked(uint32_t playerGuid) override {
			return namelocked.contains(playerGuid);
		}

		std::map<uint32_t, BanInfo> accountBans;
		std::map<uint32_t, BanInfo> addressBans;
		std::set<uint32_t> namelocked;
	};
} // namespace tests
