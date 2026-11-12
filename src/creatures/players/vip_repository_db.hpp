/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/vip_repository.hpp"

class IDatabase;

/// IVipRepository adapter that owns the VIP list SQL; the database is injected.
class VipRepositoryDB final : public IVipRepository {
public:
	explicit VipRepositoryDB(IDatabase &db) :
		db_(db) { }

	[[nodiscard]] std::vector<VIPEntry> getVIPEntries(uint32_t accountId) override;
	void addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) override;
	void editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) override;
	void removeVIPEntry(uint32_t accountId, uint32_t guid) override;

	[[nodiscard]] std::vector<VIPGroupEntry> getVIPGroupEntries(uint32_t accountId, uint32_t guid) override;
	void addVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) override;
	void editVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) override;
	void removeVIPGroupEntry(uint8_t groupId, uint32_t accountId) override;
	void addGuidVIPGroupEntry(uint8_t groupId, uint32_t accountId, uint32_t guid) override;
	void removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) override;

private:
	IDatabase &db_;
};
