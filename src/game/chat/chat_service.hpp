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

class Game;
class IConfigManager;
class Creature;
class Player;
struct Position;
struct Spectators;
enum SpeakClasses : uint8_t;

class ChatService {
public:
	ChatService(Game &game, IConfigManager &config) :
		game_(game), config_(config) { }

	bool playerBroadcastMessage(const std::shared_ptr<Player> &player, const std::string &text) const;
	void playerCreatePrivateChannel(uint32_t playerId);
	void playerChannelInvite(uint32_t playerId, const std::string &name);
	void playerChannelExclude(uint32_t playerId, const std::string &name);
	void playerRequestChannels(uint32_t playerId);
	void playerOpenChannel(uint32_t playerId, uint16_t channelId);
	void playerCloseChannel(uint32_t playerId, uint16_t channelId);
	void playerOpenPrivateChannel(uint32_t playerId, std::string &receiver);
	void playerCloseNpcChannel(uint32_t playerId);
	void playerReceivePing(uint32_t playerId);
	void playerReceivePingBack(uint32_t playerId);
	void playerSay(uint32_t playerId, uint16_t channelId, SpeakClasses type, const std::string &receiver, const std::string &text);
	bool playerSaySpell(const std::shared_ptr<Player> &player, SpeakClasses type, const std::string &text);
	void playerWhisper(const std::shared_ptr<Player> &player, const std::string &text);
	bool playerYell(const std::shared_ptr<Player> &player, const std::string &text);
	bool playerSpeakTo(const std::shared_ptr<Player> &player, SpeakClasses type, const std::string &receiver, const std::string &text);
	void playerSpeakToNpc(const std::shared_ptr<Player> &player, const std::string &text);

private:
	Game &game_;
	IConfigManager &config_;
};
