/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

class DBResult;
using DBResult_ptr = std::shared_ptr<DBResult>;

class IDatabase {
public:
	IDatabase() = default;
	virtual ~IDatabase() = default;

	IDatabase(const IDatabase &) = delete;
	void operator=(const IDatabase &) = delete;

	static IDatabase &getInstance();

	[[nodiscard]] virtual bool executeQuery(std::string_view query) = 0;
	[[nodiscard]] virtual DBResult_ptr storeQuery(std::string_view query) = 0;
	[[nodiscard]] virtual std::string escapeString(const std::string &s) const = 0;
	[[nodiscard]] virtual std::string escapeBlob(const char* s, uint32_t length) const = 0;
	[[nodiscard]] virtual uint64_t getLastInsertId() const = 0;
};

// g_database is defined in database.hpp pointing to Database::getInstance.
// New code should prefer injecting IDatabase& directly via constructor.
