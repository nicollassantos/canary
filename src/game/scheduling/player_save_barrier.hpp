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
	#include <vector>
#endif

/// Characters whose logout save is still being written to the database.
///
/// The logout save runs on another connection (SaveManager::saveLoggedOutPlayer). Until it commits,
/// the database holds the character as it was before; loading it then (an immediate relog, a letter,
/// the market, Game.getOfflinePlayer) and saving it again would lose or duplicate items. Player loads
/// (PlayerRepositoryDB) wait here first; the wait only happens in that window, a few milliseconds.
/// Writes that change a character's row without loading it must not land before the save either,
/// which would overwrite them. A bank credit (PlayerRepositoryDB::increaseBankBalance) is handed to
/// the pending save instead (deferUntilSaved) and committed right after it, before the character is
/// released; db.query/db.asyncQuery on player tables wait like loads.
///
/// The waits run on the game thread and are bounded: callers wait for a pending save only until it
/// is MaxWait old. Past that it counts as stuck, the database path as unhealthy, and every caller
/// that meets any pending save is answered at once until the stuck one commits. So an outage holds
/// the game thread for at most MaxWait in total, however many characters log out and try to come
/// back meanwhile. A false answer refuses loads (the login asks to
/// try again) and Lua writes. Each stuck save is logged once and counted in
/// `player_save_barrier_timeouts`.
class PlayerSaveBarrier {
public:
	static constexpr std::chrono::milliseconds MaxWait { 1000 };

	void begin(uint32_t guid, const std::string &name);
	/// Releases `guid`; returns the deferred statements still queued (only left on a failure path,
	/// for the caller to log).
	std::vector<std::string> end(uint32_t guid);

	/// While a save of `guid` is pending, queues `statement` to be committed right after it and
	/// returns true; false (run it yourself) when none is pending.
	bool deferUntilSaved(uint32_t guid, std::string statement);
	/// For the saver, once a batch finished: the statements deferred meanwhile (the character stays
	/// pending until they commit too), or nothing after releasing `guid`.
	std::vector<std::string> takeDeferredOrRelease(uint32_t guid);

	/// Waits while a save of `guid` (or of the character named `name`, any case) is pending.
	/// True once none is pending; false once any pending save is `limit` old (at once if one already is).
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
		std::vector<std::string> deferred;
	};

	template <typename Blocks>
	bool waitWhile(const Blocks &blocks, std::chrono::milliseconds limit);

	mutable std::mutex mutex;
	std::condition_variable saved;
	std::map<uint32_t, Pending> saving;
};
