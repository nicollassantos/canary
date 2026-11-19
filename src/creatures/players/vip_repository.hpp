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
	#include <cstdint>
	#include <string>
	#include <vector>
#endif

struct VIPEntry;
struct VIPGroupEntry;

/**
 * Persistence port for account VIP lists and VIP groups.
 */
class IVipRepository {
public:
	IVipRepository() = default;
	virtual ~IVipRepository() = default;

	IVipRepository(const IVipRepository &) = delete;
	void operator=(const IVipRepository &) = delete;

	static IVipRepository &getInstance();

	[[nodiscard]] virtual std::vector<VIPEntry> getVIPEntries(uint32_t accountId) = 0;
	virtual void addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) = 0;
	virtual void editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) = 0;
	/// The entry's fields and its whole group list in one write (one transaction): editing an entry from the
	/// client used to be an UPDATE, a DELETE and one INSERT per group, each with its own commit.
	virtual void replaceVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify, const std::vector<uint32_t> &groupIds) = 0;
	virtual void removeVIPEntry(uint32_t accountId, uint32_t guid) = 0;

	[[nodiscard]] virtual std::vector<VIPGroupEntry> getVIPGroupEntries(uint32_t accountId, uint32_t guid) = 0;
	// Group ids here are account_vipgroups.id (VIPGroup::storedId), never the 1..8 the client sees.
	/// Creates the group and returns its stored id (0 when it could not be written).
	[[nodiscard]] virtual uint32_t addVIPGroupEntry(uint32_t accountId, const std::string &groupName, bool customizable) = 0;
	virtual void editVIPGroupEntry(uint32_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) = 0;
	virtual void removeVIPGroupEntry(uint32_t groupId, uint32_t accountId) = 0;
	virtual void addGuidVIPGroupEntry(uint32_t groupId, uint32_t accountId, uint32_t guid) = 0;
	virtual void removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) = 0;
};

constexpr auto g_vipRepository = IVipRepository::getInstance;
