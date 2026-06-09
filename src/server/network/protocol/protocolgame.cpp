/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "server/network/protocol/protocolgame.hpp"
#include "server/network/commands/game_command_dispatcher.hpp"

#include "account/account.hpp"
#include "config/configmanager.hpp"
#include "core.hpp"
#include "creatures/appearance/mounts/mounts.hpp"
#include "creatures/appearance/attached_effects/attached_effects.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/combat/spells.hpp"
#include "creatures/interactions/chat.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/grouping/familiars.hpp"
#include "creatures/players/grouping/party.hpp"
#include "creatures/players/grouping/team_finder.hpp"
#include "creatures/players/highscore_category.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/livestream/livestream.hpp"
#include "creatures/players/management/ban.hpp"
#include "creatures/players/management/waitlist.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/components/player_forge_history.hpp"
#include "enums/player_icons.hpp"
#include "game/game.hpp"
#include "game/modal_window/modal_window.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "io/functions/iologindata_load_player.hpp"
#include "io/io_bosstiary.hpp"
#include "io/iobestiary.hpp"
#include "creatures/players/player_repository.hpp"
#include "io/iologindata.hpp"
#include "io/iomarket.hpp"
#include "io/guild_repository.hpp"
#include "io/ioguild.hpp"
#include "io/ioprey.hpp"
#include "items/items_classification.hpp"
#include "items/weapons/weapons.hpp"
#include "lua/creature/creatureevent.hpp"
#include "lua/modules/modules.hpp"
#include "server/network/message/outputmessage.hpp"
#include "utils/tools.hpp"
#include "creatures/players/vocations/vocation.hpp"

#include "enums/account_coins.hpp"
#include "enums/account_group_type.hpp"
#include "enums/account_type.hpp"
#include "enums/object_category.hpp"
#include "enums/player_blessings.hpp"
#include "enums/player_cyclopedia.hpp"
#include "enums/container_type.hpp"
#include "enums/imbuement.hpp"

#include "server/network/protocol/protocolgame_helpers.hpp"

ProtocolGame::ProtocolGame(const Connection_ptr &initConnection) :
	Protocol(initConnection) {
	version = CLIENT_VERSION;
}

void ProtocolGame::release() {
	// dispatcher thread
	if (m_isLivestreamViewer) {
		g_livestream().removeViewer(getThis());
		player = nullptr;
	} else if (player && player->client == shared_from_this()) {
		g_livestream().stopBroadcast(player, true);
		player->client.reset();
		player = nullptr;
	}

	OutputMessagePool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

void ProtocolGame::login(const std::string &name, uint32_t accountId, OperatingSystem_t operatingSystem) {
	sendClientLoginPreamble(operatingSystem);

	g_logger().debug("Player logging in in version '{}' and oldProtocol '{}'", getVersion(), oldProtocol);

	// dispatcher thread
	std::shared_ptr<Player> foundPlayer = g_game().getPlayerByName(name);
	if (!foundPlayer) {
		player = std::make_shared<Player>(getThis());
		player->setName(name);

		player->setID();

		if (!IOLoginDataLoad::preLoadPlayer(player, name)) {
			disconnectClient("Your character could not be loaded.");
			return;
		}

		if (IOBan::isPlayerNamelocked(player->getGUID())) {
			disconnectClient("Your character has been namelocked.");
			return;
		}

		if (g_game().getGameState() == GAME_STATE_CLOSING && !player->hasFlag(PlayerFlags_t::CanAlwaysLogin)) {
			disconnectClient("The game is just going down.\nPlease try again later.");
			return;
		}

		if (g_game().getGameState() == GAME_STATE_CLOSED && !player->hasFlag(PlayerFlags_t::CanAlwaysLogin)) {
			auto maintainMessage = g_configManager().getString(MAINTAIN_MODE_MESSAGE);
			if (!maintainMessage.empty()) {
				disconnectClient(maintainMessage);
			} else {
				disconnectClient("Server is currently closed.\nPlease try again later.");
			}
			return;
		}

		if (g_configManager().getBoolean(ONLY_PREMIUM_ACCOUNT) && !player->isPremium() && (player->getGroup()->id < GROUP_TYPE_GAMEMASTER || player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER)) {
			disconnectClient("Your premium time for this account is out.\n\nTo play please buy additional premium time from our website");
			return;
		}

		auto onlineCount = g_game().getPlayersByAccount(player->getAccount()).size();
		auto maxOnline = g_configManager().getNumber(MAX_PLAYERS_PER_ACCOUNT);
		if (player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER && onlineCount >= maxOnline) {
			disconnectClient(fmt::format("You may only login with {} character{}\nof your account at the same time.", maxOnline, maxOnline > 1 ? "s" : ""));
			return;
		}

		if (!player->hasFlag(PlayerFlags_t::CannotBeBanned)) {
			BanInfo banInfo;
			if (IOBan::isAccountBanned(accountId, banInfo)) {
				if (banInfo.reason.empty()) {
					banInfo.reason = "(none)";
				}

				std::ostringstream ss;
				if (banInfo.expiresAt > 0) {
					ss << "Your account has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy << ".\n\nReason specified:\n"
					   << banInfo.reason;
				} else {
					ss << "Your account has been permanently banned by " << banInfo.bannedBy << ".\n\nReason specified:\n"
					   << banInfo.reason;
				}
				disconnectClient(ss.str());
				return;
			}
		}

		WaitingList &waitingList = WaitingList::getInstance();
		if (!waitingList.clientLogin(player)) {
			auto currentSlot = static_cast<uint32_t>(waitingList.getClientSlot(player));
			auto retryTime = static_cast<uint32_t>(WaitingList::getTime(currentSlot));
			std::ostringstream ss;

			ss << "Too many players online.\nYou are at place "
			   << currentSlot << " on the waiting list.";

			auto output = OutputMessagePool::getOutputMessage();
			output->addByte(0x16);
			output->addString(ss.str());
			output->addByte(retryTime);
			send(output);
			disconnect();
			return;
		}

		if (!IOLoginData::loadPlayerById(player, player->getGUID(), false)) {
			disconnectClient("Your character could not be loaded, please contact an adminstrator.");
			return;
		}

		player->setOperatingSystem(operatingSystem);

		const auto tile = g_game().map.getOrCreateTile(player->getLoginPosition());
		// moving from a pz tile to a non-pz tile
		if (maxOnline > 1 && player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER && !tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
			auto maxOutsizePZ = g_configManager().getNumber(MAX_PLAYERS_OUTSIDE_PZ_PER_ACCOUNT);
			auto accountPlayers = g_game().getPlayersByAccount(player->getAccount());
			int countOutsizePZ = 0;
			for (const auto &accountPlayer : accountPlayers) {
				if (accountPlayer != player && accountPlayer->getTile() && !accountPlayer->getTile()->hasFlag(TILESTATE_PROTECTIONZONE)) {
					++countOutsizePZ;
				}
			}
			if (countOutsizePZ >= maxOutsizePZ) {
				disconnectClient(fmt::format("You can only have {} character{} from your account outside of a protection zone.", maxOutsizePZ == 1 ? "one" : std::to_string(maxOutsizePZ), maxOutsizePZ > 1 ? "s" : ""));
				return;
			}
		}

		if (!g_game().placeCreature(player, player->getLoginPosition()) && !g_game().placeCreature(player, player->getTemplePosition(), false, true)) {
			disconnectClient("Temple position is wrong. Please, contact the administrator.");
			g_logger().warn("Player {} temple position is wrong", player->getName());
			return;
		}

		player->lastIP = player->getIP();
		player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
		player->loginProtectionTime = OTSYS_TIME() + g_configManager().getNumber(LOGIN_PROTECTION_TIME);
		acceptPackets = true;
	} else {
		if (eventConnect != 0 || !g_configManager().getBoolean(REPLACE_KICK_ON_LOGIN)) {
			// Already trying to connect
			disconnectClient("You are already logged in.");
			return;
		}

		if (foundPlayer->client) {
			foundPlayer->disconnect();
			foundPlayer->isConnecting = true;

			eventConnect = g_dispatcher().scheduleEvent(
				1000,
				[self = getThis(), playerName = foundPlayer->getName(), operatingSystem] { self->connect(playerName, operatingSystem); }, "ProtocolGame::connect"
			);
		} else {
			connect(foundPlayer->getName(), operatingSystem);
		}
	}
	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
	sendBosstiaryCooldownTimer();
}

void ProtocolGame::connect(const std::string &playerName, OperatingSystem_t operatingSystem) {
	eventConnect = 0;

	std::shared_ptr<Player> foundPlayer = g_game().getPlayerByName(playerName);
	if (!foundPlayer) {
		disconnectClient("You are already logged in.");
		return;
	}

	if (isConnectionExpired()) {
		// ProtocolGame::release() has been called at this point and the Connection object
		// no longer exists, so we return to prevent leakage of the Player.
		return;
	}

	player = foundPlayer;

	g_chat().removeUserFromAllChannels(player);
	player->clearModalWindows();
	player->setOperatingSystem(operatingSystem);
	player->isConnecting = false;

	player->client = getThis();
	player->openPlayerContainers();
	sendAddCreature(player, player->getPosition(), 0, true);
	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	if (player->isProtected()) {
		player->setProtection(false);
		player->resetLoginProtection();
	} else {
		player->setLoginProtection(g_configManager().getNumber(LOGIN_PROTECTION_TIME));
	}
	player->resetIdleTime();
	acceptPackets = true;
}

void ProtocolGame::logout(bool displayEffect, bool forced) {
	if (!player) {
		return;
	}

	if (m_isLivestreamViewer) {
		sendSessionEndInformation(SESSION_END_LOGOUT);
		return;
	}

	bool removePlayer = !player->isRemoved() && !forced;
	auto tile = player->getTile();
	if (removePlayer && !player->isAccessPlayer()) {
		if (tile && tile->hasFlag(TILESTATE_NOLOGOUT)) {
			player->sendCancelMessage(RETURNVALUE_YOUCANNOTLOGOUTHERE);
			return;
		}

		if (tile && !tile->hasFlag(TILESTATE_PROTECTIONZONE) && player->hasCondition(CONDITION_INFIGHT)) {
			player->sendCancelMessage(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
			return;
		}
	}

	if (removePlayer && !g_creatureEvents().playerLogout(player)) {
		return;
	}

	displayEffect = displayEffect && !player->isRemoved() && player->getHealth() > 0 && !player->isInGhostMode();
	if (displayEffect) {
		g_game().addMagicEffect(player->getPosition(), CONST_ME_POFF);
	}

	sendSessionEndInformation(forced ? SESSION_END_FORCECLOSE : SESSION_END_LOGOUT);

	g_game().removeCreature(player, true);
}

void ProtocolGame::onRecvFirstMessage(NetworkMessage &msg) {
	if (g_game().getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	auto operatingSystem = static_cast<OperatingSystem_t>(msg.get<uint16_t>());
	version = msg.get<uint16_t>(); // Protocol version
	g_logger().trace("Protocol version: {}", version);

	// Old protocol support
	oldProtocol = g_configManager().getBoolean(OLD_PROTOCOL) && version <= 1100;

	if (oldProtocol) {
		setChecksumMethod(CHECKSUM_METHOD_ADLER32);
	} else if (operatingSystem <= CLIENTOS_OTCLIENT_MAC) {
		setChecksumMethod(CHECKSUM_METHOD_SEQUENCE);
	}

	clientVersion = static_cast<int32_t>(msg.get<uint32_t>());

	if (!oldProtocol) {
		auto clientVersionString = msg.getString(); // Client version (String)
		g_logger().trace("Client version: {}", clientVersionString);
		if (version >= 1334) {
			auto assetHashIdentifier = msg.getString(); // Assets hash identifier
			g_logger().trace("Client asset hash identifier: {}", assetHashIdentifier);
		}
	}

	if (version < 1334) {
		auto datRevision = msg.get<uint16_t>(); // Dat revision
		g_logger().trace("Dat revision: {}", datRevision);
	}

	auto gamePreviewState = msg.getByte(); // U8 game preview state
	g_logger().trace("Game preview state: {}", gamePreviewState);

	if (!Protocol::RSA_decrypt(msg)) {
		g_logger().warn("[ProtocolGame::onRecvFirstMessage] - RSA Decrypt Failed");
		disconnect();
		return;
	}

	std::array<uint32_t, 4> key = {
		msg.get<uint32_t>(),
		msg.get<uint32_t>(),
		msg.get<uint32_t>(),
		msg.get<uint32_t>()
	};

	enableXTEAEncryption();
	setXTEAKey(key.data());

	auto isGameMaster = static_cast<bool>(msg.getByte()); // gamemaster flag
	g_logger().trace("Is Game Master: {}", isGameMaster);

	std::string authType = g_configManager().getString(AUTH_TYPE);
	std::ostringstream ss;
	std::string sessionKey = msg.getString();
	std::string accountDescriptor = sessionKey;
	std::string password;

	if (authType != "session") {
		size_t pos = sessionKey.find('\n');
		if (pos == std::string::npos) {
			ss << "You must enter your " << (oldProtocol ? "username" : "email") << ".";
			disconnectClient(ss.str());
			return;
		}
		accountDescriptor = sessionKey.substr(0, pos);
		if (accountDescriptor.empty()) {
			ss.str(std::string());
			ss << "You must enter your " << (oldProtocol ? "username" : "email") << ".";
			disconnectClient(ss.str());
			return;
		}
		password = sessionKey.substr(pos + 1);
	}

	if (!oldProtocol && operatingSystem == CLIENTOS_NEW_LINUX) {
		// TODO: check what new info for linux is send
		msg.getString();
		msg.getString();
	}

	std::string characterName = msg.getString();

	const auto &onlinePlayer = g_game().getPlayerByName(characterName);
	const auto &foundPlayer = !onlinePlayer ? g_game().getDeadPlayer(characterName) : onlinePlayer;
	if (foundPlayer && foundPlayer->client && accountDescriptor != "@livestream") {
		if (foundPlayer->isDead()) {
			disconnectClient("You are already logged in.");
			return;
		}

		auto message = fmt::format("You are already connected through another client. Please use only one client at a time!");
		if (foundPlayer->getProtocolVersion() != getVersion() && foundPlayer->isOldProtocol() != oldProtocol) {
			message = fmt::format("You are already logged in using protocol '{}'. Please log out from the other session to connect here.", foundPlayer->getProtocolVersion());
		}

		foundPlayer->client->disconnectClient(message);
	}

	auto timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.getByte();
	if (challengeTimestamp != timeStamp || challengeRandom != randNumber) {
		disconnect();
		return;
	}

	// OTCv8 version detection
	auto otcV8StringLength = msg.get<uint16_t>();
	if (otcV8StringLength == 5 && msg.getString(5) == "OTCv8") {
		otclientV8 = msg.get<uint16_t>(); // 253, 260, 261, ...
	}

	if (!oldProtocol && clientVersion != CLIENT_VERSION) {
		ss.str(std::string());
		ss << "Only clients with protocol " << CLIENT_VERSION_UPPER << "." << CLIENT_VERSION_LOWER;
		if (g_configManager().getBoolean(OLD_PROTOCOL)) {
			ss << " or 11.00";
		}
		ss << " allowed!";
		disconnectClient(ss.str());
		return;
	}

	if (g_game().getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game().getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance. Please re-connect in a while.");
		return;
	}

	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		ss.str(std::string());
		ss << "Your IP has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy << ".\n\nReason specified:\n"
		   << banInfo.reason;
		disconnectClient(ss.str());
		return;
	}

	if (accountDescriptor == "@livestream") {
		g_dispatcher().addEvent([self = getThis(), characterName, password, operatingSystem] {
			self->castViewerLogin(characterName, password, operatingSystem);
		},
		                        "ProtocolGame::castViewerLogin");
		return;
	}

	uint32_t accountId;
	if (!IOLoginData::gameWorldAuthentication(accountDescriptor, password, characterName, accountId, oldProtocol, getIP())) {
		ss.str(std::string());
		if (authType == "session") {
			ss << "Your session has expired. Please log in again.";
		} else { // authType == "password"
			ss << "Your " << (oldProtocol ? "username" : "email") << " or password is not correct.";
		}

		auto output = OutputMessagePool::getOutputMessage();
		output->addByte(0x14);
		output->addString(ss.str());
		send(output);
		[[maybe_unused]] auto eventId = g_dispatcher().scheduleEvent(
			1000, [self = getThis()] { self->disconnect(); }, "ProtocolGame::disconnect"
		);
		return;
	}

	g_dispatcher().addEvent([self = getThis(), characterName, accountId, operatingSystem] { self->login(characterName, accountId, operatingSystem); }, __FUNCTION__);
}

void ProtocolGame::disconnectClient(const std::string &message) const {
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x14);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolGame::writeToOutputBuffer(NetworkMessage &msg) {
	auto writeMessage = [self = getThis(), msg]() mutable {
		self->getOutputBuffer(msg.getLength())->append(msg);
		if (self->m_isLivestreamBroadcaster && self->player && !self->m_isLivestreamViewer) {
			g_livestream().broadcastPacket(self->player, self, msg);
		}
	};

	if (g_dispatcher().context().isAsync()) {
		g_dispatcher().addEvent(std::move(writeMessage), __FUNCTION__);
	} else {
		writeMessage();
	}
}

void ProtocolGame::parsePacket(NetworkMessage &msg) {
	if (!acceptPackets || g_game().getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() <= 0) {
		return;
	}

	uint8_t recvbyte = msg.getByte();

	// Silence ping/pong: 0x1D = pingBack, 0x1E = ping [TRACKS CLIENT BYTES]
	if (recvbyte != 0x1D && recvbyte != 0x1E) {
		g_logger().debug("BYTE RECEIVED: 0x{:02X}", recvbyte);
	}

	if (!player || player->isRemoved()) {
		if (recvbyte == 0x0F) {
			disconnect();
		}
		return;
	}

	if (m_isLivestreamViewer) {
		parsePacketFromDispatcher(msg, recvbyte);
		return;
	}

	if (player->isDead() || player->getHealth() <= 0) {
		// Check player activity on death screen
		if (m_playerDeathTime == 0) {
			g_game().playerCheckActivity(player->getName(), 1000);
			m_playerDeathTime++;
		}

		parsePacketDead(recvbyte);
		return;
	}

	// Modules system
	if (player && recvbyte != 0xD3) {
		g_modules().executeOnRecvbyte(player->getID(), msg, recvbyte);
	}

	parsePacketFromDispatcher(msg, recvbyte);
}

void ProtocolGame::parsePacketDead(uint8_t recvbyte) {
	if (recvbyte == 0x14) {
		// Remove player from game if click "ok" using otc
		disconnect();
		return;
	}

	if (recvbyte == 0x0F) {
		if (!player) {
			return;
		}

		[[maybe_unused]] auto eventId = g_dispatcher().scheduleEvent(
			100, [self = getThis()] { self->sendPing(); }, "ProtocolGame::sendPing"
		);

		if (!player->spawn()) {
			disconnect();
			g_game().removeCreature(player, true);
			return;
		}

		sendAddCreature(player, player->getPosition(), 0, false);
		addBless();
		resetPlayerDeathTime();
		return;
	}

	if (recvbyte == 0x1D) {
		// keep the connection alive
		[[maybe_unused]] auto eventId = g_dispatcher().scheduleEvent(
			100, [self = getThis()] { self->sendPingBack(); }, "ProtocolGame::sendPingBack"
		);
		return;
	}
}

void ProtocolGame::addBless() {
	if (!player) {
		return;
	}
	player->checkAndShowBlessingMessage();
}

void ProtocolGame::parsePacketFromDispatcher(NetworkMessage &msg, uint8_t recvbyte) {
	if (!acceptPackets || g_game().getGameState() == GAME_STATE_SHUTDOWN) {
		return;
	}

	if (m_isLivestreamViewer) {
		switch (recvbyte) {
			case 0x14:
				logout(true, false);
				break;
			case 0x1D:
				break;
			case 0x1E:
				sendPingBack();
				break;
			case 0x96:
				parseSay(msg);
				break;
			case 0xCA:
				if (getUnreadBytes(msg) == UPDATE_CONTAINER_PAYLOAD_SIZE) {
					resendLivestreamViewerContainer(msg);
				}
				break;
			case 0xA1:
				sendCancelTarget();
				break;
			case 0xFA:
			case 0xFB:
			case 0xFC:
			case 0xFD:
			case 0xFE:
				break;
			default:
				sendCancelWalk();
				break;
		}
		return;
	}

	if (!player || player->isRemoved() || player->getHealth() <= 0) {
		return;
	}

	switch (recvbyte) {
		case 0x14:
			logout(true, false);
			break;
		case 0x1D:
			g_game().playerReceivePingBack(player->getID());
			break;
		case 0x1E:
			g_game().playerReceivePing(player->getID());
			break;
		case 0x28:
			parseStashWithdraw(msg);
			break;
		case 0x29:
			parseRetrieveDepotSearch(msg);
			break;
		case 0x2A:
			parseCyclopediaMonsterTracker(msg);
			break;
		case 0x2B:
			parsePartyAnalyzerAction(msg);
			break;
		case 0x2C:
			parseLeaderFinderWindow(msg);
			break;
		case 0x2D:
			parseMemberFinderWindow(msg);
			break;
		case 0x32:
			parseExtendedOpcode(msg);
			break; // otclient extended opcode
		case 0x38:
			parsePlayerTyping(msg); // player are typing or not
			break;
		case 0x60:
			parseInventoryImbuements(msg);
			break;
		case 0x61:
			parseOpenWheel(msg);
			break;
		case 0x62:
			parseSaveWheel(msg);
			break;
		case 0x64:
			parseAutoWalk(msg);
			break;
		case 0x65:
			g_game().playerMove(player->getID(), DIRECTION_NORTH);
			break;
		case 0x66:
			g_game().playerMove(player->getID(), DIRECTION_EAST);
			break;
		case 0x67:
			g_game().playerMove(player->getID(), DIRECTION_SOUTH);
			break;
		case 0x68:
			g_game().playerMove(player->getID(), DIRECTION_WEST);
			break;
		case 0x69:
			g_game().playerStopAutoWalk(player->getID());
			break;
		case 0x6A:
			g_game().playerMove(player->getID(), DIRECTION_NORTHEAST);
			break;
		case 0x6B:
			g_game().playerMove(player->getID(), DIRECTION_SOUTHEAST);
			break;
		case 0x6C:
			g_game().playerMove(player->getID(), DIRECTION_SOUTHWEST);
			break;
		case 0x6D:
			g_game().playerMove(player->getID(), DIRECTION_NORTHWEST);
			break;
		case 0x6F:
			g_game().playerTurn(player->getID(), DIRECTION_NORTH);
			break;
		case 0x70:
			g_game().playerTurn(player->getID(), DIRECTION_EAST);
			break;
		case 0x71:
			g_game().playerTurn(player->getID(), DIRECTION_SOUTH);
			break;
		case 0x72:
			g_game().playerTurn(player->getID(), DIRECTION_WEST);
			break;
		case 0x73:
			parseTeleport(msg);
			break;
		case 0x77:
			parseHotkeyEquip(msg);
			break;
		case 0x78:
			parseThrow(msg);
			break;
		case 0x79:
			parseLookInShop(msg);
			break;
		case 0x7A:
			parsePlayerBuyOnShop(msg);
			break;
		case 0x7B:
			parsePlayerSellOnShop(msg);
			break;
		case 0x7C:
			g_game().playerCloseShop(player->getID());
			break;
		case 0x7D:
			parseRequestTrade(msg);
			break;
		case 0x7E:
			parseLookInTrade(msg);
			break;
		case 0x7F:
			g_game().playerAcceptTrade(player->getID());
			break;
		case 0x80:
			g_game().playerCloseTrade(player->getID());
			break;
		case 0x81:
			parseFriendSystemAction(msg);
			break;
		case 0x82:
			parseUseItem(msg);
			break;
		case 0x83:
			parseUseItemEx(msg);
			break;
		case 0x84:
			parseUseWithCreature(msg);
			break;
		case 0x85:
			parseRotateItem(msg);
			break;
		case 0x86:
			parseConfigureShowOffSocket(msg);
			break;
		case 0x87:
			parseCloseContainer(msg);
			break;
		case 0x88:
			parseUpArrowContainer(msg);
			break;
		case 0x89:
			parseTextWindow(msg);
			break;
		case 0x8A:
			parseHouseWindow(msg);
			break;
		case 0x8B:
			parseWrapableItem(msg);
			break;
		case 0x8C:
			parseLookAt(msg);
			break;
		case 0x8D:
			parseLookInBattleList(msg);
			break;
		case 0x8E: /* join aggression */
			break;
		case 0x8F:
			parseQuickLoot(msg);
			break;
		case 0x90:
			parseLootContainer(msg);
			break;
		case 0x91:
			parseQuickLootBlackWhitelist(msg);
			break;
		case 0x92:
			parseOpenDepotSearch();
			break;
		case 0x93:
			parseCloseDepotSearch();
			break;
		case 0x94:
			parseDepotSearchItemRequest(msg);
			break;
		case 0x95:
			parseOpenParentContainer(msg);
			break;
		case 0x96:
			parseSay(msg);
			break;
		case 0x97:
			g_game().playerRequestChannels(player->getID());
			break;
		case 0x98:
			parseOpenChannel(msg);
			break;
		case 0x99:
			parseCloseChannel(msg);
			break;
		case 0x9A:
			parseOpenPrivateChannel(msg);
			break;
		case 0x9E:
			g_game().playerCloseNpcChannel(player->getID());
			break;
		case 0x9F:
			parseSetMonsterPodium(msg);
			break;
		case 0xA0:
			parseFightModes(msg);
			break;
		case 0xA1:
			parseAttack(msg);
			break;
		case 0xA2:
			parseFollow(msg);
			break;
		case 0xA3:
			parseInviteToParty(msg);
			break;
		case 0xA4:
			parseJoinParty(msg);
			break;
		case 0xA5:
			parseRevokePartyInvite(msg);
			break;
		case 0xA6:
			parsePassPartyLeadership(msg);
			break;
		case 0xA7:
			g_game().playerLeaveParty(player->getID());
			break;
		case 0xA8:
			parseEnableSharedPartyExperience(msg);
			break;
		case 0xAA:
			g_game().playerCreatePrivateChannel(player->getID());
			break;
		case 0xAB:
			parseChannelInvite(msg);
			break;
		case 0xAC:
			parseChannelExclude(msg);
			break;
		case 0xAD:
			parseCyclopediaHouseAuction(msg);
			break;
		case 0xAE:
			parseSendBosstiary();
			break;
		case 0xAF:
			parseSendBosstiarySlots();
			break;
		case 0xB0:
			parseBosstiarySlot(msg);
			break;
		case 0xB1:
			parseHighscores(msg);
			break;
		case 0xB2:
			parseImbuementAction(msg);
			break;
		case 0xB3:
			parseWeaponProficiency(msg);
			break;
		case 0xBA:
			parseTaskHuntingAction(msg);
			break;
		case 0xBE:
			g_game().playerCancelAttackAndFollow(player->getID());
			break;
		case 0xBF:
			parseForgeEnter(msg);
			break;
		case 0xC0:
			parseForgeBrowseHistory(msg);
			break;
		case 0xC8:
			parseAimAtTarget(msg);
			break;
		case 0xC9: /* update tile */
			break;
		case 0xCA:
			if (!oldProtocol && g_game().getWorldType() == WORLD_TYPE_NO_PVP) {
				parseExivaRestrictions(msg);
			}
			break;
		case 0xCB:
			parseBrowseField(msg);
			break;
		case 0xCC:
			parseSeekInContainer(msg);
			break;
		case 0xCD:
			parseInspectionObject(msg);
			break;
		case 0xCF:
			sendBlessingWindow();
			break;
		case 0xD2:
			g_game().playerRequestOutfit(player->getID());
			break;
		case 0xD3:
			parseSetOutfit(msg);
			break;
		case 0xD4:
			parseToggleMount(msg);
			break;
		case 0xD5:
			parseApplyImbuement(msg);
			break;
		case 0xD6:
			parseClearImbuement(msg);
			break;
		case 0xD7:
			parseCloseImbuementWindow(msg);
			break;
		case 0xDC:
			parseAddVip(msg);
			break;
		case 0xDD:
			parseRemoveVip(msg);
			break;
		case 0xDE:
			parseEditVip(msg);
			break;
		case 0xDF:
			parseVipGroupActions(msg);
			break;
		case 0xE1:
			parseBestiarySendRaces();
			break;
		case 0xE2:
			parseBestiarySendCreatures(msg);
			break;
		case 0xE3:
			parseBestiarysendMonsterData(msg);
			break;
		case 0xE4:
			parseSendBuyCharmRune(msg);
			break;
		case 0xE5:
			parseCyclopediaCharacterInfo(msg);
			break;
		case 0xE6:
			parseBugReport(msg);
			break;
		case 0xE7:
			parseWheelGemAction(msg);
			break;
		case 0xE8:
			parseOfferDescription(msg);
			break;
		case 0xEB:
			parsePreyAction(msg);
			break;
		case 0xED:
			parseSendResourceBalance();
			break;
		case 0xEE:
			parseGreet(msg);
			break;
		// Premium coins transfer
		// case 0xEF: parseCoinTransfer(msg); break;
		case 0xF0:
			g_game().playerShowQuestLog(player->getID());
			break;
		case 0xF1:
			parseQuestLine(msg);
			break;
		// case 0xF2: parseRuleViolationReport(msg); break;
		case 0xF3: /* get object info */
			break;
		case 0xF4:
			parseMarketLeave();
			break;
		case 0xF5:
			parseMarketBrowse(msg);
			break;
		case 0xF6:
			parseMarketCreateOffer(msg);
			break;
		case 0xF7:
			parseMarketCancelOffer(msg);
			break;
		case 0xF8:
			parseMarketAcceptOffer(msg);
			break;
		case 0xF9:
			parseModalWindowAnswer(msg);
			break;
		case 0xFF:
			parseRewardChestCollect(msg);
			break;
			// case 0xFA: parseStoreOpen(msg); break;
			// case 0xFB: parseStoreRequestOffers(msg); break;
			// case 0xFC: parseStoreBuyOffer(msg) break;
			// case 0xFD: parseStoreOpenTransactionHistory(msg); break;
			// case 0xFE: parseStoreRequestTransactionHistory(msg); break;

			// case 0xDF, 0xE0, 0xE1, 0xFB, 0xFC, 0xFD, 0xFE Premium Shop.

		default:
			std::string hexString = fmt::format("0x{:02x}", recvbyte);
			g_logger().debug("Player '{}' sent unknown packet header: hex[{}], decimal[{}]", player->getName(), asUpperCaseString(hexString), recvbyte);
			break;
	}
}

void ProtocolGame::parseHotkeyEquip(NetworkMessage &msg) {
	if (!player) {
		return;
	}

	auto itemId = msg.get<uint16_t>();
	uint8_t tier = 0;
	bool hasTier = Item::items[itemId].upgradeClassification > 0;
	if (hasTier) {
		tier = msg.get<uint8_t>();
	}
	g_game().playerEquipItem(player->getID(), itemId, hasTier, tier);
}

void ProtocolGame::checkCreatureAsKnown(uint32_t id, bool &known, uint32_t &removedKnown) {
	if (auto [creatureKnown, creatureInserted] = knownCreatureSet.insert(id);
	    !creatureInserted) {
		known = true;
		return;
	}
	known = false;
	if (knownCreatureSet.size() > 1300) {
		// Look for a creature to remove
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			if (*it == id) {
				continue;
			}
			// We need to protect party players from removing
			const auto &creature = g_game().getCreatureByID(*it);
			const auto &checkPlayer = creature ? creature->getPlayer() : nullptr;
			if (checkPlayer) {
				if (player->getParty() != checkPlayer->getParty() && !canSee(creature)) {
					removedKnown = *it;
					[[maybe_unused]] auto it_erase = knownCreatureSet.erase(it);
					return;
				}
			} else if (!canSee(creature)) {
				removedKnown = *it;
				[[maybe_unused]] auto it_erase = knownCreatureSet.erase(it);
				return;
			}
		}

		// Bad situation. Let's just remove anyone.
		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		removedKnown = *it;
		[[maybe_unused]] auto it_erase = knownCreatureSet.erase(it);
	} else {
		removedKnown = 0;
	}
}

bool ProtocolGame::canSee(const std::shared_ptr<Creature> &c) const {
	if (!c || !player || c->isRemoved()) {
		return false;
	}

	if (!player->canSeeCreature(c)) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolGame::canSee(const Position &pos) const {
	return canSee(pos.x, pos.y, pos.z);
}

bool ProtocolGame::canSee(int32_t x, int32_t y, int32_t z) const {
	if (!player) {
		return false;
	}

	const Position &myPos = player->getPosition();
	if (myPos.z <= MAP_INIT_SURFACE_LAYER) {
		// we are on ground level or above (7 -> 0)
		// view is from 7 -> 0
		if (z > MAP_INIT_SURFACE_LAYER) {
			return false;
		}
	} else if (myPos.z >= MAP_INIT_SURFACE_LAYER + 1) {
		// we are underground (8 -> 15)
		// view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > MAP_LAYER_VIEW_LIMIT) {
			return false;
		}
	}

	// negative offset means that the action taken place is on a lower floor than ourself
	const int8_t offsetz = myPos.getZ() - z;
	return (x >= myPos.getX() - MAP_MAX_CLIENT_VIEW_PORT_X + offsetz) && (x <= myPos.getX() + (MAP_MAX_CLIENT_VIEW_PORT_X + 1) + offsetz) && (y >= myPos.getY() - MAP_MAX_CLIENT_VIEW_PORT_Y + offsetz) && (y <= myPos.getY() + (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) + offsetz);
}

// Parse methods
void ProtocolGame::parseChannelInvite(NetworkMessage &msg) {
	const std::string name = msg.getString();
	g_game().playerChannelInvite(player->getID(), name);
}

void ProtocolGame::parseChannelExclude(NetworkMessage &msg) {
	const std::string name = msg.getString();
	g_game().playerChannelExclude(player->getID(), name);
}

void ProtocolGame::parseOpenChannel(NetworkMessage &msg) {
	auto channelId = msg.get<uint16_t>();
	g_game().playerOpenChannel(player->getID(), channelId);
}

void ProtocolGame::parseCloseChannel(NetworkMessage &msg) {
	auto channelId = msg.get<uint16_t>();
	g_game().playerCloseChannel(player->getID(), channelId);
}

void ProtocolGame::parseOpenPrivateChannel(NetworkMessage &msg) {
	std::string receiver = msg.getString();
	g_game().playerOpenPrivateChannel(player->getID(), receiver);
}

void ProtocolGame::parseAutoWalk(NetworkMessage &msg) {
	uint8_t numdirs = msg.getByte();
	if (numdirs == 0 || (msg.getBufferPosition() + numdirs) != (msg.getLength() + 6)) {
		return;
	}

	std::vector<Direction> path;
	path.resize(numdirs, DIRECTION_NORTH);
	for (size_t i = numdirs; --i < numdirs;) {
		const uint8_t rawdir = msg.getByte();
		switch (rawdir) {
			case 1:
				path[i] = DIRECTION_EAST;
				break;
			case 2:
				path[i] = DIRECTION_NORTHEAST;
				break;
			case 3:
				path[i] = DIRECTION_NORTH;
				break;
			case 4:
				path[i] = DIRECTION_NORTHWEST;
				break;
			case 5:
				path[i] = DIRECTION_WEST;
				break;
			case 6:
				path[i] = DIRECTION_SOUTHWEST;
				break;
			case 7:
				path[i] = DIRECTION_SOUTH;
				break;
			case 8:
				path[i] = DIRECTION_SOUTHEAST;
				break;
			default:
				break;
		}
	}

	if (path.empty()) {
		return;
	}

	MoveCommand cmd;
	cmd.path = std::move(path);
	GameCommandDispatcher { player->getID() }.dispatchMove(std::move(cmd));
}

void ProtocolGame::parseSetOutfit(NetworkMessage &msg) {
	if (!player || player->isRemoved()) {
		return;
	}

	uint16_t startBufferPosition = msg.getBufferPosition();
	const auto &outfitModule = g_modules().getEventByRecvbyte(0xD3, false);
	if (outfitModule) {
		outfitModule->executeOnRecvbyte(player, msg);
	}

	if (msg.getBufferPosition() == startBufferPosition) {
		uint8_t outfitType = !oldProtocol ? msg.getByte() : 0;
		Outfit_t newOutfit;
		newOutfit.lookType = msg.get<uint16_t>();
		newOutfit.lookHead = std::min<uint8_t>(132, msg.getByte());
		newOutfit.lookBody = std::min<uint8_t>(132, msg.getByte());
		newOutfit.lookLegs = std::min<uint8_t>(132, msg.getByte());
		newOutfit.lookFeet = std::min<uint8_t>(132, msg.getByte());
		newOutfit.lookAddons = msg.getByte();
		if (outfitType == 0) {
			newOutfit.lookMount = msg.get<uint16_t>();
			bool setMount = false;
			if (!oldProtocol) {
				newOutfit.lookMountHead = std::min<uint8_t>(132, msg.getByte());
				newOutfit.lookMountBody = std::min<uint8_t>(132, msg.getByte());
				newOutfit.lookMountLegs = std::min<uint8_t>(132, msg.getByte());
				newOutfit.lookMountFeet = std::min<uint8_t>(132, msg.getByte());
				setMount = msg.getByte();
				newOutfit.lookFamiliarsType = msg.get<uint16_t>();
				g_logger().debug("Bool isMounted: {}", setMount);
			}

			uint8_t isMountRandomized = !oldProtocol ? msg.getByte() : 0;
			// g_game.enableFeature(GameWingsAurasEffectsShader)
			newOutfit.lookWing = isOTCR ? msg.get<uint16_t>() : 0;
			newOutfit.lookAura = isOTCR ? msg.get<uint16_t>() : 0;
			newOutfit.lookEffect = isOTCR ? msg.get<uint16_t>() : 0;
			std::string shaderName = isOTCR ? msg.getString() : "";
			if (!shaderName.empty()) {
				const auto &shader = g_game().getAttachedEffects()->getShaderByName(shaderName);
				newOutfit.lookShader = shader ? shader->id : 0;
			}
			g_game().playerChangeOutfit(player->getID(), newOutfit, setMount, isMountRandomized);
		} else if (outfitType == 1) {
			// This value probably has something to do with try outfit variable inside outfit window dialog
			// if try outfit is set to 2 it expects uint32_t value after mounted and disable mounts from outfit window dialog
			newOutfit.lookMount = 0;
			msg.get<uint32_t>();
		} else if (outfitType == 2) {
			Position pos = msg.getPosition();
			auto itemId = msg.get<uint16_t>();
			uint8_t stackpos = msg.getByte();
			newOutfit.lookMount = msg.get<uint16_t>();
			newOutfit.lookMountHead = std::min<uint8_t>(132, msg.getByte());
			newOutfit.lookMountBody = std::min<uint8_t>(132, msg.getByte());
			newOutfit.lookMountLegs = std::min<uint8_t>(132, msg.getByte());
			newOutfit.lookMountFeet = std::min<uint8_t>(132, msg.getByte());
			uint8_t direction = std::max<uint8_t>(DIRECTION_NORTH, std::min<uint8_t>(DIRECTION_WEST, msg.getByte()));
			uint8_t podiumVisible = msg.getByte();
			g_game().playerSetShowOffSocket(player->getID(), newOutfit, pos, stackpos, itemId, podiumVisible, direction);
		}
	}
}

void ProtocolGame::parseToggleMount(NetworkMessage &msg) {
	bool mount = msg.getByte(true) != 0;
	g_game().playerToggleMount(player->getID(), mount);
}

void ProtocolGame::parseApplyImbuement(NetworkMessage &msg) {
	uint8_t slot = msg.getByte();
	auto imbuementId = msg.get<uint16_t>();
	g_game().playerApplyImbuement(player->getID(), imbuementId, slot);
}

void ProtocolGame::parseClearImbuement(NetworkMessage &msg) {
	uint8_t slot = msg.getByte();
	g_game().playerClearImbuement(player->getID(), slot);
}

void ProtocolGame::parseCloseImbuementWindow(NetworkMessage &) {
	g_game().playerCloseImbuementWindow(player->getID());
}

void ProtocolGame::parseUseItem(NetworkMessage &msg) {
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint8_t index = msg.getByte();
	g_game().playerUseItem(player->getID(), pos, stackpos, index, itemId);
}

void ProtocolGame::parseUseItemEx(NetworkMessage &msg) {
	Position fromPos = msg.getPosition();
	auto fromItemId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	Position toPos = msg.getPosition();
	auto toItemId = msg.get<uint16_t>();
	uint8_t toStackPos = msg.getByte();
	g_game().playerUseItemEx(player->getID(), fromPos, fromStackPos, fromItemId, toPos, toStackPos, toItemId);
}

void ProtocolGame::parseUseWithCreature(NetworkMessage &msg) {
	Position fromPos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	auto creatureId = msg.get<uint32_t>();
	g_game().playerUseWithCreature(player->getID(), fromPos, fromStackPos, creatureId, itemId);
}

void ProtocolGame::parseCloseContainer(NetworkMessage &msg) {
	uint8_t cid = msg.getByte();
	g_game().playerCloseContainer(player->getID(), cid);
}

void ProtocolGame::parseUpArrowContainer(NetworkMessage &msg) {
	uint8_t cid = msg.getByte();
	g_game().playerMoveUpContainer(player->getID(), cid);
}

void ProtocolGame::parseUpdateContainer(NetworkMessage &msg) {
	uint8_t cid = msg.getByte();
	g_game().playerUpdateContainer(player->getID(), cid);
}

void ProtocolGame::parseTeleport(NetworkMessage &msg) {
	Position newPosition = msg.getPosition();
	g_game().playerTeleport(player->getID(), newPosition);
}

void ProtocolGame::parseThrow(NetworkMessage &msg) {
	Position fromPos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t fromStackpos = msg.getByte();
	Position toPos = msg.getPosition();
	uint8_t count = msg.getByte();

	if (toPos != fromPos) {
		g_game().playerMoveThing(player->getID(), fromPos, itemId, fromStackpos, toPos, count);
	}
}

void ProtocolGame::parseLookAt(NetworkMessage &msg) {
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	g_game().playerLookAt(player->getID(), itemId, pos, stackpos);
}

void ProtocolGame::parseLookInBattleList(NetworkMessage &msg) {
	auto creatureId = msg.get<uint32_t>();
	g_game().playerLookInBattleList(player->getID(), creatureId);
}

void ProtocolGame::parseQuickLoot(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t variant = msg.getByte();

	if (variant == 2) {
		g_game().playerLootNearby(player->getID());
		return;
	}

	if (variant > 2) {
		g_logger().debug("[{}] unsupported quick-loot variant {} from player {}", __FUNCTION__, variant, player ? player->getName() : "<null>");
		return;
	}

	const Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	bool lootAllCorpses = variant == 1;

	g_logger().debug("[{}] variant {}, pos {}, itemId {}, stackPos {}", __FUNCTION__, variant, pos.toString(), itemId, stackpos);
	g_game().playerQuickLoot(player->getID(), pos, itemId, stackpos, nullptr, lootAllCorpses, false);
}

void ProtocolGame::parseLootContainer(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t action = msg.getByte();
	if (action == 0) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		Position pos = msg.getPosition();
		auto itemId = msg.get<uint16_t>();
		uint8_t stackpos = msg.getByte();
		g_game().playerSetManagedContainer(player->getID(), category, pos, itemId, stackpos, true);
	} else if (action == 1) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		g_game().playerClearManagedContainer(player->getID(), category, true);
	} else if (action == 2) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		g_game().playerOpenManagedContainer(player->getID(), category, true);
	} else if (action == 3) {
		bool useMainAsFallback = msg.getByte() == 1;
		g_game().playerSetQuickLootFallback(player->getID(), useMainAsFallback);
	} else if (action == 4) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		Position pos = msg.getPosition();
		auto itemId = msg.get<uint16_t>();
		uint8_t stackpos = msg.getByte();
		g_logger().debug("[{}] action {}, category {}, pos {}, itemId {}, stackPos {}", __FUNCTION__, action, static_cast<uint8_t>(category), pos.toString(), itemId, stackpos);
		g_game().playerSetManagedContainer(player->getID(), category, pos, itemId, stackpos, false);
	} else if (action == 5) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		g_game().playerClearManagedContainer(player->getID(), category, false);
	} else if (action == 6) {
		auto category = static_cast<ObjectCategory_t>(msg.getByte());
		g_game().playerOpenManagedContainer(player->getID(), category, false);
	}

	g_logger().debug("[{}] action type {}", __FUNCTION__, action);
}

void ProtocolGame::parseQuickLootBlackWhitelist(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto filter = (QuickLootFilter_t)msg.getByte();
	std::vector<uint16_t> listedItems;

	auto size = msg.get<uint16_t>();
	listedItems.reserve(size);

	for (int i = 0; i < size; i++) {
		listedItems.push_back(msg.get<uint16_t>());
	}

	g_game().playerQuickLootBlackWhitelist(player->getID(), filter, listedItems);
}

void ProtocolGame::parseSay(NetworkMessage &msg) {
	std::string receiver;
	uint16_t channelId {};

	auto type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		case TALKTYPE_PRIVATE_TO:
		case TALKTYPE_PRIVATE_RED_TO:
			receiver = msg.getString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
			channelId = msg.get<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	const std::string text = msg.getString();
	if (text.length() > 255) {
		return;
	}

	if (channelId == CHANNEL_LIVESTREAM) {
		g_livestream().handleChat(getThis(), text);
		return;
	}

	if (m_isLivestreamViewer) {
		sendTextMessage(TextMessage(MESSAGE_LOOK, "You only can talk in the Livestream channel."));
		return;
	}

	g_game().playerSay(player->getID(), channelId, type, receiver, text);
}

void ProtocolGame::parseFightModes(NetworkMessage &msg) {
	uint8_t rawFightMode = msg.getByte(); // 1 - offensive, 2 - balanced, 3 - defensive
	uint8_t rawChaseMode = msg.getByte(); // 0 - stand while fightning, 1 - chase opponent
	uint8_t rawSecureMode = msg.getByte(); // 0 - can't attack unmarked, 1 - can attack unmarked
	// uint8_t rawPvpMode = msg.getByte(); // pvp mode introduced in 10.0

	FightMode_t fightMode;
	if (rawFightMode == 1) {
		fightMode = FIGHTMODE_ATTACK;
	} else if (rawFightMode == 2) {
		fightMode = FIGHTMODE_BALANCED;
	} else {
		fightMode = FIGHTMODE_DEFENSE;
	}

	g_game().playerSetFightModes(player->getID(), fightMode, rawChaseMode != 0, rawSecureMode != 0);
}

void ProtocolGame::parseAttack(NetworkMessage &msg) {
	AttackCommand cmd;
	cmd.creatureId = msg.get<uint32_t>();
	GameCommandDispatcher { player->getID() }.dispatchAttack(std::move(cmd));
}

void ProtocolGame::parseFollow(NetworkMessage &msg) {
	FollowCommand cmd;
	cmd.creatureId = msg.get<uint32_t>();
	GameCommandDispatcher { player->getID() }.dispatchFollow(std::move(cmd));
}

void ProtocolGame::parseTextWindow(NetworkMessage &msg) {
	auto windowTextId = msg.get<uint32_t>();
	const std::string newText = msg.getString();
	g_game().playerWriteItem(player->getID(), windowTextId, newText);
}

void ProtocolGame::parseHouseWindow(NetworkMessage &msg) {
	uint8_t doorId = msg.getByte();
	auto id = msg.get<uint32_t>();
	const std::string text = msg.getString();
	g_game().playerUpdateHouseWindow(player->getID(), doorId, id, text);
}

void ProtocolGame::parseLookInShop(NetworkMessage &msg) {
	auto id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	g_game().playerLookInShop(player->getID(), id, count);
}

void ProtocolGame::parsePlayerBuyOnShop(NetworkMessage &msg) {
	auto id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint16_t amount = oldProtocol ? static_cast<uint16_t>(msg.getByte()) : msg.get<uint16_t>();
	bool ignoreCap = msg.getByte(true) != 0;
	bool inBackpacks = msg.getByte(true) != 0;
	g_game().playerBuyItem(player->getID(), id, count, amount, ignoreCap, inBackpacks);
}

void ProtocolGame::parsePlayerSellOnShop(NetworkMessage &msg) {
	auto id = msg.get<uint16_t>();
	uint8_t count = std::max(msg.getByte(), (uint8_t)1);
	uint16_t amount = oldProtocol ? static_cast<uint16_t>(msg.getByte()) : msg.get<uint16_t>();
	bool ignoreEquipped = msg.getByte(true) != 0;

	g_game().playerSellItem(player->getID(), id, count, amount, ignoreEquipped);
}

void ProtocolGame::parseRequestTrade(NetworkMessage &msg) {
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	auto playerId = msg.get<uint32_t>();
	g_game().playerRequestTrade(player->getID(), pos, stackpos, playerId, itemId);
}

void ProtocolGame::parseLookInTrade(NetworkMessage &msg) {
	bool counterOffer = (msg.getByte() == 0x01);
	uint8_t index = msg.getByte();
	g_game().playerLookInTrade(player->getID(), counterOffer, index);
}

void ProtocolGame::parseAddVip(NetworkMessage &msg) {
	const std::string name = msg.getString();
	g_game().playerRequestAddVip(player->getID(), name);
}

void ProtocolGame::parseRemoveVip(NetworkMessage &msg) {
	auto guid = msg.get<uint32_t>();
	g_game().playerRequestRemoveVip(player->getID(), guid);
}

void ProtocolGame::parseEditVip(NetworkMessage &msg) {
	std::vector<uint8_t> vipGroupsId;
	auto guid = msg.get<uint32_t>();
	const std::string description = msg.getString();
	uint32_t icon = std::min<uint32_t>(10, msg.get<uint32_t>()); // 10 is max icon in 9.63
	bool notify = msg.getByte(true) != 0;
	uint8_t groupsAmount = msg.getByte();
	for (uint8_t i = 0; i < groupsAmount; ++i) {
		uint8_t groupId = msg.getByte();
		vipGroupsId.emplace_back(groupId);
	}
	g_game().playerRequestEditVip(player->getID(), guid, description, icon, notify, vipGroupsId);
}

void ProtocolGame::parseVipGroupActions(NetworkMessage &msg) {
	uint8_t action = msg.getByte();

	switch (action) {
		case 0x01: {
			const std::string groupName = msg.getString();
			player->vip().addGroup(groupName);
			break;
		}
		case 0x02: {
			const uint8_t groupId = msg.getByte();
			const std::string newGroupName = msg.getString();
			player->vip().editGroup(groupId, newGroupName);
			break;
		}
		case 0x03: {
			const uint8_t groupId = msg.getByte();
			player->vip().removeGroup(groupId);
			break;
		}
		default: {
			break;
		}
	}
}

void ProtocolGame::parseRotateItem(NetworkMessage &msg) {
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	const auto &itemType = Item::items[itemId];
	if (itemType.isPodium) {
		g_game().playerRotatePodium(player->getID(), pos, stackpos, itemId);
	} else {
		g_game().playerRotateItem(player->getID(), pos, stackpos, itemId);
	}
}

void ProtocolGame::parseWrapableItem(NetworkMessage &msg) {
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	g_game().playerWrapableItem(player->getID(), pos, stackpos, itemId);
}

void ProtocolGame::parseInspectionObject(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t inspectionType = msg.getByte();
	if (inspectionType == INSPECT_NORMALOBJECT) {
		Position pos = msg.getPosition();
		g_game().playerInspectItem(player, pos);
	} else if (inspectionType == INSPECT_NPCTRADE || inspectionType == INSPECT_CYCLOPEDIA) {
		auto itemId = msg.get<uint16_t>();
		uint16_t itemCount = msg.getByte();
		g_game().playerInspectItem(player, itemId, static_cast<int8_t>(itemCount), (inspectionType == INSPECT_CYCLOPEDIA));
	} else if (inspectionType == INSPECT_PROFICIENCY) {
		const auto itemId = msg.get<uint16_t>();
		msg.getByte(); // Unknown byte
		sendWeaponProficiencyWindow(itemId);
	}
}

void ProtocolGame::parseFriendSystemAction(NetworkMessage &msg) {
	uint8_t state = msg.getByte();
	if (state == 0x0E) {
		uint8_t titleId = msg.getByte();
		g_game().playerFriendSystemAction(player, state, titleId);
	}
}

void ProtocolGame::parseCyclopediaCharacterInfo(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint32_t characterID;
	CyclopediaCharacterInfoType_t characterInfoType;
	characterID = msg.get<uint32_t>();
	characterInfoType = static_cast<CyclopediaCharacterInfoType_t>(msg.getByte());
	uint16_t entriesPerPage = 0, page = 0;
	if (characterInfoType == CYCLOPEDIA_CHARACTERINFO_RECENTDEATHS || characterInfoType == CYCLOPEDIA_CHARACTERINFO_RECENTPVPKILLS) {
		entriesPerPage = std::min<uint16_t>(30, std::max<uint16_t>(5, msg.get<uint16_t>()));
		page = std::max<uint16_t>(1, msg.get<uint16_t>());
	}
	if (characterID == 0) {
		characterID = player->getID();
	}
	g_game().playerCyclopediaCharacterInfo(player, characterID, characterInfoType, entriesPerPage, page);
}

void ProtocolGame::parseHighscores(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto type = static_cast<HighscoreType_t>(msg.getByte());
	uint8_t category = msg.getByte();
	auto vocation = msg.get<uint32_t>();
	uint16_t page = 1;
	const std::string worldName = msg.getString();
	msg.getByte(); // Game World Category
	msg.getByte(); // BattlEye World Type
	if (type == HIGHSCORE_GETENTRIES) {
		page = std::max<uint16_t>(1, msg.get<uint16_t>());
	}
	uint8_t entriesPerPage = std::min<uint8_t>(30, std::max<uint8_t>(5, msg.getByte()));
	g_game().playerHighscores(player, type, category, vocation, worldName, page, entriesPerPage);
}

void ProtocolGame::parseImbuementAction(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto action = static_cast<ImbuementAction>(msg.getByte()); // 0x01 - pick item, 0x02 - scroll imbuement

	std::shared_ptr<Item> item = nullptr;

	if (action == ImbuementAction::PickItem) {
		msg.skipBytes(2); // Unknown bytes
		auto slotId = msg.getByte();
		msg.skipBytes(1); // Unknown byte
		msg.skipBytes(1); // Unknown byte
		auto itemId = msg.get<uint16_t>();
		auto slotIndex = msg.getByte();

		const bool isContainerItem = slotId >= 0x40;
		if (isContainerItem) {
			// Padding to not conflict with inventory slot
			// the client sends with this padding
			slotId -= 0x40;
			const auto &container = player->getContainerByID(slotId);
			item = container ? container->getItemByIndex(slotIndex) : nullptr;
		} else {
			item = player->getInventoryItem(static_cast<Slots_t>(slotId));
		}

		if (!item || item->getID() != itemId) {
			return;
		}

		if (item->getImbuementSlot() <= 0) {
			if (isContainerItem) {
				player->sendImbuementResult("This item is not imbuable.");
			}
			return;
		}
	}

	openImbuementWindow(action, item);
}

void ProtocolGame::parseWeaponProficiency(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto action = msg.getByte();

	if (action == 0x01) {
		for (const auto weaponId : player->weaponProficiency().getTrackedWeaponIds()) {
			sendWeaponProficiencyWindow(weaponId);
		}
		return;
	}

	auto weaponId = msg.get<uint16_t>();
	const auto equippedWeaponId = player->getWeaponId(true);
	const bool isEquippedWeapon = equippedWeaponId != 0 && weaponId == equippedWeaponId;

	if (action == 0x03) {
		if (isEquippedWeapon) {
			player->weaponProficiency().clearAllStats();
		}
		player->weaponProficiency().clearSelectedPerks(weaponId);

		auto slots = msg.getByte();
		for (uint8_t slot = 0; slot < slots; slot++) {
			auto level = msg.getByte();
			auto perkIndex = msg.getByte();

			player->weaponProficiency().setSelectedPerk(level, perkIndex, weaponId);
		}

		if (isEquippedWeapon) {
			player->weaponProficiency().applyPerks(weaponId);
		}
	} else if (action == 0x02) {
		if (isEquippedWeapon) {
			player->weaponProficiency().clearAllStats();
			player->sendSkills();
		}
		player->weaponProficiency().clearSelectedPerks(weaponId);
	}

	sendWeaponProficiencyWindow(weaponId);
	sendWeaponProficiency(weaponId);
}

void ProtocolGame::parseTaskHuntingAction(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t slot = msg.getByte();
	uint8_t action = msg.getByte();
	bool upgrade = msg.getByte(true) != 0;
	auto raceId = msg.get<uint16_t>();

	if (!g_configManager().getBoolean(TASK_HUNTING_ENABLED)) {
		return;
	}

	g_game().playerTaskHuntingAction(player->getID(), slot, action, upgrade, raceId);
}

void ProtocolGame::parseConfigureShowOffSocket(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	g_game().playerConfigureShowOffSocket(player->getID(), pos, stackpos, itemId);
}

void ProtocolGame::parseRuleViolationReport(NetworkMessage &msg) {
	uint8_t reportType = msg.getByte();
	uint8_t reportReason = msg.getByte();
	const std::string &targetName = msg.getString();
	const std::string &comment = msg.getString();
	std::string translation;
	if (reportType == REPORT_TYPE_NAME) {
		translation = msg.getString();
	} else if (reportType == REPORT_TYPE_STATEMENT) {
		translation = msg.getString();
		msg.get<uint32_t>(); // statement id, used to get whatever player have said, we don't log that.
	}

	g_game().playerReportRuleViolationReport(player->getID(), targetName, reportType, reportReason, comment, translation);
}

void ProtocolGame::parseBestiarySendRaces() {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xD5);
	msg.add<uint16_t>(BESTY_RACE_LAST);
	const std::map<uint16_t, std::string> &mtype_list = g_game().getBestiaryList();
	for (uint8_t i = BESTY_RACE_FIRST; i <= BESTY_RACE_LAST; i++) {
		std::string BestClass;
		uint16_t count = 0;
		for (const auto &rit : mtype_list) {
			const auto mtype = g_monsters().getMonsterType(rit.second);
			if (!mtype) {
				return;
			}
			if (mtype->info.bestiaryRace == static_cast<BestiaryType_t>(i)) {
				count += 1;
				BestClass = mtype->info.bestiaryClass;
			}
		}
		msg.addString(BestClass);
		msg.add<uint16_t>(count);
		uint16_t unlockedCount = g_iobestiary().getBestiaryRaceUnlocked(player, static_cast<BestiaryType_t>(i));
		msg.add<uint16_t>(unlockedCount);
	}
	writeToOutputBuffer(msg);

	player->sendBestiaryCharms();
}

void ProtocolGame::parseBestiarysendMonsterData(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto raceId = msg.get<uint16_t>();
	std::string Class;
	std::shared_ptr<MonsterType> mtype = nullptr;
	const std::map<uint16_t, std::string> &mtype_list = g_game().getBestiaryList();

	auto ait = mtype_list.find(raceId);
	if (ait != mtype_list.end()) {
		auto mType = g_monsters().getMonsterType(ait->second);
		if (mType) {
			Class = mType->info.bestiaryClass;
			mtype = mType;
		}
	}

	if (!mtype) {
		g_logger().warn("[ProtocolGame::parseBestiarysendMonsterData] - "
		                "MonsterType was not found");
		return;
	}

	uint32_t killCounter = player->getBestiaryKillCount(raceId);
	uint8_t currentLevel = g_iobestiary().getKillStatus(mtype, killCounter);

	NetworkMessage newmsg;
	newmsg.addByte(0xD7);
	newmsg.add<uint16_t>(raceId);
	newmsg.addString(Class);

	newmsg.addByte(currentLevel);

	if (player->animusMastery().has(mtype->name)) {
		newmsg.add<uint16_t>(static_cast<uint16_t>(std::round((player->animusMastery().getExperienceMultiplier() - 1) * 1000))); // Animus Mastery Bonus
		newmsg.add<uint16_t>(player->animusMastery().getPoints()); // Animus Mastery Points
	} else {
		newmsg.add<uint16_t>(0);
		newmsg.add<uint16_t>(0);
	}
	newmsg.add<uint32_t>(killCounter);

	newmsg.add<uint16_t>(mtype->info.bestiaryFirstUnlock);
	newmsg.add<uint16_t>(mtype->info.bestiarySecondUnlock);
	newmsg.add<uint16_t>(mtype->info.bestiaryToUnlock);

	newmsg.addByte(mtype->info.bestiaryStars);
	newmsg.addByte(mtype->info.bestiaryOccurrence);

	const std::vector<LootBlock> &lootList = mtype->info.lootItems;
	newmsg.addByte(lootList.size());
	for (const LootBlock &loot : lootList) {
		int8_t difficult = g_iobestiary().calculateDifficult(loot.chance);
		bool shouldAddItem = false;

		switch (currentLevel) {
			case 1:
				shouldAddItem = false;
				break;
			case 2:
				if (difficult < 2) {
					shouldAddItem = true;
				}
				break;
			case 3:
				if (difficult < 3) {
					shouldAddItem = true;
				}
				break;
			case 4:
				shouldAddItem = true;
				break;
		}

		newmsg.add<uint16_t>(g_configManager().getBoolean(SHOW_LOOTS_IN_BESTIARY) || shouldAddItem == true ? loot.id : 0);
		newmsg.addByte(difficult);
		newmsg.addByte(0); // 1 if special event - 0 if regular loot (?)
		if (g_configManager().getBoolean(SHOW_LOOTS_IN_BESTIARY) || shouldAddItem == true) {
			newmsg.addString(loot.name);
			newmsg.addByte(loot.countmax > 0 ? 0x1 : 0x0);
		}
	}

	if (currentLevel > 1) {
		newmsg.add<uint16_t>(mtype->info.bestiaryCharmsPoints);
		int8_t attackmode = 0;
		if (!mtype->info.isHostile) {
			attackmode = 2;
		} else if (mtype->info.targetDistance) {
			attackmode = 1;
		}

		newmsg.addByte(attackmode);
		newmsg.addByte(0x02);
		newmsg.add<uint32_t>(mtype->info.healthMax);
		newmsg.add<uint32_t>(mtype->info.experience);
		newmsg.add<uint16_t>(mtype->getBaseSpeed());
		newmsg.add<uint16_t>(mtype->info.armor);
		newmsg.addDouble(mtype->info.mitigation);
	}

	if (currentLevel > 2) {
		std::map<uint8_t, int16_t> elements = g_iobestiary().getMonsterElements(mtype);

		newmsg.addByte(elements.size());
		for (auto &element : elements) {
			newmsg.addByte(element.first);
			newmsg.add<uint16_t>(element.second);
		}

		newmsg.add<uint16_t>(1);
		newmsg.addString(mtype->info.bestiaryLocations);
	}

	writeToOutputBuffer(newmsg);
}

void ProtocolGame::parseCyclopediaMonsterTracker(NetworkMessage &msg) {
	auto monsterRaceId = msg.get<uint16_t>();
	// Bosstiary tracker: 0 = disabled, 1 = enabled
	// Bestiary tracker: 1 = enabled
	auto trackerButtonType = msg.getByte();

	// Bosstiary tracker logic
	if (const auto monsterType = g_ioBosstiary().getMonsterTypeByBossRaceId(monsterRaceId)) {
		if (player->getBestiaryKillCount(monsterRaceId)) {
			if (trackerButtonType == 1) {
				player->addMonsterToCyclopediaTrackerList(monsterType, true, true);
			} else {
				player->removeMonsterFromCyclopediaTrackerList(monsterType, true, true);
			}
		}
		return;
	}

	// Bestiary tracker logic
	const auto &bestiaryMonsters = g_game().getBestiaryList();
	auto it = bestiaryMonsters.find(monsterRaceId);
	if (it != bestiaryMonsters.end()) {
		const auto mtype = g_monsters().getMonsterType(it->second);
		if (!mtype) {
			g_logger().error("[{}] player {} have wrong boss with race {}", __FUNCTION__, player->getName(), monsterRaceId);
			return;
		}

		if (trackerButtonType == 1) {
			player->addMonsterToCyclopediaTrackerList(mtype, false, true);
		} else {
			player->removeMonsterFromCyclopediaTrackerList(mtype, false, true);
		}
	}
}

void ProtocolGame::parsePlayerTyping(NetworkMessage &msg) {
	uint8_t typing = msg.getByte();
	g_dispatcher().addEvent([self = getThis(), playerID = player->getID(), typing] { g_game().playerSetTyping(playerID, typing); }, __FUNCTION__);
}

void ProtocolGame::createLeaderTeamFinder(NetworkMessage &msg) {
	if (!player || oldProtocol) {
		return;
	}

	const auto &teamAssemble = g_game().getOrCreateTeamFinder(player);
	teamAssemble->minLevel = msg.get<uint16_t>();
	teamAssemble->maxLevel = msg.get<uint16_t>();
	teamAssemble->vocationIDs = msg.getByte();
	teamAssemble->teamSlots = msg.get<uint16_t>();
	teamAssemble->freeSlots = msg.get<uint16_t>();
	teamAssemble->partyBool = (msg.getByte() == 1);
	teamAssemble->timestamp = msg.get<uint32_t>();
	teamAssemble->teamType = msg.getByte();

	uint16_t bossID = 0;
	uint16_t huntType1 = 0;
	uint16_t huntType2 = 0;
	uint16_t questID = 0;

	switch (teamAssemble->teamType) {
		case 1: {
			bossID = msg.get<uint16_t>();
			break;
		}
		case 2: {
			huntType1 = msg.get<uint16_t>();
			huntType2 = msg.get<uint16_t>();
			break;
		}

		case 3: {
			questID = msg.get<uint16_t>();
			break;
		}

		default:
			break;
	}

	teamAssemble->bossID = bossID;
	teamAssemble->hunt_type = huntType1;
	teamAssemble->hunt_area = huntType2;
	teamAssemble->questID = questID;
	teamAssemble->leaderGuid = player->getGUID();

	auto party = player->getParty();
	if (teamAssemble->partyBool && party) {
		for (const std::shared_ptr<Player> &member : party->getMembers()) {
			if (member && member->getGUID() != player->getGUID()) {
				[[maybe_unused]] auto [it_member, inserted_member] = teamAssemble->membersMap.insert({ member->getGUID(), 3 });
			}
		}
		auto partyLeader = party->getLeader();
		if (partyLeader && partyLeader->getGUID() != player->getGUID()) {
			[[maybe_unused]] auto [it_leader, inserted_leader] = teamAssemble->membersMap.insert({ partyLeader->getGUID(), 3 });
		}
	}
}

void ProtocolGame::parsePartyAnalyzerAction(NetworkMessage &msg) const {
	if (!player || oldProtocol) {
		return;
	}

	std::shared_ptr<Party> party = player->getParty();
	if (!party || !party->getLeader() || party->getLeader()->getID() != player->getID()) {
		return;
	}

	auto action = static_cast<PartyAnalyzerAction_t>(msg.getByte());
	if (action == PARTYANALYZERACTION_RESET) {
		party->resetAnalyzer();
	} else if (action == PARTYANALYZERACTION_PRICETYPE) {
		party->switchAnalyzerPriceType();
	} else if (action == PARTYANALYZERACTION_PRICEVALUE) {
		auto size = msg.get<uint16_t>();
		for (uint16_t i = 1; i <= size; i++) {
			auto itemId = msg.get<uint16_t>();
			auto price = msg.get<uint64_t>();
			player->setItemCustomPrice(itemId, price);
		}
		party->reloadPrices();
		party->updateTrackerAnalyzer();
		player->updatePartyTrackerAnalyzer(true);
	}
}

void ProtocolGame::parseLeaderFinderWindow(NetworkMessage &msg) {
	if (!player || oldProtocol) {
		return;
	}

	uint8_t action = msg.getByte();
	switch (action) {
		case 0: {
			player->sendLeaderTeamFinder(false);
			break;
		}
		case 1: {
			player->sendLeaderTeamFinder(true);
			break;
		}
		case 2: {
			auto memberID = msg.get<uint32_t>();
			std::shared_ptr<Player> member = g_game().getPlayerByGUID(memberID);
			if (!member) {
				return;
			}

			const auto &teamAssemble = g_game().getTeamFinder(player);
			if (!teamAssemble) {
				return;
			}

			uint8_t memberStatus = msg.getByte();
			for (auto &[guid, status] : teamAssemble->membersMap) {
				if (guid == memberID) {
					status = memberStatus;
				}
			}

			switch (memberStatus) {
				case 2: {
					member->sendTextMessage(MESSAGE_STATUS, "You are invited to a new team.");
					break;
				}
				case 3: {
					member->sendTextMessage(MESSAGE_STATUS, "Your team finder request was accepted.");
					break;
				}
				case 4: {
					member->sendTextMessage(MESSAGE_STATUS, "Your team finder request was denied.");
					break;
				}

				default:
					break;
			}
			player->sendLeaderTeamFinder(false);
			break;
		}
		case 3: {
			player->createLeaderTeamFinder(msg);
			player->sendLeaderTeamFinder(false);
			break;
		}

		default:
			break;
	}
}

void ProtocolGame::parseMemberFinderWindow(NetworkMessage &msg) {
	if (!player || oldProtocol) {
		return;
	}

	uint8_t action = msg.getByte();
	if (action == 0) {
		player->sendTeamFinderList();
	} else {
		auto leaderID = msg.get<uint32_t>();
		std::shared_ptr<Player> leader = g_game().getPlayerByGUID(leaderID);
		if (!leader) {
			return;
		}

		const auto &teamAssemble = g_game().getTeamFinder(player);
		if (!teamAssemble) {
			return;
		}

		if (action == 1) {
			leader->sendTextMessage(MESSAGE_STATUS, "There is a new request to join your team.");
			[[maybe_unused]] auto [it_request, inserted_request] = teamAssemble->membersMap.insert({ player->getGUID(), 1 });
		} else {
			for (auto itt = teamAssemble->membersMap.begin(), end = teamAssemble->membersMap.end(); itt != end; ++itt) {
				if (itt->first == player->getGUID()) {
					[[maybe_unused]] auto it = teamAssemble->membersMap.erase(itt);
					break;
				}
			}
		}
		player->sendTeamFinderList();
	}
}

void ProtocolGame::parseSendBuyCharmRune(NetworkMessage &msg) {
	if (!player || oldProtocol) {
		return;
	}

	uint8_t action = msg.getByte();
	auto charmId = static_cast<charmRune_t>(msg.getByte());
	uint16_t raceId = msg.get<uint16_t>();
	g_iobestiary().sendBuyCharmRune(player, action, charmId, raceId);
}

void ProtocolGame::refreshCyclopediaMonsterTracker(const std::unordered_set<std::shared_ptr<MonsterType>> &trackerSet, bool isBoss) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xB9);
	msg.addByte(isBoss ? 0x01 : 0x00);
	msg.addByte(trackerSet.size());
	for (const auto &mtype : trackerSet) {
		auto raceId = mtype->info.raceid;
		const auto stages = g_ioBosstiary().getBossRaceKillStages(mtype->info.bosstiaryRace);
		if (isBoss && (stages.empty() || stages.size() != 3)) {
			return;
		}

		uint32_t killAmount = player->getBestiaryKillCount(raceId);
		msg.add<uint16_t>(raceId);
		msg.add<uint32_t>(killAmount);
		bool completed = false;
		if (isBoss) {
			for (const auto &stage : stages) {
				msg.add<uint16_t>(static_cast<uint16_t>(stage.kills));
			}
			completed = g_ioBosstiary().getBossCurrentLevel(player, raceId) == 3;
		} else {
			msg.add<uint16_t>(mtype->info.bestiaryFirstUnlock);
			msg.add<uint16_t>(mtype->info.bestiarySecondUnlock);
			msg.add<uint16_t>(mtype->info.bestiaryToUnlock);
			completed = g_iobestiary().getKillStatus(mtype, killAmount) == 4;
		}

		if (completed) {
			msg.addByte(4);
		} else {
			msg.addByte(0);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::parseBestiarySendCreatures(NetworkMessage &msg) {
	if (!player || oldProtocol) {
		return;
	}

	std::ostringstream ss;
	std::map<uint16_t, std::string> race = {};
	std::string text;
	uint8_t search = msg.getByte();

	if (search == 1) {
		auto monsterAmount = msg.get<uint16_t>();
		const std::map<uint16_t, std::string> &mtype_list = g_game().getBestiaryList();
		for (uint16_t monsterCount = 1; monsterCount <= monsterAmount; monsterCount++) {
			auto raceid = msg.get<uint16_t>();
			if (player->getBestiaryKillCount(raceid) > 0) {
				auto it = mtype_list.find(raceid);
				if (it != mtype_list.end()) {
					[[maybe_unused]] auto [it_race, inserted_race] = race.try_emplace(raceid, it->second);
				}
			}
		}
	} else {
		std::string raceName = msg.getString();
		race = g_iobestiary().findRaceByName(raceName);

		if (race.empty()) {
			g_logger().warn("[ProtocolGame::parseBestiarysendCreature] - "
			                "Race was not found: {}, search: {}",
			                raceName, search);
			return;
		}
		text = raceName;
	}
	NetworkMessage newmsg;
	newmsg.addByte(0xD6);
	newmsg.addString(text);
	newmsg.add<uint16_t>(race.size());
	std::map<uint16_t, uint32_t> creaturesKilled = g_iobestiary().getBestiaryKillCountByMonsterIDs(player, race);

	for (const auto &it_ : race) {
		uint16_t raceid_ = it_.first;
		newmsg.add<uint16_t>(raceid_);

		uint8_t progress = 0;
		uint8_t occurrence = 0;
		for (const auto &_it : creaturesKilled) {
			if (_it.first == raceid_) {
				const auto tmpType = g_monsters().getMonsterType(it_.second);
				if (!tmpType) {
					return;
				}
				progress = g_iobestiary().getKillStatus(tmpType, _it.second);
				occurrence = tmpType->info.bestiaryOccurrence;
			}
		}

		if (progress > 0) {
			newmsg.addByte(progress);
			newmsg.addByte(occurrence);
		} else {
			newmsg.addByte(0);
		}

		const auto monsterType = g_monsters().getMonsterType(it_.second);
		if (monsterType && player->animusMastery().has(it_.second)) {
			newmsg.add<uint16_t>(static_cast<uint16_t>(std::round((player->animusMastery().getExperienceMultiplier() - 1) * 1000))); // Animus Mastery Bonus
		} else {
			newmsg.add<uint16_t>(0);
		}
	}

	newmsg.add<uint16_t>(player->animusMastery().getPoints()); // Animus Mastery Points

	writeToOutputBuffer(newmsg);
}

void ProtocolGame::parseBugReport(NetworkMessage &msg) {
	uint8_t category = msg.getByte();
	std::string message = msg.getString();

	Position position;
	if (category == BUG_CATEGORY_MAP) {
		position = msg.getPosition();
	}

	g_game().playerReportBug(player->getID(), message, position, category);
}

void ProtocolGame::parseGreet(NetworkMessage &msg) {
	auto npcId = msg.get<uint32_t>();
	g_game().playerNpcGreet(player->getID(), npcId);
}

void ProtocolGame::parseOfferDescription(NetworkMessage &msg) {
	auto offerId = msg.get<uint32_t>();
	g_logger().debug("[{}] offer id: {}", __FUNCTION__, offerId);
}

void ProtocolGame::parsePreyAction(NetworkMessage &msg) {
	int8_t index = -1;
	uint8_t slot = msg.getByte();
	uint8_t action = msg.getByte();
	uint8_t option = 0;
	uint16_t raceId = 0;
	if (action == static_cast<uint8_t>(PreyAction_MonsterSelection)) {
		index = msg.getByte();
	} else if (action == static_cast<uint8_t>(PreyAction_Option)) {
		option = msg.getByte();
	} else if (action == static_cast<uint8_t>(PreyAction_ListAll_Selection)) {
		raceId = msg.get<uint16_t>();
	}

	if (!g_configManager().getBoolean(PREY_ENABLED)) {
		return;
	}

	g_game().playerPreyAction(player->getID(), slot, action, option, index, raceId);
}

void ProtocolGame::parseSendResourceBalance() {
	auto [sliverCount, coreCount] = player->getForgeSliversAndCores();

	sendResourcesBalance(
		player->getMoney(),
		player->getBankBalance(),
		player->getPreyCards(),
		player->getTaskHuntingPoints(),
		player->getForgeDusts(),
		sliverCount,
		coreCount
	);

	sendCharmResourcesBalance(
		player->getCharmPoints(),
		player->getMinorCharmEchoes(),
		player->getMaxCharmPoints(),
		player->getMaxMinorCharmEchoes()
	);
}

void ProtocolGame::parseInviteToParty(NetworkMessage &msg) {
	auto targetId = msg.get<uint32_t>();
	g_game().playerInviteToParty(player->getID(), targetId);
}

void ProtocolGame::parseJoinParty(NetworkMessage &msg) {
	auto targetId = msg.get<uint32_t>();
	g_game().playerJoinParty(player->getID(), targetId);
}

void ProtocolGame::parseRevokePartyInvite(NetworkMessage &msg) {
	auto targetId = msg.get<uint32_t>();
	g_game().playerRevokePartyInvitation(player->getID(), targetId);
}

void ProtocolGame::parsePassPartyLeadership(NetworkMessage &msg) {
	auto targetId = msg.get<uint32_t>();
	g_game().playerPassPartyLeadership(player->getID(), targetId);
}

void ProtocolGame::parseEnableSharedPartyExperience(NetworkMessage &msg) {
	bool sharedExpActive = msg.getByte() == 1;
	g_game().playerEnableSharedPartyExperience(player->getID(), sharedExpActive);
}

void ProtocolGame::parseQuestLine(NetworkMessage &msg) {
	auto questId = msg.get<uint16_t>();
	g_game().playerShowQuestLine(player->getID(), questId);
}

void ProtocolGame::parseMarketLeave() {
	g_game().playerLeaveMarket(player->getID());
}

void ProtocolGame::parseMarketBrowse(NetworkMessage &msg) {
	uint16_t browseId = oldProtocol ? msg.get<uint16_t>() : static_cast<uint16_t>(msg.getByte());

	if ((oldProtocol && browseId == MARKETREQUEST_OWN_OFFERS_OLD) || (!oldProtocol && browseId == MARKETREQUEST_OWN_OFFERS)) {
		g_game().playerBrowseMarketOwnOffers(player->getID());
	} else if ((oldProtocol && browseId == MARKETREQUEST_OWN_HISTORY_OLD) || (!oldProtocol && browseId == MARKETREQUEST_OWN_HISTORY)) {
		g_game().playerBrowseMarketOwnHistory(player->getID());
	} else if (!oldProtocol) {
		auto itemId = msg.get<uint16_t>();
		uint8_t tier = 0;
		if (Item::items[itemId].upgradeClassification > 0) {
			tier = msg.get<uint8_t>();
		}
		player->sendMarketEnter(player->getLastDepotId());
		g_game().playerBrowseMarket(player->getID(), itemId, tier);
	} else {
		g_game().playerBrowseMarket(player->getID(), browseId, 0);
	}
}

void ProtocolGame::parseMarketCreateOffer(NetworkMessage &msg) {
	uint8_t type = msg.getByte();
	auto itemId = msg.get<uint16_t>();
	uint8_t itemTier = 0;
	if (!oldProtocol && Item::items[itemId].upgradeClassification > 0) {
		itemTier = msg.getByte();
	}

	auto amount = msg.get<uint16_t>();
	uint64_t price = oldProtocol ? static_cast<uint64_t>(msg.get<uint32_t>()) : msg.get<uint64_t>();
	bool anonymous = (msg.getByte(true) != 0);
	if (amount > 0 && price > 0) {
		g_game().playerCreateMarketOffer(player->getID(), type, itemId, amount, price, itemTier, anonymous);
	}
}

void ProtocolGame::parseMarketCancelOffer(NetworkMessage &msg) {
	auto timestamp = msg.get<uint32_t>();
	auto counter = msg.get<uint16_t>();
	if (counter > 0) {
		g_game().playerCancelMarketOffer(player->getID(), timestamp, counter);
	}

	updateCoinBalance();
}

void ProtocolGame::parseMarketAcceptOffer(NetworkMessage &msg) {
	auto timestamp = msg.get<uint32_t>();
	auto counter = msg.get<uint16_t>();
	auto amount = msg.get<uint16_t>();
	if (amount > 0 && counter > 0) {
		g_game().playerAcceptMarketOffer(player->getID(), timestamp, counter, amount);
	}

	updateCoinBalance();
}

void ProtocolGame::parseModalWindowAnswer(NetworkMessage &msg) {
	auto id = msg.get<uint32_t>();
	uint8_t button = msg.getByte();
	uint8_t choice = msg.getByte();
	g_game().playerAnswerModalWindow(player->getID(), id, button, choice);
}

void ProtocolGame::parseRewardChestCollect(NetworkMessage &msg) {
	const auto position = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	auto stackPosition = msg.getByte();

	// Block collect reward
	auto useCollect = g_configManager().getBoolean(REWARD_CHEST_COLLECT_ENABLED);
	if (!useCollect) {
		return;
	}

	auto maxCollectItems = g_configManager().getNumber(REWARD_CHEST_MAX_COLLECT_ITEMS);
	g_game().playerRewardChestCollect(player->getID(), position, itemId, stackPosition, maxCollectItems);
}

void ProtocolGame::parseBrowseField(NetworkMessage &msg) {
	const Position &pos = msg.getPosition();
	g_game().playerBrowseField(player->getID(), pos);
}

void ProtocolGame::parseSeekInContainer(NetworkMessage &msg) {
	uint8_t containerId = msg.getByte();
	auto index = msg.get<uint16_t>();
	auto primaryType = msg.getByte();
	g_game().playerSeekInContainer(player->getID(), containerId, index, primaryType);
}

// Send methods
void ProtocolGame::addCreatureIcon(NetworkMessage &msg, const std::shared_ptr<Creature> &creature) {
	if (!creature || !player || oldProtocol) {
		return;
	}

	const auto icons = creature->getIcons();
	// client only supports 3 icons, otherwise it will crash
	const auto count = icons.size() > 3 ? 3 : icons.size();
	msg.addByte(count);
	for (uint8_t i = 0; i < count; ++i) {
		const auto icon = icons[i];
		msg.addByte(icon.serialize());
		msg.addByte(static_cast<uint8_t>(icon.category));
		msg.add<uint16_t>(icon.count);
	}
}

void ProtocolGame::updateCoinBalance() {
	if (!player) {
		return;
	}

	g_dispatcher().addEvent(
		[playerId = player->getID()] {
			const auto &threadPlayer = g_game().getPlayerByID(playerId);
			if (threadPlayer && threadPlayer->getAccount()) {
				const auto [coins, errCoin] = threadPlayer->getAccount()->getCoins(CoinType::Normal);
				const auto [transferCoins, errTCoin] = threadPlayer->getAccount()->getCoins(CoinType::Transferable);

				threadPlayer->coinBalance = coins;
				threadPlayer->coinTransferableBalance = transferCoins;
				threadPlayer->sendCoinBalance();
			}
		},
		__FUNCTION__
	);
}

void ProtocolGame::parseForgeEnter(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	// 0xBF -> 0 = fusion, 1 = transfer, 2 = dust to sliver, 3 = sliver to core, 4 = increase dust limit
	const auto actionType = static_cast<ForgeAction_t>(msg.getByte());

	bool convergence = false;
	uint16_t firstItem = 0;
	uint8_t tier = 0;
	uint16_t secondItem = 0;

	if (actionType == ForgeAction_t::FUSION || actionType == ForgeAction_t::TRANSFER) {
		convergence = msg.getByte();
		firstItem = msg.get<uint16_t>();
		tier = msg.getByte();
		secondItem = msg.get<uint16_t>();
	}

	if (actionType == ForgeAction_t::FUSION) {
		const bool usedCore = convergence ? false : msg.getByte();
		const bool reduceTierLoss = convergence ? false : msg.getByte();
		g_game().playerForgeFuseItems(player->getID(), actionType, firstItem, tier, secondItem, usedCore, reduceTierLoss, convergence);
	} else if (actionType == ForgeAction_t::TRANSFER) {
		g_game().playerForgeTransferItemTier(player->getID(), actionType, firstItem, tier, secondItem, convergence);
	} else if (actionType <= ForgeAction_t::INCREASELIMIT) {
		g_game().playerForgeResourceConversion(player->getID(), actionType);
	}
}

void ProtocolGame::parseForgeBrowseHistory(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	g_game().playerBrowseForgeHistory(player->getID(), msg.getByte());
}

void ProtocolGame::closeForgeWindow() {
	NetworkMessage msg;
	msg.addByte(0x89);
	writeToOutputBuffer(msg);
}

void ProtocolGame::removeMagicEffect(const Position &pos, uint16_t type) {
	if (oldProtocol && type > 0xFF) {
		return;
	}
	NetworkMessage msg;
	msg.addByte(0x84);
	msg.addPosition(pos);
	if (oldProtocol) {
		msg.addByte(static_cast<uint8_t>(type));
	} else {
		msg.add<uint16_t>(type);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::addImbuementInfo(NetworkMessage &msg, uint16_t imbuementID, bool isScrollAction /* = false */) const {
	Imbuement* imbuement = g_imbuements().getImbuement(imbuementID);
	const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());

	msg.add<uint32_t>(imbuement->getID());
	msg.addString(fmt::format("{} {}", baseImbuement->name, imbuement->getName()));
	msg.addString(imbuement->getDescription());

	msg.addByte(imbuement->getBaseID() - 1);

	msg.add<uint16_t>(imbuement->getIconID());
	msg.add<uint32_t>(baseImbuement->duration);

	auto items = imbuement->getItems();
	if (isScrollAction) {
		(void)items.emplace_back(std::make_pair(ITEM_EMPTY_IMBUEMENT_SCROLL, 1));
	}
	msg.addByte(items.size());

	for (const auto &[id, amount] : items) {
		const ItemType &it = Item::items[id];
		msg.add<uint16_t>(id);
		msg.addString(it.name);
		msg.add<uint16_t>(amount);
	}

	msg.add<uint32_t>(baseImbuement->price);
}

void ProtocolGame::addAvailableImbuementsInfo(NetworkMessage &msg, const std::shared_ptr<Item> &item, phmap::flat_hash_map<uint16_t, uint16_t> &neededItems, bool isScrollAction /* = false */) const {
	std::vector<Imbuement*> imbuements = g_imbuements().getImbuements(player, item, isScrollAction);
	msg.add<uint16_t>(imbuements.size());
	for (const Imbuement* imbuement : imbuements) {
		addImbuementInfo(msg, imbuement->getID(), isScrollAction);

		const auto &imbuementItems = imbuement->getItems();
		for (const auto &[id, _] : imbuementItems) {
			if (!neededItems.count(id)) {
				const uint32_t invCount = player->getItemTypeCount(id);
				const uint32_t stashCount = player->getStashItemCount(id);
				const uint32_t total = invCount + stashCount;
				neededItems[id] = static_cast<uint16_t>(std::min<uint32_t>(total, std::numeric_limits<uint16_t>::max()));
			}
		}
	}

	if (isScrollAction) {
		if (!neededItems.count(ITEM_EMPTY_IMBUEMENT_SCROLL)) {
			const uint32_t invCount = player->getItemTypeCount(ITEM_EMPTY_IMBUEMENT_SCROLL);
			const uint32_t stashCount = player->getStashItemCount(ITEM_EMPTY_IMBUEMENT_SCROLL);
			const uint32_t total = invCount + stashCount;
			neededItems[ITEM_EMPTY_IMBUEMENT_SCROLL] = static_cast<uint16_t>(std::min<uint32_t>(total, std::numeric_limits<uint16_t>::max()));
		}
	}
}

void ProtocolGame::openImbuementWindow(ImbuementAction action, const std::shared_ptr<Item> &item) {
	if (!item && action == ImbuementAction::PickItem) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xEB);

	phmap::flat_hash_map<uint16_t, uint16_t> neededItems;

	msg.addByte(static_cast<uint8_t>(action));
	const auto emptyImbuementScrolls = player->getItemTypeCount(ITEM_EMPTY_IMBUEMENT_SCROLL) + player->getStashItemCount(ITEM_EMPTY_IMBUEMENT_SCROLL);
	msg.addByte(emptyImbuementScrolls > 0 ? 0x01 : 0x00);
	if (action == ImbuementAction::Open) {
		msg.add<uint16_t>(0);
	} else if (action == ImbuementAction::PickItem) {
		player->setImbuingItem(item);
		msg.add<uint16_t>(item->getID());
		if (item->getClassification() > 0) {
			msg.addByte(item->getTier());
		}

		msg.addByte(item->getImbuementSlot());

		// Imbuements applied
		for (uint8_t slotID = 0; slotID < item->getImbuementSlot(); slotID++) {
			ImbuementInfo imbuementInfo;
			if (!item->getImbuementInfo(slotID, &imbuementInfo)) {
				msg.addByte(0x00);
				continue;
			}

			msg.addByte(0x01);
			addImbuementInfo(msg, imbuementInfo.imbuement->getID());
			msg.add<uint32_t>(imbuementInfo.duration);
			msg.add<uint32_t>(g_imbuements().getBaseByID(imbuementInfo.imbuement->getBaseID())->removeCost);
		}

		addAvailableImbuementsInfo(msg, item, neededItems);

	} else if (action == ImbuementAction::Scroll) {
		const auto freeSlots = player->getFreeBackpackSlots();
		msg.addByte(freeSlots > 0 ? 0x01 : 0x00);
		msg.addByte(0); // Unknown Byte

		addAvailableImbuementsInfo(msg, nullptr, neededItems, true);
	}

	msg.add<uint32_t>(neededItems.size());
	for (const auto &[id, amount] : neededItems) {
		msg.add<uint16_t>(id);
		msg.add<uint16_t>(amount);
	}

	sendResourceBalance(RESOURCE_BANK, player->getBankBalance());
	sendResourceBalance(RESOURCE_INVENTORY_MONEY, player->getMoney());

	writeToOutputBuffer(msg);
}

void ProtocolGame::closeImbuementWindow() {
	NetworkMessage msg;
	msg.addByte(0xEC);
	writeToOutputBuffer(msg);
}

void ProtocolGame::updatePartyTrackerAnalyzer(const std::shared_ptr<Party> &party, bool force) {
	if (oldProtocol || !player || !party || !party->getLeader()) {
		return;
	}

	if (force) {
		m_nextPartyAnalyzerUpdate = 0;
	}

	const uint64_t currentTime = OTSYS_TIME();
	if (!force && currentTime < m_nextPartyAnalyzerUpdate) {
		return;
	}

	m_nextPartyAnalyzerUpdate = currentTime + PARTY_ANALYZER_THROTTLE_MS;

	NetworkMessage msg;
	msg.addByte(0x2B);
	msg.add<uint32_t>(party->getAnalyzerTimeNow());
	msg.add<uint32_t>(party->getLeader()->getID());
	msg.addByte(static_cast<uint8_t>(party->priceType));

	msg.addByte(static_cast<uint8_t>(party->membersData.size()));
	for (const std::shared_ptr<PartyAnalyzer> &analyzer : party->membersData) {
		msg.add<uint32_t>(analyzer->id);
		if (std::shared_ptr<Player> member = g_game().getPlayerByID(analyzer->id);
		    !member || !member->getParty() || member->getParty() != party) {
			msg.addByte(0);
		} else {
			msg.addByte(1);
		}

		msg.add<uint64_t>(analyzer->lootPrice);
		msg.add<uint64_t>(analyzer->supplyPrice);
		msg.add<uint64_t>(analyzer->damage);
		msg.add<uint64_t>(analyzer->healing);
	}

	bool showNames = !party->membersData.empty();
	msg.addByte(showNames ? 0x01 : 0x00);
	if (showNames) {
		msg.addByte(static_cast<uint8_t>(party->membersData.size()));
		for (const std::shared_ptr<PartyAnalyzer> &analyzer : party->membersData) {
			msg.add<uint32_t>(analyzer->id);
			msg.addString(analyzer->name);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::RemoveTileThing(NetworkMessage &msg, const Position &pos, uint32_t stackpos) {
	if (stackpos >= 10) {
		return;
	}

	msg.addByte(0x6C);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
}

void ProtocolGame::MoveUpCreature(NetworkMessage &msg, const std::shared_ptr<Creature> &creature, const Position &newPos, const Position &oldPos) {
	if (creature != player) {
		return;
	}

	// floor change up
	msg.addByte(0xBE);

	// going to surface
	if (newPos.z == MAP_INIT_SURFACE_LAYER) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 5, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 3, skip); //(floor 7 and 6 already set)
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 4, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 4, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 3, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 5, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 2, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 6, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 1, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 7, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, 0, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 8, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}
	// underground, going one floor up (still underground)
	else if (newPos.z > MAP_INIT_SURFACE_LAYER) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, oldPos.getZ() - 3, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, 3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	// moving up a floor up makes us out of sync
	// west
	msg.addByte(0x68);
	GetMapDescription(oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - (MAP_MAX_CLIENT_VIEW_PORT_Y - 1), newPos.z, 1, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, msg);

	// north
	msg.addByte(0x65);
	GetMapDescription(oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, 1, msg);
}

void ProtocolGame::MoveDownCreature(NetworkMessage &msg, const std::shared_ptr<Creature> &creature, const Position &newPos, const Position &oldPos) {
	if (creature != player) {
		return;
	}

	// floor change down
	msg.addByte(0xBF);

	// going from surface to underground
	if (newPos.z == MAP_INIT_SURFACE_LAYER + 1) {
		int32_t skip = -1;

		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, -1, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z + 1, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, -2, skip);
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z + 2, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, -3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}
	// going further down
	else if (newPos.z > oldPos.z && newPos.z > MAP_INIT_SURFACE_LAYER + 1 && newPos.z < MAP_MAX_LAYERS - MAP_LAYER_VIEW_LIMIT) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z + MAP_LAYER_VIEW_LIMIT, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, -3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	// moving down a floor makes us out of sync
	// east
	msg.addByte(0x66);
	GetMapDescription(oldPos.x + MAP_MAX_CLIENT_VIEW_PORT_X + 1, oldPos.y - (MAP_MAX_CLIENT_VIEW_PORT_Y + 1), newPos.z, 1, ((MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2), msg);

	// south
	msg.addByte(0x67);
	GetMapDescription(oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, oldPos.y + (MAP_MAX_CLIENT_VIEW_PORT_Y + 1), newPos.z, ((MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2), 1, msg);
}

void ProtocolGame::parseExtendedOpcode(NetworkMessage &msg) {
	uint8_t opcode = msg.getByte();
	const std::string &buffer = msg.getString();

	// process additional opcodes via lua script event
	g_game().parsePlayerExtendedOpcode(player->getID(), opcode, buffer);
}

// OTCv8
void ProtocolGame::parseInventoryImbuements(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	bool isTrackerOpen = msg.getByte(); // Window is opened or closed
	g_game().playerRequestInventoryImbuements(player->getID(), isTrackerOpen);
}

void ProtocolGame::reloadCreature(const std::shared_ptr<Creature> &creature) {
	if (!creature || !canSee(creature)) {
		return;
	}

	auto tile = creature->getTile();
	if (!tile) {
		return;
	}

	uint32_t stackpos = tile->getClientIndexOfCreature(player, creature);

	if (stackpos >= 10) {
		return;
	}

	NetworkMessage msg;

	if (knownCreatureSet.contains(creature->getID())) {
		msg.addByte(0x6B);
		msg.addPosition(creature->getPosition());
		msg.addByte(static_cast<uint8_t>(stackpos));
		AddCreature(msg, creature, false, 0);
	} else {
		sendAddCreature(creature, creature->getPosition(), stackpos, false);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::parseStashWithdraw(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	if (!player->isAccessPlayer() && !player->isStashMenuAvailable()) {
		player->sendCancelMessage("You can't use stash right now.");
		return;
	}

	if (player->isUIExhausted(500)) {
		player->sendCancelMessage("You need to wait to do this again.");
		return;
	}

	auto action = static_cast<Stash_Actions_t>(msg.getByte());
	switch (action) {
		case STASH_ACTION_STOW_ITEM: {
			Position pos = msg.getPosition();
			auto itemId = msg.get<uint16_t>();
			uint8_t stackpos = msg.getByte();
			uint32_t count = msg.getByte();
			g_game().playerStowItem(player->getID(), pos, itemId, stackpos, count, false);
			break;
		}
		case STASH_ACTION_STOW_CONTAINER: {
			Position pos = msg.getPosition();
			auto itemId = msg.get<uint16_t>();
			uint8_t stackpos = msg.getByte();
			g_game().playerStowItem(player->getID(), pos, itemId, stackpos, 0, false);
			break;
		}
		case STASH_ACTION_STOW_STACK: {
			Position pos = msg.getPosition();
			auto itemId = msg.get<uint16_t>();
			uint8_t stackpos = msg.getByte();
			g_game().playerStowItem(player->getID(), pos, itemId, stackpos, 0, true);
			break;
		}
		case STASH_ACTION_WITHDRAW: {
			auto itemId = msg.get<uint16_t>();
			auto count = msg.get<uint32_t>();
			uint8_t stackpos = msg.getByte();
			g_game().playerStashWithdraw(player->getID(), itemId, count, stackpos);
			break;
		}
		default:
			g_logger().error("Unknown 'stash' action switch: {}", fmt::underlying(action));
			break;
	}

	player->updateUIExhausted();
}

void ProtocolGame::parseOpenDepotSearch() {
	if (oldProtocol) {
		return;
	}

	g_game().playerRequestDepotItems(player->getID());
}

void ProtocolGame::parseCloseDepotSearch() {
	if (oldProtocol) {
		return;
	}

	g_game().playerRequestCloseDepotSearch(player->getID());
}

void ProtocolGame::parseDepotSearchItemRequest(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto itemId = msg.get<uint16_t>();
	uint8_t itemTier = 0;
	if (Item::items[itemId].upgradeClassification > 0) {
		itemTier = msg.getByte();
	}

	g_game().playerRequestDepotSearchItem(player->getID(), itemId, itemTier);
}

void ProtocolGame::parseRetrieveDepotSearch(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	auto itemId = msg.get<uint16_t>();
	uint8_t itemTier = 0;
	if (Item::items[itemId].upgradeClassification > 0) {
		itemTier = msg.getByte();
	}
	uint8_t type = msg.getByte();

	g_game().playerRequestDepotSearchRetrieve(player->getID(), itemId, itemTier, type);
}

void ProtocolGame::parseOpenParentContainer(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	Position pos = msg.getPosition();
	g_game().playerRequestOpenContainerFromDepotSearch(player->getID(), pos);
}

void ProtocolGame::getForgeInfoMap(const std::shared_ptr<Item> &item, std::map<uint16_t, std::map<uint8_t, uint16_t>> &itemsMap) const {
	std::map<uint8_t, uint16_t> itemInfo;
	[[maybe_unused]] auto result = itemInfo.insert({ item->getTier(), item->getItemCount() });
	auto [first, inserted] = itemsMap.try_emplace(item->getID(), itemInfo);
	if (!inserted) {
		auto [otherFirst, otherInserted] = itemsMap[item->getID()].try_emplace(item->getTier(), item->getItemCount());
		if (!otherInserted) {
			(itemsMap[item->getID()])[item->getTier()] += item->getItemCount();
		}
	}
}

double ProtocolGame::getForgeSkillStat(Slots_t slot, bool applyAmplification /*= true*/) const {
	if (oldProtocol) {
		return 0;
	}

	double skill = 0;
	if (const auto &item = player->getInventoryItem(slot); item) {
		const ItemType &it = Item::items[item->getID()];
		if (it.isWeapon()) {
			skill = item->getFatalChance();
		}
		if (it.isArmor()) {
			skill = item->getDodgeChance();
		}
		if (it.isHelmet()) {
			skill = item->getMomentumChance();
		}
		if (it.isLegs()) {
			skill = item->getTranscendenceChance();
		}
		if (it.isBoots()) {
			skill = item->getAmplificationChance();
		}
	}

	if (applyAmplification) {
		const auto &boots = player->getInventoryItem(CONST_SLOT_FEET);
		if (slot != CONST_SLOT_FEET && boots) {
			skill *= 1 + (boots->getAmplificationChance() / 100);
		}
	}

	return skill / 100;
}

void ProtocolGame::parseSendBosstiary() {
	if (oldProtocol) {
		return;
	}

	sendBosstiaryData();

	NetworkMessage msg;
	msg.addByte(0x73);

	auto mtype_map = g_ioBosstiary().getBosstiaryMap();
	auto bossesBuffer = msg.getBufferPosition();
	uint16_t bossesCount = 0;
	msg.skipBytes(2);

	for (const auto &[bossid, name] : mtype_map) {
		const auto mType = g_monsters().getMonsterType(name);
		if (!mType) {
			continue;
		}

		auto bossRace = mType->info.bosstiaryRace;
		if (bossRace < BosstiaryRarity_t::RARITY_BANE || bossRace > BosstiaryRarity_t::RARITY_NEMESIS) {
			g_logger().error("[{}] monster {} have wrong boss race {}", __FUNCTION__, mType->name, fmt::underlying(bossRace));
			continue;
		}

		auto killCount = player->getBestiaryKillCount(bossid);
		msg.add<uint32_t>(bossid);
		msg.addByte(static_cast<uint8_t>(bossRace));
		msg.add<uint32_t>(killCount);
		msg.addByte(0);
		msg.addByte(player->isBossOnBosstiaryTracker(mType) ? 0x01 : 0x00);
		++bossesCount;
	}

	msg.setBufferPosition(bossesBuffer);
	msg.add<uint16_t>(bossesCount);

	writeToOutputBuffer(msg);
}

void ProtocolGame::parseSendBosstiarySlots() {
	if (oldProtocol) {
		return;
	}

	uint32_t bossIdSlotOne = player->getSlotBossId(1);
	uint32_t bossIdSlotTwo = player->getSlotBossId(2);
	uint32_t boostedBossId = g_ioBosstiary().getBoostedBossId();

	// Sanity checks
	const std::string &boostedBossName = g_ioBosstiary().getBoostedBossName();
	if (boostedBossName.empty()) {
		g_logger().error("[{}] The boosted boss name is empty", __FUNCTION__);
		return;
	}
	const auto mTypeBoosted = g_monsters().getMonsterType(boostedBossName);
	auto boostedBossRace = mTypeBoosted ? mTypeBoosted->info.bosstiaryRace : BosstiaryRarity_t::BOSS_INVALID;
	auto isValidBoostedBoss = boostedBossId == 0 || (boostedBossRace >= BosstiaryRarity_t::RARITY_BANE && boostedBossRace <= BosstiaryRarity_t::RARITY_NEMESIS);
	if (!isValidBoostedBoss) {
		g_logger().error("[{}] The boosted boss '{}' has an invalid race", __FUNCTION__, boostedBossName);
		return;
	}

	const auto mTypeSlotOne = g_ioBosstiary().getMonsterTypeByBossRaceId((uint16_t)bossIdSlotOne);
	auto bossRaceSlotOne = mTypeSlotOne ? mTypeSlotOne->info.bosstiaryRace : BosstiaryRarity_t::BOSS_INVALID;
	auto isValidBossSlotOne = bossIdSlotOne == 0 || (bossRaceSlotOne >= BosstiaryRarity_t::RARITY_BANE && bossRaceSlotOne <= BosstiaryRarity_t::RARITY_NEMESIS);
	if (!isValidBossSlotOne) {
		g_logger().error("[{}] boss slot1 with race id '{}' has an invalid race", __FUNCTION__, bossIdSlotOne);
		return;
	}

	const auto mTypeSlotTwo = g_ioBosstiary().getMonsterTypeByBossRaceId((uint16_t)bossIdSlotTwo);
	auto bossRaceSlotTwo = mTypeSlotTwo ? mTypeSlotTwo->info.bosstiaryRace : BosstiaryRarity_t::BOSS_INVALID;
	auto isValidBossSlotTwo = bossIdSlotTwo == 0 || (bossRaceSlotTwo >= BosstiaryRarity_t::RARITY_BANE && bossRaceSlotTwo <= BosstiaryRarity_t::RARITY_NEMESIS);
	if (!isValidBossSlotTwo) {
		g_logger().error("[{}] boss slot1 with race id '{}' has an invalid race", __FUNCTION__, bossIdSlotTwo);
		return;
	}

	sendBosstiaryData();

	NetworkMessage msg;
	msg.addByte(0x62);

	uint32_t playerBossPoints = player->getBossPoints();
	uint16_t currentBonus = g_ioBosstiary().calculateLootBonus(playerBossPoints);
	uint32_t pointsNextBonus = g_ioBosstiary().calculateBossPoints(currentBonus + 1);
	msg.add<uint32_t>(playerBossPoints); // Player Points
	msg.add<uint32_t>(pointsNextBonus); // Total Points next bonus
	msg.add<uint16_t>(currentBonus); // Current Bonus
	msg.add<uint16_t>(currentBonus + 1); // Next Bonus

	uint32_t removePrice = g_ioBosstiary().calculteRemoveBoss(player->getRemoveTimes());

	auto bossesUnlockedList = g_ioBosstiary().getBosstiaryFinished(player);
	if (auto it = std::ranges::find(bossesUnlockedList.begin(), bossesUnlockedList.end(), boostedBossId);
	    it != bossesUnlockedList.end()) {
		[[maybe_unused]] auto it_erase = bossesUnlockedList.erase(it);
	}
	auto bossesUnlockedSize = static_cast<uint16_t>(bossesUnlockedList.size());

	bool isSlotOneUnlocked = (bossesUnlockedSize > 0 ? true : false);
	msg.addByte(isSlotOneUnlocked ? 1 : 0);
	msg.add<uint32_t>(isSlotOneUnlocked ? bossIdSlotOne : 0);
	if (isSlotOneUnlocked && bossIdSlotOne != 0) {
		// Variables Boss Slot One
		auto bossKillCount = player->getBestiaryKillCount(static_cast<uint16_t>(bossIdSlotOne));
		auto slotOneBossLevel = g_ioBosstiary().getBossCurrentLevel(player, (uint16_t)bossIdSlotOne);
		uint16_t bonusBossSlotOne = currentBonus + (slotOneBossLevel == 3 ? 25 : 0);
		uint8_t isSlotOneInactive = bossIdSlotOne == boostedBossId ? 1 : 0;
		// Bytes Slot One
		sendBosstiarySlotsBytes(msg, static_cast<uint8_t>(bossRaceSlotOne), bossKillCount, bonusBossSlotOne, 0, isSlotOneInactive, removePrice);
		bossesUnlockedSize--;
	}

	uint32_t slotTwoPoints = 1500;
	bool isSlotTwoUnlocked = (playerBossPoints >= slotTwoPoints ? true : false);
	msg.addByte(isSlotTwoUnlocked ? 1 : 0);
	msg.add<uint32_t>(isSlotTwoUnlocked ? bossIdSlotTwo : slotTwoPoints);
	if (isSlotTwoUnlocked && bossIdSlotTwo != 0) {
		// Variables Boss Slot Two
		auto bossKillCount = player->getBestiaryKillCount((uint16_t)(bossIdSlotTwo));
		auto slotTwoBossLevel = g_ioBosstiary().getBossCurrentLevel(player, (uint16_t)bossIdSlotTwo);
		uint16_t bonusBossSlotTwo = currentBonus + (slotTwoBossLevel == 3 ? 25 : 0);
		uint8_t isSlotTwoInactive = bossIdSlotTwo == boostedBossId ? 1 : 0;
		// Bytes Slot Two
		sendBosstiarySlotsBytes(msg, static_cast<uint8_t>(bossRaceSlotTwo), bossKillCount, bonusBossSlotTwo, 0, isSlotTwoInactive, removePrice);
		bossesUnlockedSize--;
	}

	bool isTodaySlotUnlocked = g_configManager().getBoolean(BOOSTED_BOSS_SLOT);
	msg.addByte(isTodaySlotUnlocked ? 1 : 0);
	msg.add<uint32_t>(boostedBossId);
	if (isTodaySlotUnlocked && boostedBossId != 0) {
		auto boostedBossKillCount = player->getBestiaryKillCount(static_cast<uint16_t>(boostedBossId));
		auto boostedLootBonus = static_cast<uint16_t>(g_configManager().getNumber(BOOSTED_BOSS_LOOT_BONUS));
		auto bosstiaryMultiplier = static_cast<uint8_t>(g_configManager().getNumber(BOSSTIARY_KILL_MULTIPLIER));
		auto boostedKillBonus = static_cast<uint8_t>(g_configManager().getNumber(BOOSTED_BOSS_KILL_BONUS));
		sendBosstiarySlotsBytes(msg, static_cast<uint8_t>(boostedBossRace), boostedBossKillCount, boostedLootBonus, bosstiaryMultiplier + boostedKillBonus, 0, 0);
	}

	msg.addByte(bossesUnlockedSize != 0 ? 1 : 0);
	if (bossesUnlockedSize != 0) {
		auto unlockCountBuffer = msg.getBufferPosition();
		uint16_t bossesCount = 0;
		msg.skipBytes(2);
		for (const auto &bossId : bossesUnlockedList) {
			if (bossId == bossIdSlotOne || bossId == bossIdSlotTwo) {
				continue;
			}

			const auto mType = g_ioBosstiary().getMonsterTypeByBossRaceId(bossId);
			if (!mType) {
				g_logger().error("[{}] monster {} not found", __FUNCTION__, bossId);
				continue;
			}

			auto bossRace = mType->info.bosstiaryRace;
			if (bossRace < BosstiaryRarity_t::RARITY_BANE || bossRace > BosstiaryRarity_t::RARITY_NEMESIS) {
				g_logger().error("[{}] monster {} have wrong boss race {}", __FUNCTION__, mType->name, fmt::underlying(bossRace));
				continue;
			}

			msg.add<uint32_t>(bossId);
			msg.addByte(static_cast<uint8_t>(bossRace));
			bossesCount++;
		}
		msg.setBufferPosition(unlockCountBuffer);
		msg.add<uint16_t>(bossesCount);
	}

	writeToOutputBuffer(msg);
	parseSendResourceBalance();
}

void ProtocolGame::parseBosstiarySlot(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t slotBossId = msg.getByte();
	auto selectedBossId = msg.get<uint32_t>();

	g_game().playerBosstiarySlot(player->getID(), slotBossId, selectedBossId);
}

void ProtocolGame::parseSetMonsterPodium(NetworkMessage &msg) const {
	if (!player || oldProtocol) {
		return;
	}

	// For some reason the cip sends uint32_t, but we use uint16_t, so let's just ignore that
	auto monsterRaceId = static_cast<uint16_t>(msg.get<uint32_t>());
	Position pos = msg.getPosition();
	auto itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint8_t direction = msg.getByte();
	uint8_t podiumVisible = msg.getByte();
	uint8_t monsterVisible = msg.getByte();

	g_game().playerSetMonsterPodium(player->getID(), monsterRaceId, pos, stackpos, itemId, direction, std::make_pair(podiumVisible, monsterVisible));
}

void ProtocolGame::parseOpenWheel(NetworkMessage &msg) {
	if (oldProtocol || !g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		return;
	}

	auto ownerId = msg.get<uint32_t>();
	g_game().playerOpenWheel(player->getID(), ownerId);
}

void ProtocolGame::parseWheelGemAction(NetworkMessage &msg) {
	if (oldProtocol || !g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		return;
	}

	g_game().playerWheelGemAction(player->getID(), msg);
}

void ProtocolGame::parseSaveWheel(NetworkMessage &msg) {
	if (oldProtocol || !g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		return;
	}

	g_game().playerSaveWheel(player->getID(), msg);
}

void ProtocolGame::parseCyclopediaHouseAuction(NetworkMessage &msg) {
	if (oldProtocol) {
		return;
	}

	uint8_t houseActionType = msg.getByte();
	switch (houseActionType) {
		case 0: {
			const auto townName = msg.getString();
			g_game().playerCyclopediaHousesByTown(player->getID(), townName);
			break;
		}
		case 1: {
			const uint32_t houseId = msg.get<uint32_t>();
			const uint64_t bidValue = msg.get<uint64_t>();
			g_game().playerCyclopediaHouseBid(player->getID(), houseId, bidValue);
			break;
		}
		case 2: {
			const uint32_t houseId = msg.get<uint32_t>();
			const uint32_t timestamp = msg.get<uint32_t>();
			g_game().playerCyclopediaHouseMoveOut(player->getID(), houseId, timestamp);
			break;
		}
		case 3: {
			const uint32_t houseId = msg.get<uint32_t>();
			const uint32_t timestamp = msg.get<uint32_t>();
			const std::string &newOwner = msg.getString();
			const uint64_t bidValue = msg.get<uint64_t>();
			g_game().playerCyclopediaHouseTransfer(player->getID(), houseId, timestamp, newOwner, bidValue);
			break;
		}
		case 4: {
			const uint32_t houseId = msg.get<uint32_t>();
			g_game().playerCyclopediaHouseCancelMoveOut(player->getID(), houseId);
			break;
		}
		case 5: {
			const uint32_t houseId = msg.get<uint32_t>();
			g_game().playerCyclopediaHouseCancelTransfer(player->getID(), houseId);
			break;
		}
		case 6: {
			const uint32_t houseId = msg.get<uint32_t>();
			g_game().playerCyclopediaHouseAcceptTransfer(player->getID(), houseId);
			break;
		}
		case 7: {
			const uint32_t houseId = msg.get<uint32_t>();
			g_game().playerCyclopediaHouseRejectTransfer(player->getID(), houseId);
			break;
		}
	}
}

void ProtocolGame::parseAimAtTarget(NetworkMessage &msg) {
	if (!player) {
		return; // Safety check: ensure player exists
	}

	uint8_t amount = msg.getByte(); // Number of spells being updated
	for (uint8_t i = 0; i < amount; ++i) {
		uint16_t spellId = msg.get<uint16_t>(); // ID of the spell
		uint8_t state = msg.getByte(); // State: 1 = enabled, 0 = disabled
		player->updateAimAtTargetSpells(spellId, state); // Update player's config
	}
}

void ProtocolGame::parseExivaRestrictions(NetworkMessage &msg) {
	if (!player || g_configManager().getString(WORLD_TYPE) != "no-pvp") {
		return;
	}

	auto &restrictions = player->getExivaRestrictions();

	restrictions.allowAll = msg.getByte();
	restrictions.allowOwnGuild = msg.getByte();
	restrictions.allowOwnParty = msg.getByte();
	restrictions.allowVipList = msg.getByte();
	restrictions.allowPlayerWhitelist = msg.getByte();
	restrictions.allowGuildWhitelist = msg.getByte();

	const int32_t MAX_EXIVA_WHITELIST = g_configManager().getNumber(ConfigKey_t::MAX_EXIVA_WHITELIST);

	std::vector<std::string> addedPlayerNames;
	std::unordered_set<uint32_t> addedPlayerGuids;
	addExivaEntries(msg, restrictions.playerWhitelist, addedPlayerNames, addedPlayerGuids, MAX_EXIVA_WHITELIST, [](const std::string &n) {
		return g_playerRepository().getGuidByName(n);
	});

	std::vector<std::string> removedPlayerNames;
	std::unordered_set<uint32_t> removedPlayerGuids;
	removeExivaEntries(msg, restrictions.playerWhitelist, removedPlayerNames, removedPlayerGuids, MAX_EXIVA_WHITELIST, [](const std::string &n) {
		return g_playerRepository().getGuidByName(n);
	});

	std::vector<std::string> addedGuildNames;
	std::unordered_set<uint32_t> addedGuildIds;
	addExivaEntries(msg, restrictions.guildWhitelist, addedGuildNames, addedGuildIds, MAX_EXIVA_WHITELIST, [](const std::string &n) {
		return g_guildRepository().getGuildIdByName(n);
	});

	std::vector<std::string> removedGuildNames;
	std::unordered_set<uint32_t> removedGuildIds;
	removeExivaEntries(msg, restrictions.guildWhitelist, removedGuildNames, removedGuildIds, MAX_EXIVA_WHITELIST, [](const std::string &n) {
		return g_guildRepository().getGuildIdByName(n);
	});

	sendExivaRestrictions(false, addedPlayerNames, removedPlayerNames, addedGuildNames, removedGuildNames);
}

void ProtocolGame::sendClientLoginPreamble(OperatingSystem_t operatingSystem) {
	if (otclientV8 > 0) {
		sendFeatures();
	}

	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		isOTC = true;
		if (otclientV8 == 0) {
			sendOTCRFeatures();
		}

		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}
}

void ProtocolGame::castViewerLogin(const std::string &name, const std::string &password, OperatingSystem_t operatingSystem) {
	sendClientLoginPreamble(operatingSystem);

	const auto &foundPlayer = g_game().getPlayerByName(name);
	if (!canWatchLivestream(foundPlayer, password)) {
		return;
	}

	m_isLivestreamViewer = true;
	player = foundPlayer;
	acceptPackets = true;
	sendLivestreamViewerAppear(foundPlayer);
	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
}

bool ProtocolGame::canWatchLivestream(const std::shared_ptr<Player> &foundPlayer, const std::string &password) {
	std::string reason;
	if (!g_livestream().canWatch(foundPlayer, getThis(), password, reason)) {
		disconnectClient(reason);
		return false;
	}

	return true;
}

void ProtocolGame::sendLivestreamViewerAppear(const std::shared_ptr<Player> &foundPlayer) {
	if (!foundPlayer) {
		return;
	}

	player = foundPlayer;
	const auto ownerClient = foundPlayer->client;
	foundPlayer->client = getThis();
	sendAddCreature(player, player->getPosition(), 0, true);
	foundPlayer->client = ownerClient;

	syncLivestreamViewerOpenContainers(player);
	g_livestream().addViewer(player, getThis());

	const auto viewerCount = g_livestream().getViewerCount(player);
	sendTextMessage(TextMessage(MESSAGE_LOOK, fmt::format("{} is broadcasting for {} people.\nLivestream time: {}", player->getName(), viewerCount, g_livestream().getBroadcastTimeString(player))));

	const auto description = g_livestream().getDescription(player);
	if (!description.empty()) {
		sendCreatureSay(player, TALKTYPE_SAY, description);
	}

	if (!oldProtocol) {
		const std::unordered_set<PlayerIcon> iconSet { PlayerIcon::Rooted };
		sendIcons(iconSet, IconBakragore::None);
	}

	sendChannel(CHANNEL_LIVESTREAM, "Livestream", nullptr, nullptr);
	sendTextMessage(TextMessage(MESSAGE_EVENT_ADVANCE, "Available commands: \n/name newname\n/show"));
}

void ProtocolGame::syncLivestreamViewerOpenContainers(const std::shared_ptr<Player> &foundPlayer) {
	if (!foundPlayer) {
		return;
	}

	for (const auto &[cid, openContainer] : foundPlayer->getOpenContainers()) {
		if (openContainer.container) {
			sendContainer(cid, openContainer.container, openContainer.container->hasParent(), openContainer.index);
		}
	}
}

void ProtocolGame::resendLivestreamViewerContainer(NetworkMessage &msg) {
	if (!player) {
		return;
	}

	const uint8_t cid = msg.getByte();
	const auto &openContainers = player->getOpenContainers();
	const auto it = openContainers.find(cid);
	if (it == openContainers.end() || !it->second.container) {
		sendCloseContainer(cid);
		return;
	}

	sendContainer(cid, it->second.container, it->second.container->hasParent(), it->second.index);
}
