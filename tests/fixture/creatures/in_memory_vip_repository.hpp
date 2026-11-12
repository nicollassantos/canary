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
#include "creatures/players/vip_repository.hpp"
#include "lib/di/container.hpp"

#include <map>
#include <set>
#include <utility>

class InMemoryVipRepository final : public IVipRepository {
public:
	struct Entry {
		std::string description;
		uint32_t icon = 0;
		bool notify = false;
	};

	struct Group {
		std::string name;
		bool customizable = false;
	};

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<IVipRepository>.to<InMemoryVipRepository>().in(di::singleton));
		return injector;
	}

	std::vector<VIPEntry> getVIPEntries(uint32_t accountId) override {
		std::vector<VIPEntry> result;
		for (const auto &[key, entry] : entries) {
			if (key.first == accountId) {
				result.emplace_back(key.second, "", entry.description, entry.icon, entry.notify);
			}
		}
		return result;
	}

	void addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) override {
		entries[{ accountId, guid }] = Entry { description, icon, notify };
	}

	void editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) override {
		entries[{ accountId, guid }] = Entry { description, icon, notify };
	}

	void removeVIPEntry(uint32_t accountId, uint32_t guid) override {
		entries.erase({ accountId, guid });
	}

	std::vector<VIPGroupEntry> getVIPGroupEntries(uint32_t accountId, uint32_t) override {
		std::vector<VIPGroupEntry> result;
		for (const auto &[key, group] : groups) {
			if (key.second == accountId) {
				result.emplace_back(key.first, group.name, group.customizable);
			}
		}
		return result;
	}

	void addVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) override {
		groups[{ groupId, accountId }] = Group { groupName, customizable };
	}

	void editVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) override {
		groups[{ groupId, accountId }] = Group { groupName, customizable };
	}

	void removeVIPGroupEntry(uint8_t groupId, uint32_t accountId) override {
		groups.erase({ groupId, accountId });
	}

	void addGuidVIPGroupEntry(uint8_t groupId, uint32_t accountId, uint32_t guid) override {
		groupMembers.insert({ accountId, guid, groupId });
	}

	void removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) override {
		std::erase_if(groupMembers, [&](const auto &member) {
			return std::get<0>(member) == accountId && std::get<1>(member) == guid;
		});
	}

	[[nodiscard]] bool hasEntry(uint32_t accountId, uint32_t guid) const {
		return entries.contains({ accountId, guid });
	}

	[[nodiscard]] const Entry &entry(uint32_t accountId, uint32_t guid) const {
		return entries.at({ accountId, guid });
	}

	[[nodiscard]] bool hasGroup(uint8_t groupId, uint32_t accountId) const {
		return groups.contains({ groupId, accountId });
	}

	[[nodiscard]] bool isGroupMember(uint32_t accountId, uint32_t guid, uint8_t groupId) const {
		return groupMembers.contains({ accountId, guid, groupId });
	}

private:
	std::map<std::pair<uint32_t, uint32_t>, Entry> entries;
	std::map<std::pair<uint8_t, uint32_t>, Group> groups;
	std::set<std::tuple<uint32_t, uint32_t, uint8_t>> groupMembers;
};
