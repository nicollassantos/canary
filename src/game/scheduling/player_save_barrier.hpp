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
	#include <array>
	#include <chrono>
	#include <condition_variable>
	#include <cstdint>
	#include <map>
	#include <mutex>
	#include <string>
	#include <string_view>
#endif

/// Characters whose logout save is still being written to the database.
///
/// The logout save runs on another connection (SaveManager::saveLoggedOutPlayer). Until it commits,
/// the database holds the character as it was before; loading it then (an immediate relog, a letter,
/// the market, Game.getOfflinePlayer) and saving it again would lose or duplicate items. Player loads
/// (PlayerRepositoryDB) wait here first; the wait only happens in that window, a few milliseconds.
/// Writes that change a character's row without loading it wait too: the save would overwrite them
/// (PlayerRepositoryDB::increaseBankBalance, and db.query/db.asyncQuery on player tables).
///
/// The waits run on the game thread and are bounded per save: callers wait for a pending save only
/// until it is MaxWait old. Past that it counts as stuck and every caller is answered at once, so one
/// save stuck behind an unreachable database holds the game thread for at most MaxWait in total, no
/// matter how often its character tries to log in. A false answer refuses loads (the login asks to
/// try again) and Lua writes; increaseBankBalance goes ahead with an error log. Each stuck save is
/// logged once and counted in `player_save_barrier_timeouts`.
class PlayerSaveBarrier {
public:
	static constexpr std::chrono::milliseconds MaxWait { 1000 };

	void begin(uint32_t guid, const std::string &name);
	void end(uint32_t guid);

	/// Waits while a save of `guid` (or of the character named `name`, any case) is pending.
	/// True once none is pending; false once the pending save is `limit` old (at once if it already is).
	[[nodiscard]] bool waitForGuid(uint32_t guid, std::chrono::milliseconds limit = MaxWait);
	[[nodiscard]] bool waitForName(const std::string &name, std::chrono::milliseconds limit = MaxWait);
	/// Waits while any logout save is pending (scripts writing player rows without naming one).
	[[nodiscard]] bool waitForAll(std::chrono::milliseconds limit = MaxWait);

	[[nodiscard]] size_t pending() const;

	/// True when `query` writes (is not a SELECT) one of the tables the logout save rewrites
	/// (IOLoginData::savePlayerGuard and the components it saves): such a write could be undone by a
	/// pending save. Keep SavedTables in sync with the save.
	static bool writesSavedTables(std::string_view query);
	static constexpr std::array<std::string_view, 15> SavedTables {
		"players", "player_items", "player_depotitems", "player_inboxitems", "player_rewards",
		"player_stash", "player_spells", "player_kills", "player_charms", "player_prey",
		"player_taskhunt", "player_bosstiary", "player_storage", "player_wheeldata", "forge_history",
	};

	static PlayerSaveBarrier &global();

private:
	struct Pending {
		std::string lowerName;
		std::chrono::steady_clock::time_point started;
		bool reportedStuck = false;
	};

	template <typename Blocks>
	bool waitWhile(const Blocks &blocks, std::chrono::milliseconds limit);

	mutable std::mutex mutex;
	std::condition_variable saved;
	std::map<uint32_t, Pending> saving;
};
