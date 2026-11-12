/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "server/server_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <functional>
	#include <optional>
	#include <string>
	#include <vector>
#endif

/// One highscore page as read from the database: the page the rows belong to, the number of
/// ranked players (for the page count) and the rows themselves.
struct HighscorePage {
	uint16_t page = 0;
	uint32_t total = 0;
	std::vector<HighscoreCharacter> characters;
};

/// Highscore queries (driven port). The load functions build the query and run it off the game
/// thread. They return false when no query can be built; onLoaded is then never called.
/// Otherwise onLoaded runs later on the dispatcher, with std::nullopt when nothing was found.
/// onLoaded must not capture state that can die before it runs.
class IHighscoreRepository {
public:
	using OnLoaded = std::function<void(std::optional<HighscorePage>)>;

	virtual ~IHighscoreRepository() = default;

	static IHighscoreRepository &getInstance();

	/// `vocation` is the server vocation id, or 0xFFFFFFFF for all vocations.
	[[nodiscard]] virtual bool loadEntries(const std::string &categoryName, uint32_t page, uint8_t entriesPerPage, uint32_t vocation, OnLoaded onLoaded) = 0;
	/// Loads the page that holds `playerGUID`.
	[[nodiscard]] virtual bool loadOurRank(const std::string &categoryName, uint8_t entriesPerPage, uint32_t playerGUID, uint32_t vocation, OnLoaded onLoaded) = 0;
};

constexpr auto g_highscoreRepository = IHighscoreRepository::getInstance;
