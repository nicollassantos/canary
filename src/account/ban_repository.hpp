/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <ctime>
	#include <optional>
	#include <string>
#endif

struct BanInfo {
	std::string bannedBy {};
	std::string reason {};
	/// 0 for a permanent ban.
	time_t expiresAt {};
};

/// Account bans, address bans and namelocks (driven port). The login checks them before a character
/// enters the world; tests use an in-memory fake (tests/fixture/account/in_memory_ban_repository.hpp).
class IBanRepository {
public:
	IBanRepository() = default;
	virtual ~IBanRepository() = default;
	IBanRepository(const IBanRepository &) = delete;
	void operator=(const IBanRepository &) = delete;

	static IBanRepository &getInstance();

	/// The account's active ban. An expired one is moved to the ban history and not returned.
	[[nodiscard]] virtual std::optional<BanInfo> findAccountBan(uint32_t accountId) = 0;
	/// The address's active ban (0 is never banned). An expired one is removed and not returned.
	[[nodiscard]] virtual std::optional<BanInfo> findAddressBan(uint32_t ip) = 0;
	[[nodiscard]] virtual bool isNamelocked(uint32_t playerGuid) = 0;
};

constexpr auto g_banRepository = IBanRepository::getInstance;
