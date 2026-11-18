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
	#include <chrono>
	#include <condition_variable>
	#include <cstdint>
	#include <map>
	#include <mutex>
	#include <string>
#endif

/// Characters whose logout save is still being written to the database.
///
/// The logout save runs on another connection (SaveManager::saveLoggedOutPlayer). Until it commits,
/// the database holds the character as it was before; loading it then (an immediate relog, a letter,
/// the market, Game.getOfflinePlayer) and saving it again would lose or duplicate items. Player loads
/// (PlayerRepositoryDB) wait here first; the wait only happens in that window, a few milliseconds.
/// Writes that change a character's row without loading it wait too: the save would overwrite them
/// (PlayerRepositoryDB::increaseBankBalance, and db.query/db.asyncQuery on player tables).
class PlayerSaveBarrier {
public:
	/// Logged after each WarningInterval spent waiting (the database may be down).
	static constexpr std::chrono::seconds WarningInterval { 5 };

	void begin(uint32_t guid, const std::string &name);
	void end(uint32_t guid);

	/// Blocks while a save of `guid` (or of the character named `name`, any case) is pending;
	/// returns how long it waited.
	std::chrono::milliseconds waitForGuid(uint32_t guid);
	std::chrono::milliseconds waitForName(const std::string &name);
	/// Blocks while any logout save is pending (scripts writing player rows without naming one).
	std::chrono::milliseconds waitForAll();

	[[nodiscard]] size_t pending() const;

	static PlayerSaveBarrier &global();

private:
	template <typename Pending>
	std::chrono::milliseconds waitWhile(const Pending &isPending, const std::string &who);

	mutable std::mutex mutex;
	std::condition_variable saved;
	std::map<uint32_t, std::string> saving; // guid -> lower-case name
};
