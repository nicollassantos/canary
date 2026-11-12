/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// ProtocolGame send methods: Chat channels, messages, VIP, party/team finder and analyzer packets.
// Split from protocolgame_send.cpp; shares protocolgame_helpers.hpp.

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
#include "io/market_repository.hpp"
#include "io/iologindata.hpp"
#include "io/iomarket.hpp"
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
#include "creatures/players/components/pvp/expert_pvp.hpp"
#include "enums/player_wheel.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "server/network/connection/connection.hpp"
#include "server/network/protocol/protocol_port_utils.hpp"
#include "server/network/protocol/transport_codec.hpp"
#include "server/network/protocol/protocolgame_helpers.hpp"

void ProtocolGame::sendTeamFinderList() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x2D);
	msg.addByte(0x00); // Bool value, with 'true' the player exceed packets for second.
	const auto &teamFinder = g_game().getTeamFinderList();
	msg.add<uint16_t>(teamFinder.size());
	for (const auto &it : teamFinder) {
		const auto &leader = g_game().getPlayerByGUID(it.first);
		if (!leader) {
			return;
		}

		const auto &teamAssemble = it.second;
		if (!teamAssemble) {
			return;
		}

		uint8_t status = 0;
		uint16_t membersSize = 0;
		msg.add<uint32_t>(leader->getGUID());
		msg.addString(leader->getName());
		msg.add<uint16_t>(teamAssemble->minLevel);
		msg.add<uint16_t>(teamAssemble->maxLevel);
		msg.addByte(teamAssemble->vocationIDs);
		msg.add<uint16_t>(teamAssemble->teamSlots);
		for (auto itt : teamAssemble->membersMap) {
			std::shared_ptr<Player> member = g_game().getPlayerByGUID(it.first);
			if (member) {
				if (itt.first == player->getGUID()) {
					status = itt.second;
				}

				if (itt.second == 3) {
					membersSize += 1;
				}
			}
		}
		msg.add<uint16_t>(std::max<uint16_t>((teamAssemble->teamSlots - teamAssemble->freeSlots), membersSize));
		// The leader does not count on this math, he is included inside the 'freeSlots'.
		msg.add<uint32_t>(teamAssemble->timestamp);
		msg.addByte(teamAssemble->teamType);

		switch (teamAssemble->teamType) {
			case 1: {
				msg.add<uint16_t>(teamAssemble->bossID);
				break;
			}
			case 2: {
				msg.add<uint16_t>(teamAssemble->hunt_type);
				msg.add<uint16_t>(teamAssemble->hunt_area);
				break;
			}
			case 3: {
				msg.add<uint16_t>(teamAssemble->questID);
				break;
			}

			default:
				break;
		}

		msg.addByte(status);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendLeaderTeamFinder(bool reset) {
	if (!player || oldProtocol) {
		return;
	}

	const auto &teamAssemble = g_game().getTeamFinder(player);
	if (!teamAssemble) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x2C);
	msg.addByte(reset ? 1 : 0);
	if (reset) {
		g_game().removeTeamFinderListed(player->getGUID());
		return;
	}

	msg.add<uint16_t>(teamAssemble->minLevel);
	msg.add<uint16_t>(teamAssemble->maxLevel);
	msg.addByte(teamAssemble->vocationIDs);
	msg.add<uint16_t>(teamAssemble->teamSlots);
	msg.add<uint16_t>(teamAssemble->freeSlots);
	msg.add<uint32_t>(teamAssemble->timestamp);
	msg.addByte(teamAssemble->teamType);

	switch (teamAssemble->teamType) {
		case 1: {
			msg.add<uint16_t>(teamAssemble->bossID);
			break;
		}
		case 2: {
			msg.add<uint16_t>(teamAssemble->hunt_type);
			msg.add<uint16_t>(teamAssemble->hunt_area);
			break;
		}
		case 3: {
			msg.add<uint16_t>(teamAssemble->questID);
			break;
		}

		default:
			break;
	}

	uint16_t membersSize = 1;
	for (const auto &[memberGUID, memberStatus] : teamAssemble->membersMap) {
		std::shared_ptr<Player> member = g_game().getPlayerByGUID(memberGUID);
		if (member) {
			membersSize += 1;
		}
	}

	msg.add<uint16_t>(membersSize);
	std::shared_ptr<Player> leader = g_game().getPlayerByGUID(teamAssemble->leaderGuid);
	if (!leader) {
		return;
	}

	msg.add<uint32_t>(leader->getGUID());
	msg.addString(leader->getName());
	msg.add<uint16_t>(leader->getLevel());
	msg.addByte(leader->getVocation()->getClientId());
	msg.addByte(3);

	const auto countPos = msg.getBufferPosition();
	uint8_t count = 0;

	for (const auto &[memberGUID, memberStatus] : teamAssemble->membersMap) {
		auto member = g_game().getPlayerByGUID(memberGUID);
		if (!member) {
			continue;
		}

		++count;
	}

	for (const auto &[memberGUID, memberStatus] : teamAssemble->membersMap) {
		auto member = g_game().getPlayerByGUID(memberGUID);
		if (!member) {
			continue;
		}
		msg.add<uint32_t>(member->getGUID());
		msg.addString(member->getName());
		msg.add<uint16_t>(member->getLevel());
		msg.addByte(member->getVocation()->getClientId());
		msg.addByte(memberStatus);
	}

	msg.setBufferPosition(countPos);
	msg.addByte(count);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenPrivateChannel(const std::string &receiver) {
	NetworkMessage msg;
	msg.addByte(0xAD);
	msg.addString(receiver);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelEvent(uint16_t channelId, const std::string &playerName, ChannelEvent_t channelEvent) {
	NetworkMessage msg;
	msg.addByte(0xF3);
	msg.add<uint16_t>(channelId);
	msg.addString(playerName);
	msg.addByte(channelEvent);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTutorial(uint8_t tutorialId) {
	NetworkMessage msg;
	msg.addByte(0xDC);
	msg.addByte(tutorialId);
	writeToOutputBuffer(msg);
}

bool ProtocolGame::sendCipsoft860SpecialTextMessage(const TextMessage &message, MessageClasses internalType) {
	switch (internalType) {
		case MESSAGE_DAMAGE_DEALT:
		case MESSAGE_DAMAGE_RECEIVED:
		case MESSAGE_DAMAGE_OTHERS: {
			NetworkMessage msg;
			if (message.primary.value != 0) {
				msg.addByte(0x84);
				msg.addPosition(message.position);
				msg.addByte(message.primary.color);
				msg.addString(std::to_string(message.primary.value));
			}
			if (message.secondary.value != 0) {
				msg.addByte(0x84);
				msg.addPosition(message.position);
				msg.addByte(message.secondary.color);
				msg.addString(std::to_string(message.secondary.value));
			}
			if (!message.text.empty()) {
				msg.addByte(0xB4);
				msg.addByte(cipsoft860EventDefaultMessage);
				msg.addString(message.text);
			}
			writeToOutputBuffer(msg);
			return true;
		}
		case MESSAGE_MANA:
		case MESSAGE_HEALED:
		case MESSAGE_HEALED_OTHERS:
		case MESSAGE_EXPERIENCE:
		case MESSAGE_EXPERIENCE_OTHERS: {
			NetworkMessage msg;
			msg.addByte(0x84);
			msg.addPosition(message.position);
			msg.addByte(message.primary.color);
			msg.addString(std::to_string(message.primary.value));
			if (!message.text.empty()) {
				msg.addByte(0xB4);
				msg.addByte(cipsoft860EventDefaultMessage);
				msg.addString(message.text);
			}
			writeToOutputBuffer(msg);
			return true;
		}
		default:
			return false;
	}
}

void ProtocolGame::sendTextMessage(const TextMessage &message) {
	if (message.type == MESSAGE_NONE) {
		g_logger().error("[ProtocolGame::sendTextMessage] - Message type is wrong, missing or invalid for player with name {}, on position {}", player->getName(), player->getPosition().toString());
		player->sendTextMessage(MESSAGE_ADMINISTRATOR, "There was a problem requesting your message, please contact the administrator");
		return;
	}

	MessageClasses internalType = message.type;
	if (oldProtocol && message.type > MESSAGE_LAST_OLDPROTOCOL) {
		switch (internalType) {
			case MESSAGE_REPORT: {
				internalType = MESSAGE_LOOT;
				break;
			}
			case MESSAGE_HOTKEY_PRESSED: {
				internalType = MESSAGE_LOOK;
				break;
			}
			case MESSAGE_TUTORIAL_HINT: {
				internalType = MESSAGE_LOGIN;
				break;
			}
			case MESSAGE_THANK_YOU: {
				internalType = MESSAGE_LOGIN;
				break;
			}
			case MESSAGE_MARKET: {
				internalType = MESSAGE_EVENT_ADVANCE;
				break;
			}
			case MESSAGE_MANA: {
				internalType = MESSAGE_HEALED;
				break;
			}
			case MESSAGE_BEYOND_LAST: {
				internalType = MESSAGE_LOOT;
				break;
			}
			case MESSAGE_ATTENTION: {
				internalType = MESSAGE_EVENT_ADVANCE;
				break;
			}
			case MESSAGE_BOOSTED_CREATURE: {
				internalType = MESSAGE_LOOT;
				break;
			}
			case MESSAGE_OFFLINE_TRAINING: {
				internalType = MESSAGE_LOOT;
				break;
			}
			case MESSAGE_TRANSACTION: {
				internalType = MESSAGE_LOOT;
				break;
			}
			case MESSAGE_POTION: {
				internalType = MESSAGE_FAILURE;
				break;
			}

			default: {
				internalType = MESSAGE_EVENT_ADVANCE;
				break;
			}
		}
	}

	uint8_t clientType = static_cast<uint8_t>(internalType);
	if (isCipsoft860Profile(protocolProfile)) {
		clientType = translateCipsoft860MessageClassToClient(internalType);
		if (clientType == MESSAGE_NONE) {
			sendCipsoft860SpecialTextMessage(message, internalType);
			return;
		}
	}

	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(clientType);
	switch (internalType) {
		case MESSAGE_DAMAGE_DEALT:
		case MESSAGE_DAMAGE_RECEIVED:
		case MESSAGE_DAMAGE_OTHERS: {
			msg.addPosition(message.position);
			msg.add<uint32_t>(message.primary.value);
			msg.addByte(message.primary.color);
			msg.add<uint32_t>(message.secondary.value);
			msg.addByte(message.secondary.color);
			break;
		}
		case MESSAGE_HEALED:
		case MESSAGE_HEALED_OTHERS: {
			if (!oldProtocol) {
				msg.addPosition(message.position);
				msg.add<uint32_t>(message.primary.value);
				msg.addByte(message.primary.color);
			}
			break;
		}
		case MESSAGE_EXPERIENCE:
		case MESSAGE_EXPERIENCE_OTHERS: {
			msg.addPosition(message.position);
			if (!oldProtocol) {
				msg.add<uint64_t>(message.primary.value);
			} else {
				msg.add<uint32_t>(message.primary.value);
			}
			msg.addByte(message.primary.color);
			break;
		}
		case MESSAGE_GUILD:
		case MESSAGE_PARTY_MANAGEMENT:
		case MESSAGE_PARTY:
			msg.add<uint16_t>(message.channelId);
			break;
		default:
			break;
	}
	msg.addString(message.text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatePrivateChannel(uint16_t channelId, const std::string &channelName) {
	NetworkMessage msg;
	msg.addByte(0xB2);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	msg.add<uint16_t>(0x01);
	msg.addString(player->getName());
	msg.add<uint16_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelsDialog() {
	NetworkMessage msg;
	msg.addByte(0xAB);

	const ChannelList &list = g_chat().getChannelList(player);
	msg.addByte(list.size());
	for (const auto &channel : list) {
		msg.add<uint16_t>(channel->getId());
		msg.addString(channel->getName());
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannel(uint16_t channelId, const std::string &channelName, const UsersMap* channelUsers, const InvitedMap* invitedUsers) {
	NetworkMessage msg;
	msg.addByte(0xAC);

	msg.add<uint16_t>(channelId);
	msg.addString(channelName);

	if (version >= 910) {
		addChannelUserNames(msg, channelUsers);
		addChannelUserNames(msg, invitedUsers);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelMessage(const std::string &author, const std::string &text, SpeakClasses type, uint16_t channel) {
	uint8_t clientType = static_cast<uint8_t>(type);
	if (isCipsoft860Profile(protocolProfile)) {
		clientType = translateCipsoft860SpeakClassToClient(type);
		if (clientType == cipsoft860TalkNone) {
			return;
		}
	}

	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);
	msg.addString(author);
	msg.add<uint16_t>(0x00);
	msg.addByte(clientType);
	msg.add<uint16_t>(channel);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendLootContainers() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC0);
	msg.addByte(player->quickLootFallbackToMainContainer ? 1 : 0);

	std::map<ObjectCategory_t, std::pair<std::shared_ptr<Container>, std::shared_ptr<Container>>> managedContainersMap;
	for (const auto &[category, containersPair] : player->m_managedContainers) {
		if (containersPair.first && !containersPair.first->isRemoved()) {
			managedContainersMap[category].first = containersPair.first;
		}
		if (containersPair.second && !containersPair.second->isRemoved()) {
			managedContainersMap[category].second = containersPair.second;
		}
	}

	auto msgPosition = msg.getBufferPosition();
	msg.skipBytes(1);
	uint8_t containers = 0;
	for (const auto &[category, containersPair] : managedContainersMap) {
		if (!isValidObjectCategory(category)) {
			continue;
		}
		containers++;
		msg.addByte(category);
		uint16_t lootContainerId = containersPair.first ? containersPair.first->getID() : 0;
		uint16_t obtainContainerId = containersPair.second ? containersPair.second->getID() : 0;
		msg.add<uint16_t>(lootContainerId);
		msg.add<uint16_t>(obtainContainerId);
	}
	msg.setBufferPosition(msgPosition);
	msg.addByte(containers);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendLootStats(const std::shared_ptr<Item> &item, uint8_t count) {
	if (!item) {
		return;
	}

	if (oldProtocol) {
		item->setIsLootTrackeable(false);
		return;
	}

	std::shared_ptr<Item> lootedItem = nullptr;
	lootedItem = item->clone();
	lootedItem->setItemCount(count);

	NetworkMessage msg;
	msg.addByte(0xCF);
	AddItem(msg, lootedItem);
	msg.addString(lootedItem->getName());
	item->setIsLootTrackeable(false);
	writeToOutputBuffer(msg);

	lootedItem = nullptr;
}

void ProtocolGame::sendShop(const std::shared_ptr<Npc> &npc) {
	Benchmark brenchmark;
	NetworkMessage msg;
	msg.addByte(0x7A);
	msg.addString(npc->getName());

	if (!oldProtocol) {
		msg.add<uint16_t>(npc->getCurrency());
		msg.addString(std::string()); // Currency name
	}

	const auto &shoplist = npc->getShopItemVector(player->getGUID());
	const bool useCipsoft860ShopLayout = isCipsoft860Profile(protocolProfile);
	const auto shopItemLimit = useCipsoft860ShopLayout ? std::numeric_limits<uint8_t>::max() : std::numeric_limits<uint16_t>::max();
	uint16_t itemsToSend = static_cast<uint16_t>(std::min<size_t>(shoplist.size(), shopItemLimit));
	if (useCipsoft860ShopLayout) {
		msg.addByte(static_cast<uint8_t>(itemsToSend));
	} else {
		msg.add<uint16_t>(itemsToSend);
	}

	// Initialize before the loop to avoid database overload on each iteration
	auto talkactionHidden = player->kv()->get("npc-shop-hidden-sell-item");
	// Initialize the inventoryMap outside the loop to avoid creation on each iteration
	std::map<uint16_t, uint16_t> inventoryMap;
	player->getAllSaleItemIdAndCount(inventoryMap);
	uint16_t i = 0;
	for (const ShopBlock &shopBlock : shoplist) {
		if (++i > itemsToSend) {
			break;
		}

		// Hidden sell items from the shop if they are not in the player's inventory
		if (talkactionHidden && talkactionHidden->get<bool>()) {
			const auto &foundItem = inventoryMap.find(shopBlock.itemId);
			if (foundItem == inventoryMap.end() && shopBlock.itemSellPrice > 0 && shopBlock.itemBuyPrice == 0) {
				AddHiddenShopItem(msg);
				continue;
			}
		}

		AddShopItem(msg, shopBlock);
	}

	writeToOutputBuffer(msg);
	g_logger().debug("ProtocolGame::sendShop - Time: {} ms, shop items: {}", brenchmark.duration(), shoplist.size());
}

void ProtocolGame::sendCloseShop() {
	NetworkMessage msg;
	msg.addByte(0x7C);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSay(const std::shared_ptr<Creature> &creature, SpeakClasses type, const std::string &text, const Position* pos /* = nullptr*/) {
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);

	msg.addString(creature->getName());

	if (!oldProtocol) {
		msg.addByte(0x00); // Show (Traded)
	}

	// Add level only for players
	if (auto speaker = creature->getPlayer()) {
		msg.add<uint16_t>(speaker->getLevel());
	} else {
		msg.add<uint16_t>(0x00);
	}

	uint8_t clientType = static_cast<uint8_t>(type);
	if (isCipsoft860Profile(protocolProfile)) {
		clientType = translateCipsoft860SpeakClassToClient(type);
		if (clientType == cipsoft860TalkNone) {
			return;
		}
	} else if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		clientType = TALKTYPE_MONSTER_SAY;
	}

	msg.addByte(clientType);

	if (pos) {
		msg.addPosition(*pos);
	} else {
		msg.addPosition(creature->getPosition());
	}

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendToChannel(const std::shared_ptr<Creature> &creature, SpeakClasses type, const std::string &text, uint16_t channelId) {
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (!creature) {
		msg.add<uint32_t>(0x00);
		if (!oldProtocol && statementId != 0) {
			msg.addByte(0x00); // Show (Traded)
		}
	} else if (type == TALKTYPE_CHANNEL_R2) {
		msg.add<uint32_t>(0x00);
		if (!oldProtocol && statementId != 0) {
			msg.addByte(0x00); // Show (Traded)
		}
		type = TALKTYPE_CHANNEL_R1;
	} else {
		msg.addString(creature->getName());
		if (!oldProtocol && statementId != 0) {
			msg.addByte(0x00); // Show (Traded)
		}

		// Add level only for players
		if (auto speaker = creature->getPlayer()) {
			msg.add<uint16_t>(speaker->getLevel());
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	uint8_t clientType = static_cast<uint8_t>(type);
	if (isCipsoft860Profile(protocolProfile)) {
		clientType = translateCipsoft860SpeakClassToClient(type);
		if (clientType == cipsoft860TalkNone) {
			return;
		}
	} else if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		clientType = TALKTYPE_CHANNEL_O;
	}

	msg.addByte(clientType);

	msg.add<uint16_t>(channelId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPrivateMessage(const std::shared_ptr<Player> &speaker, SpeakClasses type, const std::string &text) {
	NetworkMessage msg;
	msg.addByte(0xAA);
	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (speaker) {
		msg.addString(speaker->getName());
		if (!oldProtocol && statementId != 0) {
			msg.addByte(0x00); // Show (Traded)
		}
		msg.add<uint16_t>(speaker->getLevel());
	} else {
		msg.add<uint32_t>(0x00);
		if (!oldProtocol && statementId != 0) {
			msg.addByte(0x00); // Show (Traded)
		}
	}

	uint8_t clientType = static_cast<uint8_t>(type);
	if (isCipsoft860Profile(protocolProfile)) {
		clientType = translateCipsoft860SpeakClassToClient(type);
		if (clientType == cipsoft860TalkNone) {
			return;
		}
	} else if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		clientType = TALKTYPE_PRIVATE_TO;
	}

	msg.addByte(clientType);

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyCreatureUpdate(const std::shared_ptr<Creature> &target) {
	if (!player || oldProtocol) {
		return;
	}

	bool known;
	uint32_t removedKnown = 0;
	uint32_t cid = target->getID();
	checkCreatureAsKnown(cid, known, removedKnown);

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(cid);
	msg.addByte(0); // creature update
	AddCreature(msg, target, known, removedKnown);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyCreatureShield(const std::shared_ptr<Creature> &target) {
	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x91);
	msg.add<uint32_t>(cid);
	msg.addByte(player->getPartyShield(target->getPlayer()));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyCreatureSkull(const std::shared_ptr<Creature> &target) {
	if (g_game().getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x90);
	msg.add<uint32_t>(cid);
	msg.addByte(player->getSkullClient(target));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyCreatureHealth(const std::shared_ptr<Creature> &target, uint8_t healthPercent) {
	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8C);
	msg.add<uint32_t>(cid);
	msg.addByte(std::min<uint8_t>(100, healthPercent));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyPlayerMana(const std::shared_ptr<Player> &target, uint8_t manaPercent) {
	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
	}

	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(cid);
	msg.addByte(11); // mana percent
	msg.addByte(std::min<uint8_t>(100, manaPercent));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyCreatureShowStatus(const std::shared_ptr<Creature> &target, bool showStatus) {
	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
	}

	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(cid);
	msg.addByte(12); // show status
	msg.addByte((showStatus ? 0x01 : 0x00));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPartyPlayerVocation(const std::shared_ptr<Player> &target) {
	if (!target) {
		return;
	}

	uint32_t cid = target->getID();
	if (!knownCreatureSet.contains(cid)) {
		sendPartyCreatureUpdate(target);
		return;
	}

	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(cid);
	msg.addByte(13); // vocation
	msg.addByte(target->getVocation()->getClientId());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, const std::shared_ptr<Item> &item, uint16_t maxlen, bool canWrite) {
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	AddItem(msg, item);

	if (canWrite) {
		msg.add<uint16_t>(maxlen);
		msg.addString(item->getAttribute<std::string>(ItemAttribute_t::TEXT));
	} else {
		const std::string &text = item->getAttribute<std::string>(ItemAttribute_t::TEXT);
		msg.add<uint16_t>(text.size());
		msg.addString(text);
	}

	const std::string &writer = item->getAttribute<std::string>(ItemAttribute_t::WRITER);
	if (!writer.empty()) {
		msg.addString(writer);
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!oldProtocol) {
		msg.addByte(0x00); // Show (Traded)
	}

	auto writtenDate = item->getAttribute<time_t>(ItemAttribute_t::DATE);
	if (writtenDate != 0) {
		msg.addString(formatDateShort(writtenDate));
	} else {
		msg.add<uint16_t>(0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string &text) {
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	AddItem(msg, itemId, 1, 0);
	msg.add<uint16_t>(text.size());
	msg.addString(text);
	msg.add<uint16_t>(0x00);

	if (!oldProtocol) {
		msg.addByte(0x00); // Show (Traded)
	}

	msg.add<uint16_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus) {
	if (oldProtocol && newStatus == VipStatus_t::Training) {
		return;
	}

	NetworkMessage msg;
	if (oldProtocol && version < 980) {
		msg.addByte(newStatus == VipStatus_t::Offline ? 0xD4 : 0xD3);
		msg.add<uint32_t>(guid);
		writeToOutputBuffer(msg);
		return;
	}

	msg.addByte(0xD3);
	msg.add<uint32_t>(guid);
	msg.addByte(enumToValue(newStatus));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIP(uint32_t guid, const std::string &name, const std::string &description, uint32_t icon, bool notify, VipStatus_t status) {
	if (oldProtocol && status == VipStatus_t::Training) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xD2);
	msg.add<uint32_t>(guid);
	msg.addString(name);

	if (oldProtocol && version <= 860) {
		msg.addByte(notify ? 0x01 : 0x00);
		writeToOutputBuffer(msg);
		return;
	}

	msg.addString(description);
	msg.add<uint32_t>(std::min<uint32_t>(10, icon));
	msg.addByte(notify ? 0x01 : 0x00);
	msg.addByte(enumToValue(status));

	const auto &vipGuidGroups = player->vip().getGroupsIdGuidBelongs(guid);

	if (!oldProtocol) {
		msg.addByte(vipGuidGroups.size()); // vipGroups
		for (const auto &vipGroupID : vipGuidGroups) {
			msg.addByte(vipGroupID);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIPGroups() {
	if (oldProtocol) {
		return;
	}

	const auto &vipGroups = player->vip().getGroups();

	NetworkMessage msg;
	msg.addByte(0xD4);
	msg.addByte(vipGroups.size()); // vipGroups.size()
	for (const auto &vipGroup : vipGroups) {
		msg.addByte(vipGroup->id);
		msg.addString(vipGroup->name);
		msg.addByte(vipGroup->customizable ? 0x01 : 0x00); // 0x00 = not Customizable, 0x01 = Customizable
	}
	msg.addByte(player->vip().getMaxGroupEntries() - vipGroups.size()); // max vip groups

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendModalWindow(const ModalWindow &modalWindow) {
	if (!player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xFA);

	msg.add<uint32_t>(modalWindow.id);
	msg.addString(modalWindow.title);
	msg.addString(modalWindow.message);

	msg.addByte(modalWindow.buttons.size());
	for (const auto &it : modalWindow.buttons) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.choices.size());
	for (const auto &it : modalWindow.choices) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.defaultEscapeButton);
	msg.addByte(modalWindow.defaultEnterButton);
	msg.addByte(modalWindow.priority ? 0x01 : 0x00);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMessageDialog(const std::string &message) {
	NetworkMessage msg;
	msg.addByte(0xED);
	msg.addByte(0x14); // Unknown type
	msg.addString(message);
	writeToOutputBuffer(msg);
}

// tile
void ProtocolGame::sendKillTrackerUpdate(const std::shared_ptr<Container> &corpse, const std::string &name, const Outfit_t creatureOutfit) {
	if (oldProtocol) {
		return;
	}

	bool isCorpseEmpty = corpse->empty();

	NetworkMessage msg;
	msg.addByte(0xD1);
	msg.addString(name);
	msg.add<uint16_t>(creatureOutfit.lookType ? creatureOutfit.lookType : 21);
	msg.addByte(creatureOutfit.lookType ? creatureOutfit.lookHead : 0x00);
	msg.addByte(creatureOutfit.lookType ? creatureOutfit.lookBody : 0x00);
	msg.addByte(creatureOutfit.lookType ? creatureOutfit.lookLegs : 0x00);
	msg.addByte(creatureOutfit.lookType ? creatureOutfit.lookFeet : 0x00);
	msg.addByte(creatureOutfit.lookType ? creatureOutfit.lookAddons : 0x00);
	msg.addByte(isCorpseEmpty ? 0 : corpse->size());

	if (!isCorpseEmpty) {
		for (const auto &it : corpse->getItemList()) {
			AddItem(msg, it);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateSupplyTracker(const std::shared_ptr<Item> &item) {
	if (oldProtocol || !player || !item) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xCE);
	msg.add<uint16_t>(item->getID());

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateInputAnalyzer(CombatType_t type, int32_t amount, const std::string &target) {
	if (!player || oldProtocol) {
		return;
	}

	auto clientElement = getCipbiaElement(type);
	if (clientElement == CIPBIA_ELEMENTAL_UNDEFINED) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xCC);
	msg.addByte(ANALYZER_DAMAGE_RECEIVED);
	msg.add<uint32_t>(amount);
	msg.addByte(clientElement);
	msg.addString(target);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddHiddenShopItem(NetworkMessage &msg) {
	// Empty bytes from AddShopItem
	msg.add<uint16_t>(0);
	msg.addByte(0);
	msg.addString(std::string());
	msg.add<uint32_t>(0);
	msg.add<uint32_t>(0);
	msg.add<uint32_t>(0);
}

void ProtocolGame::AddShopItem(NetworkMessage &msg, const ShopBlock &shopBlock) {
	// Sends the item information empty if the player doesn't have the storage to buy/sell a certain item
	if (shopBlock.itemStorageKey != 0 && player->getStorageValue(shopBlock.itemStorageKey) < shopBlock.itemStorageValue) {
		AddHiddenShopItem(msg);
		return;
	}

	const ItemType &it = Item::items[shopBlock.itemId];
	msg.add<uint16_t>(shopBlock.itemId);
	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(static_cast<uint8_t>(shopBlock.itemSubType));
	} else {
		msg.addByte(0x00);
	}

	// If not send "itemName" variable from the npc shop, will registered the name that is in items.xml
	if (shopBlock.itemName.empty()) {
		msg.addString(it.name);
	} else {
		msg.addString(shopBlock.itemName);
	}
	msg.add<uint32_t>(it.weight);
	msg.add<uint32_t>(shopBlock.itemBuyPrice == 4294967295 ? 0 : shopBlock.itemBuyPrice);
	msg.add<uint32_t>(shopBlock.itemSellPrice == 4294967295 ? 0 : shopBlock.itemSellPrice);
}
