/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/login/login_service.hpp"

#include "account/account.hpp"
#include "config/config_port.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "enums/account_group_type.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "items/tile.hpp"
#include "utils/tools.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <fmt/format.h>
#endif

GameState_t GameLoginWorld::gameState() const {
	return game.getGameState();
}

std::vector<std::shared_ptr<Player>> GameLoginWorld::playersOfAccount(const std::shared_ptr<Account> &account) {
	return game.getPlayersByAccount(account);
}

LoginService::LoginService(LoginWorld &world, IConfigManager &config, IBanRepository &bans) :
	world(world), config(config), bans(bans) { }

std::optional<std::string> LoginService::refuseBeforeLoad(const Player &player, uint32_t accountId) const {
	if (bans.isNamelocked(player.getGUID())) {
		return "Your character has been namelocked.";
	}

	const bool canAlwaysLogin = player.hasFlag(PlayerFlags_t::CanAlwaysLogin);
	if (world.gameState() == GAME_STATE_CLOSING && !canAlwaysLogin) {
		return "The game is just going down.\nPlease try again later.";
	}

	if (world.gameState() == GAME_STATE_CLOSED && !canAlwaysLogin) {
		const auto &maintainMessage = config.getString(MAINTAIN_MODE_MESSAGE);
		return !maintainMessage.empty() ? maintainMessage : "Server is currently closed.\nPlease try again later.";
	}

	if (config.getBoolean(ONLY_PREMIUM_ACCOUNT) && !player.isPremium() && (player.getGroup()->id < GROUP_TYPE_GAMEMASTER || player.getAccountType() < ACCOUNT_TYPE_GAMEMASTER)) {
		return "Your premium time for this account is out.\n\nTo play please buy additional premium time from our website";
	}

	const auto onlineCount = world.playersOfAccount(player.getAccount()).size();
	const auto maxOnline = config.getNumber(MAX_PLAYERS_PER_ACCOUNT);
	if (player.getAccountType() < ACCOUNT_TYPE_GAMEMASTER && onlineCount >= static_cast<size_t>(maxOnline)) {
		return fmt::format("You may only login with {} character{}\nof your account at the same time.", maxOnline, maxOnline > 1 ? "s" : "");
	}

	if (!player.hasFlag(PlayerFlags_t::CannotBeBanned)) {
		if (auto ban = bans.findAccountBan(accountId)) {
			return accountBanMessage(std::move(*ban));
		}
	}

	return std::nullopt;
}

std::optional<std::string> LoginService::refuseOutsideProtectionZone(const std::shared_ptr<Player> &player, const Tile &loginTile) const {
	const auto maxOnline = config.getNumber(MAX_PLAYERS_PER_ACCOUNT);
	if (maxOnline <= 1 || player->getAccountType() >= ACCOUNT_TYPE_GAMEMASTER || loginTile.hasFlag(TILESTATE_PROTECTIONZONE)) {
		return std::nullopt;
	}

	const auto maxOutsidePz = config.getNumber(MAX_PLAYERS_OUTSIDE_PZ_PER_ACCOUNT);
	int32_t outsidePz = 0;
	for (const auto &accountPlayer : world.playersOfAccount(player->getAccount())) {
		if (accountPlayer != player && accountPlayer->getTile() && !accountPlayer->getTile()->hasFlag(TILESTATE_PROTECTIONZONE)) {
			++outsidePz;
		}
	}
	if (outsidePz < maxOutsidePz) {
		return std::nullopt;
	}
	return fmt::format("You can only have {} character{} from your account outside of a protection zone.", maxOutsidePz == 1 ? "one" : std::to_string(maxOutsidePz), maxOutsidePz > 1 ? "s" : "");
}

std::optional<std::string> LoginService::refuseBannedAddress(uint32_t ip) const {
	if (auto ban = bans.findAddressBan(ip)) {
		return addressBanMessage(std::move(*ban));
	}
	return std::nullopt;
}

std::string LoginService::accountBanMessage(BanInfo ban) {
	if (ban.reason.empty()) {
		ban.reason = "(none)";
	}
	if (ban.expiresAt > 0) {
		return fmt::format("Your account has been banned until {} by {}.\n\nReason specified:\n{}", formatDateShort(ban.expiresAt), ban.bannedBy, ban.reason);
	}
	return fmt::format("Your account has been permanently banned by {}.\n\nReason specified:\n{}", ban.bannedBy, ban.reason);
}

std::string LoginService::addressBanMessage(BanInfo ban) {
	if (ban.reason.empty()) {
		ban.reason = "(none)";
	}
	return fmt::format("Your IP has been banned until {} by {}.\n\nReason specified:\n{}", formatDateShort(ban.expiresAt), ban.bannedBy, ban.reason);
}
