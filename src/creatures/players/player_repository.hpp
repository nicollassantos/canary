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
#include <optional>
#include <vector>

#include "map/house/house_access_facts.hpp"

class Player;

class IPlayerRepository {
public:
	IPlayerRepository() = default;
	virtual ~IPlayerRepository() = default;

	IPlayerRepository(const IPlayerRepository &) = delete;
	void operator=(const IPlayerRepository &) = delete;

	static IPlayerRepository &getInstance();

	[[nodiscard]] virtual bool loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id, bool disableIrrelevantInfo = true) = 0;
	[[nodiscard]] virtual bool loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) = 0;
	[[nodiscard]] virtual bool savePlayer(const std::shared_ptr<Player> &player) = 0;

	[[nodiscard]] virtual uint32_t getGuidByName(const std::string &name) = 0;
	[[nodiscard]] virtual std::string getNameByGuid(uint32_t guid) = 0;
	virtual void increaseBankBalance(uint32_t guid, uint64_t amount) = 0;

	/// Normalises `name` to the stored character name; false when no such character exists.
	[[nodiscard]] virtual bool formatPlayerName(std::string &name) = 0;
	/// Resolves guid and special-VIP flag by name, normalising `name`; false when not found.
	[[nodiscard]] virtual bool getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) = 0;
	/// Makes `players_online` hold exactly `onlineGuids` (empty clears it), in one transaction.
	/// Returns false when any statement fails; the table is then left as it was.
	[[nodiscard]] virtual bool updateOnlinePlayers(const std::vector<uint32_t> &onlineGuids) = 0;
	/// Account, group flag and guild rank of `guid` from one indexed query; std::nullopt when the
	/// player does not exist. Used instead of a full load where only house access is needed.
	[[nodiscard]] virtual std::optional<HouseAccessFacts> loadHouseAccessFacts(uint32_t guid) = 0;
	/// Validates a game-world login and resolves the account id.
	[[nodiscard]] virtual bool gameWorldAuthentication(const std::string &accountDescriptor, const std::string &sessionOrPassword, std::string &characterName, uint32_t &accountId, bool oldProtocol, uint32_t ip) = 0;
};

constexpr auto g_playerRepository = IPlayerRepository::getInstance;
