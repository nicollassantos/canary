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
#include "game/game_definitions.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <chrono>
	#include <cstdint>
	#include <memory>
	#include <mutex>
	#include <unordered_map>
	#include <optional>
	#include <string>
	#include <vector>
#endif

class Account;
class Game;
class IConfigManager;
class Player;
class Tile;

/// What the login rules read from the world (role port). GameLoginWorld reads the Game; tests fake it.
class LoginWorld {
public:
	virtual ~LoginWorld() = default;
	[[nodiscard]] virtual GameState_t gameState() const = 0;
	/// The characters of `account` in the world.
	[[nodiscard]] virtual std::vector<std::shared_ptr<Player>> playersOfAccount(const std::shared_ptr<Account> &account) = 0;
};

class GameLoginWorld final : public LoginWorld {
public:
	explicit GameLoginWorld(Game &game) :
		game(game) { }
	[[nodiscard]] GameState_t gameState() const override;
	[[nodiscard]] std::vector<std::shared_ptr<Player>> playersOfAccount(const std::shared_ptr<Account> &account) override;

private:
	Game &game;
};

/// Who may enter the world: the admission rules of a login (use case of ProtocolGame::login and of
/// the login server). The protocol adapters authenticate, load and place the character and talk to
/// the client; this decides. A refusal is the message the client is disconnected with.
class LoginService {
public:
	LoginService(LoginWorld &world, IConfigManager &config, IBanRepository &bans);

	/// A new session, once the character is preloaded (account, group and flags known) and before it
	/// is loaded: namelock, server closing or closed, premium-only server, characters of the account
	/// already online, account ban.
	[[nodiscard]] std::optional<std::string> refuseBeforeLoad(const Player &player, uint32_t accountId) const;

	/// Once loaded, for the tile it logs in on: characters of the account outside a protection zone.
	[[nodiscard]] std::optional<std::string> refuseOutsideProtectionZone(const std::shared_ptr<Player> &player, const Tile &loginTile) const;

	/// A connection, before any character: the address ban. Runs on the network threads and reads the
	/// snapshot refreshAddressBans keeps, not the database (one query per pre-login connection let a
	/// client with many addresses make the network wait on the game's connection); before the first
	/// snapshot it asks the repository. A ban made or lifted shows within AddressBanRefresh.
	[[nodiscard]] std::optional<std::string> refuseBannedAddress(uint32_t ip) const;

	static constexpr std::chrono::seconds AddressBanRefresh { 30 };
	/// Reloads the address bans (Game schedules it every AddressBanRefresh). A failed read keeps the
	/// snapshot it had.
	void refreshAddressBans();

	static std::string accountBanMessage(BanInfo ban);
	static std::string addressBanMessage(BanInfo ban);

private:
	LoginWorld &world;
	IConfigManager &config;
	IBanRepository &bans;

	mutable std::mutex addressBansMutex;
	/// Nothing before the first successful refresh.
	mutable std::optional<std::unordered_map<uint32_t, BanInfo>> addressBans;
};
