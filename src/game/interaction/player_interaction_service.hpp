/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "utils/request_cooldown.hpp"
#include "utils/tools.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Game;
class IConfigManager;
class Container;
class House;
class Item;
class Player;
struct Outfit_t;
struct Position;

class PlayerInteractionService {
public:
	/// A house access list edit parses the whole text (regex) and may look up every name in the
	/// database; legitimate lists are at most 100 short lines.
	static constexpr size_t MaxHouseListLength = 8192;
	static constexpr int64_t HouseListCooldownMs = 1000;
	/// Bug reports are appended to a file per player by the datapack (Player:onReportBug), so their
	/// size and rate are bounded to keep a client from filling the disk.
	static constexpr size_t MaxBugReportLength = 2048;
	static constexpr int64_t BugReportCooldownMs = 60000;
	/// Rule violation reports go to reports/players/<reporter>-<target>-<type>.txt (datapack), one
	/// file per target, so the target must be a plain name and reports are rate limited.
	static constexpr int64_t RuleViolationReportCooldownMs = 10000;
	/// One exiva restrictions edit (no-pvp worlds) resolves up to four lists of maxExivaWhitelist
	/// names, each with a synchronous database lookup.
	static constexpr int64_t ExivaRestrictionsCooldownMs = 2000;
	/// Weapon proficiency action 0x01 sends one window per weapon the player has trained.
	static constexpr int64_t ProficiencyListCooldownMs = 1000;

	PlayerInteractionService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	void playerUseItemEx(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint16_t fromItemId, const Position &toPos, uint8_t toStackPos, uint16_t toItemId);
	void playerUseItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint8_t index, uint16_t itemId);
	void playerEquipItem(uint32_t playerId, uint16_t itemId, bool hasTier = false, uint8_t tier = 0);
	void playerUseWithCreature(uint32_t playerId, const Position &fromPos, uint8_t fromStackPos, uint32_t creatureId, uint16_t itemId);

	void playerCloseContainer(uint32_t playerId, uint8_t cid);
	void playerMoveUpContainer(uint32_t playerId, uint8_t cid);
	void playerUpdateContainer(uint32_t playerId, uint8_t cid);

	void playerRotateItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	void playerConfigureShowOffSocket(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	void playerSetShowOffSocket(uint32_t playerId, Outfit_t &outfit, const Position &pos, uint8_t stackPos, uint16_t itemId, uint8_t podiumVisible, uint8_t direction);

	void playerWrapableItem(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId);
	std::shared_ptr<Item> wrapItem(const std::shared_ptr<Item> &item, const std::shared_ptr<House> &house);
	void unwrapItem(const std::shared_ptr<Item> &item, uint16_t unWrapId, const std::shared_ptr<House> &house, const std::shared_ptr<Player> &player);

	void playerWriteItem(uint32_t playerId, uint32_t windowTextId, const std::string &text);
	void playerBrowseField(uint32_t playerId, const Position &pos);
	void playerStowItem(uint32_t playerId, const Position &pos, uint16_t itemId, uint8_t stackpos, uint8_t count, bool allItems);
	void playerStashWithdraw(uint32_t playerId, uint16_t itemId, uint32_t count, uint8_t stackpos);
	void playerSeekInContainer(uint32_t playerId, uint8_t containerId, uint16_t index, uint8_t containerCategory);
	void playerUpdateHouseWindow(uint32_t playerId, uint8_t listId, uint32_t windowTextId, const std::string &text);

	void playerApplyImbuement(uint32_t playerId, uint16_t imbuementid, uint8_t slot);
	void playerClearImbuement(uint32_t playerid, uint8_t slot);
	void playerCloseImbuementWindow(uint32_t playerid);
	void playerRequestInventoryImbuements(uint32_t playerId, bool isTrackerOpen);

	void playerRequestAddVip(uint32_t playerId, const std::string &name);
	void playerRequestRemoveVip(uint32_t playerId, uint32_t guid);
	void playerRequestEditVip(uint32_t playerId, uint32_t guid, const std::string &description, uint32_t icon, bool notify, std::vector<uint8_t> vipGroupsId);

	void playerCheckActivity(const std::string &playerName, int interval);

	void playerReportBug(uint32_t playerId, const std::string &message, const Position &position, uint8_t category);
	void playerReportRuleViolationReport(uint32_t playerId, const std::string &targetName, uint8_t reportType, uint8_t reportReason, const std::string &comment, const std::string &translation);

	/// True when the player may edit exiva restrictions now. A refused player is told they are
	/// exhausted and counted in client_requests_throttled{request="exiva_restrictions"}.
	bool admitExivaRestrictionsEdit(const std::shared_ptr<Player> &player);
	/// Same for the weapon proficiency list (action 0x01), request="proficiency_list".
	bool admitProficiencyListing(const std::shared_ptr<Player> &player);

	/// A player name as the client shows it: 1 to 30 letters, spaces, apostrophes or hyphens.
	/// The report target ends up in a file path, so nothing else is accepted.
	static bool isReportableName(std::string_view name);

	[[nodiscard]] uint64_t rejectedBugReports() const {
		return rejectedBugReportCount;
	}

	[[nodiscard]] uint64_t rejectedRuleViolationReports() const {
		return rejectedRuleViolationReportCount;
	}

	[[nodiscard]] uint64_t rejectedHouseListUpdates() const {
		return rejectedHouseLists;
	}

	/// Milliseconds clock for the cooldowns; tests replace it.
	std::function<int64_t()> now = [] { return OTSYS_TIME(); };

private:
	Game &game_;
	IConfigManager &config_;
	RequestCooldown houseListCooldown { HouseListCooldownMs };
	uint64_t rejectedHouseLists = 0;
	RequestCooldown bugReportCooldown { BugReportCooldownMs };
	uint64_t rejectedBugReportCount = 0;
	RequestCooldown ruleViolationReportCooldown { RuleViolationReportCooldownMs };
	uint64_t rejectedRuleViolationReportCount = 0;
	RequestCooldown exivaRestrictionsCooldown { ExivaRestrictionsCooldownMs };
	RequestCooldown proficiencyListCooldown { ProficiencyListCooldownMs };
};
