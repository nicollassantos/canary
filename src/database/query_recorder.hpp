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
	#include <string>
	#include <string_view>
	#include <vector>
#endif

/// Records the writes a piece of code would send to the database instead of running them.
///
/// While a Scope is alive on a thread, Database::executeQuery (and DBInsert, which uses it) on that
/// thread appends the statement to the scope's list and returns true; reads (storeQuery) still run.
/// Used to snapshot a player's save on the game thread, where nothing else changes the player, and
/// run the statements on another connection (SaveManager::saveLoggedOutPlayer).
namespace query_recorder {

	class Scope {
	public:
		explicit Scope(std::vector<std::string> &statements);
		~Scope();

		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		std::vector<std::string>* previous;
	};

	/// True when a Scope on this thread took the statement.
	bool record(std::string_view statement);

} // namespace query_recorder
