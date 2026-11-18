/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "account/ban_repository.hpp"

class IDatabase;

/// IBanRepository on the database: `account_bans`, `account_ban_history`, `ip_bans`,
/// `player_namelocks` (the SQL of the former IOBan).
class BanRepositoryDB : public IBanRepository {
public:
	explicit BanRepositoryDB(IDatabase &db);

	[[nodiscard]] std::optional<BanInfo> findAccountBan(uint32_t accountId) override;
	[[nodiscard]] std::optional<BanInfo> findAddressBan(uint32_t ip) override;
	[[nodiscard]] bool isNamelocked(uint32_t playerGuid) override;
	[[nodiscard]] std::optional<std::vector<AddressBan>> loadAddressBans() override;
	void forgetExpiredAddressBan(uint32_t ip) override;

protected:
	/// Moving or removing an expired ban does not hold the caller: by default it runs on
	/// DatabaseTasks. Tests override it to see the statements.
	virtual void runLater(const std::string &query);
	/// The clock that decides whether a ban expired.
	virtual time_t now() const;

private:
	IDatabase &db;
};
