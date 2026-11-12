/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/players/player_repository.hpp"

class PlayerRepositoryDB final : public IPlayerRepository {
public:
	PlayerRepositoryDB() = default;

	[[nodiscard]] bool loadPlayerById(const std::shared_ptr<Player> &player, uint32_t id, bool disableIrrelevantInfo) override;
	[[nodiscard]] bool loadPlayerByName(const std::shared_ptr<Player> &player, const std::string &name) override;
	[[nodiscard]] bool savePlayer(const std::shared_ptr<Player> &player) override;

	[[nodiscard]] uint32_t getGuidByName(const std::string &name) override;
	[[nodiscard]] std::string getNameByGuid(uint32_t guid) override;
	void increaseBankBalance(uint32_t guid, uint64_t amount) override;

	[[nodiscard]] bool formatPlayerName(std::string &name) override;
	[[nodiscard]] bool getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) override;
	[[nodiscard]] bool gameWorldAuthentication(const std::string &accountDescriptor, const std::string &sessionOrPassword, std::string &characterName, uint32_t &accountId, bool oldProtocol, uint32_t ip) override;
};
