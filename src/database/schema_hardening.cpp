/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "database/schema_hardening.hpp"

#include "database/database.hpp"
#include "database/database_port.hpp"
#include "lib/logging/logger.hpp"
#include "utils/benchmark.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <map>
#endif

namespace schema_hardening {

	namespace {

		std::vector<std::vector<std::string>> existingIndexes(IDatabase &db, const std::string &schemaName, std::string_view table) {
			const auto result = db.storeQuery(fmt::format(
				"SELECT `INDEX_NAME` AS `index_name`, `COLUMN_NAME` AS `column_name` FROM `information_schema`.`STATISTICS` "
				"WHERE `TABLE_SCHEMA` = {} AND `TABLE_NAME` = {} ORDER BY `INDEX_NAME`, `SEQ_IN_INDEX`",
				db.escapeString(schemaName), db.escapeString(std::string(table))
			));

			std::map<std::string, std::vector<std::string>> byName;
			if (result) {
				do {
					byName[result->getString("index_name")].push_back(result->getString("column_name"));
				} while (result->next());
			}

			std::vector<std::vector<std::string>> indexes;
			for (auto &[_, columns] : byName) {
				indexes.push_back(std::move(columns));
			}
			return indexes;
		}

	} // namespace

	const std::vector<WantedIndex> &wantedIndexes() {
		static const std::vector<WantedIndex> indexes {
			{
				"market_history",
				"solid_market_history_stats",
				{ "state", "itemtype", "sale", "tier", "price" },
				"market statistics (Game::loadItemsPrice) scanned the whole table; covering index halves it (300k rows: 207 ms -> 96 ms)",
			},
		};
		return indexes;
	}

	bool hasLeadingIndex(const std::vector<std::vector<std::string>> &existing, const std::vector<std::string_view> &wanted) {
		for (const auto &columns : existing) {
			if (columns.size() < wanted.size()) {
				continue;
			}
			bool leads = true;
			for (size_t i = 0; i < wanted.size(); ++i) {
				if (columns[i] != wanted[i]) {
					leads = false;
					break;
				}
			}
			if (leads) {
				return true;
			}
		}
		return false;
	}

	size_t ensureIndexes(IDatabase &db, const std::string &schemaName) {
		size_t created = 0;
		for (const auto &wanted : wantedIndexes()) {
			const auto existing = existingIndexes(db, schemaName, wanted.table);
			if (existing.empty()) {
				g_logger().warn("[schema_hardening] table {} not found or has no index; skipping {}", wanted.table, wanted.name);
				continue;
			}
			if (hasLeadingIndex(existing, wanted.columns)) {
				continue;
			}

			std::string columns;
			for (const auto &column : wanted.columns) {
				columns += fmt::format("{}`{}`", columns.empty() ? "" : ", ", column);
			}

			g_logger().info("[schema_hardening] creating index {} on {} ({}); a large table may take a while...", wanted.name, wanted.table, wanted.reason);
			Benchmark bm;
			if (!db.executeQuery(fmt::format("ALTER TABLE `{}` ADD INDEX `{}` ({})", wanted.table, wanted.name, columns))) {
				g_logger().error("[schema_hardening] failed to create index {} on {}; the server keeps running without it", wanted.name, wanted.table);
				continue;
			}
			g_logger().info("[schema_hardening] index {} on {} created in {:.0f} ms", wanted.name, wanted.table, bm.duration());
			++created;
		}
		return created;
	}

	const std::vector<WantedColumnWidening> &wantedColumnWidenings() {
		static const std::vector<WantedColumnWidening> columns {
			{
				"ip_bans",
				"ip",
				"DECIMAL(39,0) UNSIGNED NOT NULL",
				"signed int(11) holding the network-order IPv4: every address whose last octet is 128 or more overflowed it and could not be banned; also holds an IPv6 /64 prefix",
			},
			{
				"players",
				"lastip",
				"DECIMAL(39,0) UNSIGNED NOT NULL DEFAULT '0'",
				"holds an IPv6 address next to the IPv4 values it already has",
			},
		};
		return columns;
	}

	WidenAction widenActionFor(std::string_view dataType) {
		if (dataType.empty()) {
			return WidenAction::Missing;
		}
		const auto type = asLowerCaseString(std::string(dataType));
		if (type == "decimal") {
			return WidenAction::AlreadyWide;
		}
		if (type == "tinyint" || type == "smallint" || type == "mediumint" || type == "int" || type == "bigint") {
			return WidenAction::Widen;
		}
		return WidenAction::Unexpected;
	}

	size_t widenColumns(IDatabase &db, const std::string &schemaName) {
		return widenColumns(db, schemaName, wantedColumnWidenings());
	}

	size_t widenColumns(IDatabase &db, const std::string &schemaName, const std::vector<WantedColumnWidening> &wanted) {
		size_t widened = 0;
		for (const auto &column : wanted) {
			const auto info = db.storeQuery(fmt::format(
				"SELECT `DATA_TYPE` AS `data_type`, `COLUMN_TYPE` AS `column_type` FROM `information_schema`.`COLUMNS` "
				"WHERE `TABLE_SCHEMA` = {} AND `TABLE_NAME` = {} AND `COLUMN_NAME` = {}",
				db.escapeString(schemaName), db.escapeString(std::string(column.table)), db.escapeString(std::string(column.column))
			));
			const auto dataType = info ? info->getString("data_type") : std::string {};
			const auto columnType = info ? asLowerCaseString(info->getString("column_type")) : std::string {};

			switch (widenActionFor(dataType)) {
				case WidenAction::AlreadyWide:
					continue;
				case WidenAction::Missing:
					g_logger().warn("[schema_hardening] column {}.{} not found; skipping its widening", column.table, column.column);
					continue;
				case WidenAction::Unexpected:
					g_logger().warn("[schema_hardening] column {}.{} is {} (upstream changed it?); left as it is", column.table, column.column, columnType);
					continue;
				case WidenAction::Widen:
					break;
			}

			g_logger().info("[schema_hardening] widening {}.{} from {} ({}); a large table may take a while...", column.table, column.column, columnType, column.reason);
			Benchmark bm;
			const bool isSigned = columnType.find("unsigned") == std::string::npos;
			const bool hasNegatives = isSigned && db.storeQuery(fmt::format("SELECT 1 FROM `{}` WHERE `{}` < 0 LIMIT 1", column.table, column.column)) != nullptr;
			bool ok = true;
			if (hasNegatives) {
				// A 32-bit value stored as signed: widen while still signed, then add 2^32 back.
				ok = db.executeQuery(fmt::format("ALTER TABLE `{}` MODIFY `{}` DECIMAL(39,0) NOT NULL", column.table, column.column))
					&& db.executeQuery(fmt::format("UPDATE `{0}` SET `{1}` = `{1}` + 4294967296 WHERE `{1}` < 0", column.table, column.column));
			}
			ok = ok && db.executeQuery(fmt::format("ALTER TABLE `{}` MODIFY `{}` {}", column.table, column.column, column.definition));
			if (!ok) {
				g_logger().error("[schema_hardening] failed to widen {}.{}; the server keeps running with the old type", column.table, column.column);
				continue;
			}
			g_logger().info("[schema_hardening] {}.{} widened to {} in {:.0f} ms", column.table, column.column, column.definition, bm.duration());
			++widened;
		}
		return widened;
	}

} // namespace schema_hardening
