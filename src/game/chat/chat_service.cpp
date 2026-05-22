/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/chat/chat_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/creature.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "io/iologindata.hpp"
#include "lua/creature/actions.hpp"
#include "lua/creature/talkaction.hpp"
#include "lua/global/globalevent.hpp"
#include "map/spectators.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "utils/tools.hpp"

bool ChatService::playerBroadcastMessage(const std::shared_ptr<Player> &player, const std::string &text) const {
	if (!player->hasFlag(PlayerFlags_t::CanBroadcast)) {
		return false;
	}

	g_logger().info("{} broadcasted: {}", player->getName(), text);

	for (const auto &it : game_.getPlayers()) {
		it.second->sendPrivateMessage(player, TALKTYPE_BROADCAST, text);
	}

	return true;
}

void ChatService::playerCreatePrivateChannel(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || !player->isPremium()) {
		return;
	}

	const auto &channel = g_chat().createChannel(player, CHANNEL_PRIVATE);
	if (!channel || !channel->addUser(player)) {
		return;
	}

	player->sendCreatePrivateChannel(channel->getId(), channel->getName());
}

void ChatService::playerChannelInvite(uint32_t playerId, const std::string &name) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &channel = g_chat().getPrivateChannel(player);
	if (!channel) {
		return;
	}

	std::shared_ptr<Player> invitePlayer = game_.getPlayerByName(name);
	if (!invitePlayer) {
		return;
	}

	if (player == invitePlayer) {
		return;
	}

	channel->invitePlayer(player, invitePlayer);
}

void ChatService::playerChannelExclude(uint32_t playerId, const std::string &name) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &channel = g_chat().getPrivateChannel(player);
	if (!channel) {
		return;
	}

	std::shared_ptr<Player> excludePlayer = game_.getPlayerByName(name);
	if (!excludePlayer) {
		return;
	}

	if (player == excludePlayer) {
		return;
	}

	channel->excludePlayer(player, excludePlayer);
}

void ChatService::playerRequestChannels(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendChannelsDialog();
}

void ChatService::playerOpenChannel(uint32_t playerId, uint16_t channelId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const auto &channel = g_chat().addUserToChannel(player, channelId);
	if (!channel) {
		return;
	}

	const InvitedMap* invitedUsers = channel->getInvitedUsers();
	const UsersMap* users;
	if (!channel->isPublicChannel()) {
		users = &channel->getUsers();
	} else {
		users = nullptr;
	}

	player->sendChannel(channel->getId(), channel->getName(), users, invitedUsers);
}

void ChatService::playerCloseChannel(uint32_t playerId, uint16_t channelId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	g_chat().removeUserFromChannel(player, channelId);
}

void ChatService::playerOpenPrivateChannel(uint32_t playerId, std::string &receiver) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (!IOLoginData::formatPlayerName(receiver)) {
		player->sendCancelMessage("A player with this name does not exist.");
		return;
	}

	if (player->getName() == receiver) {
		player->sendCancelMessage("You cannot set up a private message channel with yourself.");
		return;
	}

	player->sendOpenPrivateChannel(receiver);
}

void ChatService::playerCloseNpcChannel(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	for (const auto &spectator : Spectators().find<Creature>(player->getPosition()).filter<Npc>()) {
		spectator->getNpc()->onPlayerCloseChannel(player);
	}
}

void ChatService::playerReceivePing(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->receivePing();
}

void ChatService::playerReceivePingBack(uint32_t playerId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->sendPingBack();
}

void ChatService::playerSay(uint32_t playerId, uint16_t channelId, SpeakClasses type, const std::string &receiver, const std::string &text) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	player->resetLoginProtection();
	player->resetIdleTime();

	if (playerSaySpell(player, type, text)) {
		return;
	}

	uint32_t muteTime = player->isMuted();
	if (muteTime > 0) {
		std::ostringstream ss;
		ss << "You are still muted for " << muteTime << " seconds.";
		player->sendTextMessage(MESSAGE_FAILURE, ss.str());
		return;
	}

	if (!text.empty() && text.front() == '/' && player->isAccessPlayer()) {
		return;
	}

	if (type != TALKTYPE_PRIVATE_PN) {
		player->removeMessageBuffer();
	}

	switch (type) {
		case TALKTYPE_SAY:
			game_.internalCreatureSay(player, TALKTYPE_SAY, text, false);
			break;

		case TALKTYPE_WHISPER:
			playerWhisper(player, text);
			break;

		case TALKTYPE_YELL:
			playerYell(player, text);
			break;

		case TALKTYPE_PRIVATE_TO:
		case TALKTYPE_PRIVATE_RED_TO:
			playerSpeakTo(player, type, receiver, text);
			break;

		case TALKTYPE_CHANNEL_O:
		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
			g_chat().talkToChannel(player, type, text, channelId);
			break;

		case TALKTYPE_PRIVATE_PN:
			playerSpeakToNpc(player, text);
			break;

		case TALKTYPE_BROADCAST:
			playerBroadcastMessage(player, text);
			break;

		default:
			break;
	}
}

bool ChatService::playerSaySpell(const std::shared_ptr<Player> &player, SpeakClasses type, const std::string &text) {
	if (player->walkExhausted()) {
		return true;
	}

	std::string words = text;
	TalkActionResult_t result = g_talkActions().checkPlayerCanSayTalkAction(player, type, words);
	if (result == TALKACTION_BREAK) {
		return true;
	}

	result = g_spells().playerSaySpell(player, words);
	if (result == TALKACTION_BREAK) {
		if (!config_.getBoolean(PUSH_WHEN_ATTACKING)) {
			player->cancelPush();
		}
		return player->saySpell(type, words, false);
	} else if (result == TALKACTION_FAILED) {
		return true;
	}

	return false;
}

void ChatService::playerWhisper(const std::shared_ptr<Player> &player, const std::string &text) {
	auto spectators = Spectators().find<Player>(player->getPosition(), false, MAP_MAX_CLIENT_VIEW_PORT_X, MAP_MAX_CLIENT_VIEW_PORT_X, MAP_MAX_CLIENT_VIEW_PORT_Y, MAP_MAX_CLIENT_VIEW_PORT_Y);

	// Send to client
	for (const auto &spectator : spectators) {
		if (const auto &spectatorPlayer = spectator->getPlayer()) {
			if (!Position::areInRange<1, 1>(player->getPosition(), spectatorPlayer->getPosition())) {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, "pspsps");
			} else {
				spectatorPlayer->sendCreatureSay(player, TALKTYPE_WHISPER, text);
			}
		}
	}

	// event method
	for (const auto &spectator : spectators) {
		spectator->onCreatureSay(player, TALKTYPE_WHISPER, text);
	}
}

bool ChatService::playerYell(const std::shared_ptr<Player> &player, const std::string &text) {
	if (player->getLevel() == 1) {
		player->sendTextMessage(MESSAGE_FAILURE, "You may not yell as long as you are on level 1.");
		return false;
	}

	if (player->hasCondition(CONDITION_YELLTICKS)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (player->getAccountType() < AccountType::ACCOUNT_TYPE_GAMEMASTER) {
		auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_YELLTICKS, 30000, 0);
		player->addCondition(condition);
	}

	game_.internalCreatureSay(player, TALKTYPE_YELL, asUpperCaseString(text), false);
	return true;
}

bool ChatService::playerSpeakTo(const std::shared_ptr<Player> &player, SpeakClasses type, const std::string &receiver, const std::string &text) {
	std::shared_ptr<Player> toPlayer = game_.getPlayerByName(receiver);
	if (!toPlayer) {
		player->sendTextMessage(MESSAGE_FAILURE, "A player with this name is not online.");
		return false;
	}

	if (type == TALKTYPE_PRIVATE_RED_TO && (player->hasFlag(PlayerFlags_t::CanTalkRedPrivate) || player->getAccountType() >= AccountType::ACCOUNT_TYPE_GAMEMASTER)) {
		type = TALKTYPE_PRIVATE_RED_FROM;
	} else {
		type = TALKTYPE_PRIVATE_FROM;
	}

	toPlayer->sendPrivateMessage(player, type, text);
	toPlayer->onCreatureSay(player, type, text);

	if (toPlayer->isInGhostMode() && !player->isAccessPlayer()) {
		player->sendTextMessage(MESSAGE_FAILURE, "A player with this name is not online.");
	} else {
		std::ostringstream ss;
		ss << "Message sent to " << toPlayer->getName() << '.';
		player->sendTextMessage(MESSAGE_FAILURE, ss.str());
	}
	return true;
}

void ChatService::playerSpeakToNpc(const std::shared_ptr<Player> &player, const std::string &text) {
	if (player == nullptr) {
		g_logger().error("[ChatService::playerSpeakToNpc] - Player is nullptr");
		return;
	}

	// Check npc say exhausted
	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	for (const auto &spectator : Spectators().find<Creature>(player->getPosition()).filter<Npc>()) {
		if (!player->canSpeakWithHireling(spectator->getNpc()->getSpeechBubble())) {
			continue;
		}

		spectator->getNpc()->onCreatureSay(player, TALKTYPE_PRIVATE_PN, text);
	}

	player->updateUIExhausted();
}
