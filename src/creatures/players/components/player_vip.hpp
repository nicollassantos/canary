/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/creatures_definitions.hpp"

class Player;

class VIPGroup {
public:
	/// What the client sees (PlayerVIP::firstID..lastID), given when the group is loaded or created.
	uint8_t id = 0;
	/// account_vipgroups.id, used for every write; 0 when the group was never stored (no account).
	uint32_t storedId = 0;
	std::string name;
	bool customizable = false;
	phmap::flat_hash_set<uint32_t> vipGroupGuids;

	VIPGroup() = default;
	VIPGroup(uint8_t id, std::string name, bool customizable, uint32_t storedId = 0) :
		id(id), storedId(storedId), name(std::move(name)), customizable(customizable) { }
};

class PlayerVIP {
public:
	/// Matches account_viplist.description and account_vipgroups.name (varchar(128)).
	static constexpr size_t MaxTextLength = 128;

	explicit PlayerVIP(Player &player);

	static const uint8_t firstID;
	static const uint8_t lastID;

	size_t getMaxEntries() const;
	uint8_t getMaxGroupEntries() const;

	VipStatus_t getStatus() const {
		return status;
	}
	void setStatus(VipStatus_t newStatus) {
		status = newStatus;
	}

	void notifyStatusChange(const std::shared_ptr<Player> &loginPlayer, VipStatus_t status, bool message = true) const;
	bool remove(uint32_t vipGuid);
	bool add(uint32_t vipGuid, const std::string &vipName, VipStatus_t status);
	bool addInternal(uint32_t vipGuid);
	bool edit(uint32_t vipGuid, const std::string &description, uint32_t icon, bool notify, const std::vector<uint8_t> &groupsId) const;

	bool exists(uint32_t vipGuid) const;

	// VIP Group
	std::shared_ptr<VIPGroup> getGroupByID(uint8_t groupId) const;
	std::shared_ptr<VIPGroup> getGroupByName(const std::string &name) const;

	/// A group loaded from account_vipgroups: `storedId` is its row id; the client id is the first free one.
	void addGroupInternal(uint32_t storedId, const std::string &name, bool customizable);
	void removeGroup(uint8_t groupId);
	void addGroup(const std::string &rawName, bool customizable = true);
	void editGroup(uint8_t groupId, const std::string &rawName, bool customizable = true) const;

	void addGuidToGroupInternal(uint8_t groupId, uint32_t guid) const;
	/// A membership loaded from account_vipgrouplist, whose vipgroup_id is the stored id.
	void addGuidToStoredGroupInternal(uint32_t storedId, uint32_t guid) const;

	uint8_t getFreeId() const;
	std::vector<uint8_t> getGroupsIdGuidBelongs(uint32_t guid) const;

	[[nodiscard]] const std::vector<std::shared_ptr<VIPGroup>> &getGroups() const {
		return vipGroups;
	}

private:
	Player &m_player;

	VipStatus_t status = VipStatus_t::Online;
	std::vector<std::shared_ptr<VIPGroup>> vipGroups;
	phmap::flat_hash_set<uint32_t> vipGuids;
};
