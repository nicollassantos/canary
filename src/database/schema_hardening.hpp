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
	#include <cstddef>
	#include <string>
	#include <string_view>
	#include <vector>
#endif

class IDatabase;

/// Indexes this fork adds on top of upstream's schema.
///
/// Upstream numbers its migrations (data-*/migrations/N.lua, `db_version`), so a numbered file
/// here would collide with the next upstream one and make servers skip it. Instead, this step runs
/// on every boot after upstream's migrations and only creates what is missing: no counter, no
/// schema.sql edit, nothing to merge. It never drops indexes.
namespace schema_hardening {

	struct WantedIndex {
		std::string_view table;
		/// "solid_" prefix, so it never clashes with an upstream name.
		std::string_view name;
		std::vector<std::string_view> columns;
		std::string_view reason;
	};

	[[nodiscard]] const std::vector<WantedIndex> &wantedIndexes();

	/// Creates every wanted index unless the table already has an index that starts with the same
	/// columns (any name). Failures are logged and the boot continues. Returns how many it created.
	size_t ensureIndexes(IDatabase &db, const std::string &schemaName);

	/// True when some index in `existing` (each one its ordered columns) starts with `wanted`.
	[[nodiscard]] bool hasLeadingIndex(const std::vector<std::vector<std::string>> &existing, const std::vector<std::string_view> &wanted);

} // namespace schema_hardening
