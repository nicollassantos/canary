/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "utils/directory_creation.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <exception>
	#include <filesystem>
	#include <system_error>
#endif

namespace directory_creation {

	std::optional<std::string> createDirectories(std::string_view path) {
		if (path.empty()) {
			return "empty path";
		}

		try {
			// Building the path can throw before create_directories sees the error_code: on Windows
			// with a UTF-8 code page, a byte with no Unicode mapping throws std::system_error.
			const std::filesystem::path target { std::string(path) };
			std::error_code error;
			std::filesystem::create_directories(target, error);
			if (error) {
				return error.message();
			}
			if (!std::filesystem::is_directory(target, error)) {
				return error ? error.message() : "not a directory";
			}
			return std::nullopt;
		} catch (const std::exception &exception) {
			return exception.what();
		}
	}

} // namespace directory_creation
