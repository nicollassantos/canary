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
	#include <optional>
	#include <string>
	#include <string_view>
#endif

namespace directory_creation {

	/// Creates the directory and every missing parent, without a shell (FS.mkdir/FS.mkdir_p, upstream
	/// PR #4102). Any name the filesystem accepts works, quotes and `;` included. Returns the error
	/// message on failure, nothing on success (an existing directory is a success). Never throws: a
	/// path that cannot be converted for the filesystem is also returned as an error.
	[[nodiscard]] std::optional<std::string> createDirectories(std::string_view path);

} // namespace directory_creation
