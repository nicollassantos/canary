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
#include "io/iologindata.hpp"
#include "io/iomarket.hpp"
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

void ProtocolGame::AddItem(NetworkMessage &msg, uint16_t id, uint8_t count, uint8_t tier) const {
	const ItemType &it = Item::items[id];

	msg.add<uint16_t>(it.id);

	if (oldProtocol) {
		msg.addByte(0xFF);
	}

	if (it.stackable) {
		msg.addByte(count);
	}

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(count);
	}

	if (oldProtocol) {
		if (it.animationType == ANIMATION_RANDOM) {
			msg.addByte(0xFE);
		} else if (it.animationType == ANIMATION_DESYNC) {
			msg.addByte(0xFF);
		}

		// OTCR Features
		if (isOTCR) {
			msg.addString(""); // g_game.enableFeature(GameItemShader)
		}
		return;
	}

	if (it.isContainer()) {
		msg.addByte(0x00);
	}

	if (it.isPodium) {
		msg.add<uint16_t>(0);
		msg.add<uint16_t>(0);
		msg.add<uint16_t>(0);

		msg.addByte(2);
		msg.addByte(0x01);
	}

	if (it.upgradeClassification > 0) {
		msg.addByte(tier);
	}

	if (it.expire || it.expireStop || it.clockExpire) {
		msg.add<uint32_t>(it.decayTime);
		msg.addByte(0x01); // Brand-new
	}

	if (it.wearOut) {
		msg.add<uint32_t>(it.charges);
		msg.addByte(0x01); // Brand-new
	}

	if (it.isWrapKit && !oldProtocol) {
		msg.add<uint16_t>(0x00);
	}

	// OTCR Features
	if (isOTCR) {
		msg.addString(""); // g_game.enableFeature(GameItemShader)
	}
}

void ProtocolGame::AddItem(NetworkMessage &msg, const std::shared_ptr<Item> &item) {
	if (!item) {
		return;
	}

	const ItemType &it = Item::items[item->getID()];

	msg.add<uint16_t>(it.id);

	if (oldProtocol) {
		msg.addByte(0xFF);
	}

	if (it.stackable) {
		msg.addByte(static_cast<uint8_t>(std::min<uint16_t>(std::numeric_limits<uint8_t>::max(), item->getItemCount())));
	}

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(item->getAttribute<uint8_t>(ItemAttribute_t::FLUIDTYPE));
	}

	if (oldProtocol) {
		if (it.animationType == ANIMATION_RANDOM) {
			msg.addByte(0xFE);
		} else if (it.animationType == ANIMATION_DESYNC) {
			msg.addByte(0xFF);
		}

		// OTCR Features
		if (isOTCR) {
			msg.addString(item->getShader()); // g_game.enableFeature(GameItemShader)
		}
		return;
	}

	const auto &container = item->getContainer();
	if (it.isContainer() && container) {
		ContainerSpecial_t containerType = container->getSpecialCategory(player);
		msg.addByte(enumToValue(containerType));
		switch (containerType) {
			case ContainerSpecial_t::LootHighlight:
				break;
			case ContainerSpecial_t::Manager: {
				auto [lootFlags, obtainFlags] = container->getObjectCategoryFlags(player);
				msg.add<uint32_t>(lootFlags);
				msg.add<uint32_t>(obtainFlags);
				break;
			}
			case ContainerSpecial_t::ContentCounter: {
				auto ammoTotal = container->getAmmoAmount(player);
				msg.add<uint32_t>(ammoTotal);
				break;
			}
			case ContainerSpecial_t::QuiverLoot: {
				auto ammoTotal = container->getAmmoAmount(player);
				auto [lootFlags, obtainFlags] = container->getObjectCategoryFlags(player);
				msg.add<uint32_t>(lootFlags);
				msg.add<uint32_t>(ammoTotal);
				msg.add<uint32_t>(obtainFlags);
				break;
			}
			default:
				break;
		}
	}

	if (it.isPodium) {
		const auto podiumVisible = item->getCustomAttribute("PodiumVisible");
		const auto lookType = item->getCustomAttribute("LookType");
		const auto lookTypeAttribute = item->getCustomAttribute("LookTypeEx");
		const auto lookMount = item->getCustomAttribute("LookMount");
		const auto lookDirection = item->getCustomAttribute("LookDirection");

		if (lookType && lookType->getAttribute<uint16_t>() != 0) {
			addOutfitAndMountBytes(msg, item, lookType, "LookHead", "LookBody", "LookLegs", "LookFeet", true);
		} else if (lookTypeAttribute) {
			auto lookTypeEx = lookTypeAttribute->getAttribute<uint16_t>();
			// "Tantugly's Head" boss have to send other looktype to the podium
			if (lookTypeEx == 35105) {
				lookTypeEx = 39003;
			}
			msg.add<uint16_t>(0);
			msg.add<uint16_t>(lookTypeEx);
		} else {
			msg.add<uint16_t>(0);
			msg.add<uint16_t>(0);
		}

		if (lookMount) {
			addOutfitAndMountBytes(msg, item, lookMount, "LookMountHead", "LookMountBody", "LookMountLegs", "LookMountFeet");
		} else {
			msg.add<uint16_t>(0);
		}

		msg.addByte(lookDirection ? lookDirection->getAttribute<uint8_t>() : 2);
		msg.addByte(podiumVisible ? podiumVisible->getAttribute<uint8_t>() : 0x01);
	}

	if (item->getClassification() > 0) {
		msg.addByte(item->getTier());
	}

	// Timer
	if (it.expire || it.expireStop || it.clockExpire) {
		if (item->hasAttribute(ItemAttribute_t::DURATION)) {
			msg.add<uint32_t>(item->getDuration() / 1000);
			msg.addByte((item->getDuration() / 1000) == it.decayTime ? 0x01 : 0x00); // Brand-new
		} else {
			msg.add<uint32_t>(it.decayTime);
			msg.addByte(0x01); // Brand-new
		}
	}

	// Charge
	if (it.wearOut) {
		if (item->getSubType() == 0) {
			msg.add<uint32_t>(it.charges);
			msg.addByte(0x01); // Brand-new
		} else {
			msg.add<uint32_t>(static_cast<uint32_t>(item->getSubType()));
			msg.addByte(item->getSubType() == it.charges ? 0x01 : 0x00); // Brand-new
		}
	}

	if (it.isWrapKit && !oldProtocol) {
		uint16_t unWrapId = item->getCustomAttribute("unWrapId") ? static_cast<uint16_t>(item->getCustomAttribute("unWrapId")->getInteger()) : 0;
		if (unWrapId != 0) {
			msg.add<uint16_t>(unWrapId);
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	// OTCR Features
	if (isOTCR) {
		msg.addString(item->getShader()); // g_game.enableFeature(GameItemShader)
	}
}

void ProtocolGame::sendLoginChallenge() {
	auto output = OutputMessagePool::getOutputMessage();
	static std::random_device rd;
	static std::ranlux24 generator(rd());
	static std::uniform_int_distribution<uint16_t> randNumber(0x00, 0xFF);

	// Skip checksum
	output->skipBytes(sizeof(uint32_t));

	// Packet length & type
	output->addByte(0x01);
	output->addByte(0x1F);
	// Add timestamp & random number
	challengeTimestamp = static_cast<uint32_t>(time(nullptr));
	output->add<uint32_t>(challengeTimestamp);

	challengeRandom = randNumber(generator);
	output->addByte(challengeRandom);
	output->addByte(0x71);

	// Go back and write checksum
	output->skipBytes(-12);
	// To support 11.10-, not have problems with 11.11+
	output->add<uint32_t>(adlerChecksum(output->getOutputBuffer() + sizeof(uint32_t), 8));

	send(output);
}

void ProtocolGame::GetTileDescription(const std::shared_ptr<Tile> &tile, NetworkMessage &msg) {
	if (oldProtocol) {
		msg.add<uint16_t>(0x00); // Env effects
	}

	int32_t count;
	std::shared_ptr<Item> ground = tile->getGround();
	if (ground) {
		AddItem(msg, ground);
		count = 1;
	} else {
		count = 0;
	}

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			AddItem(msg, *it);

			count++;
			if (count == 9 && tile->getPosition() == player->getPosition()) {
				break;
			} else if (count == 10) {
				return;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		bool playerAdded = false;
		for (auto creature : std::ranges::reverse_view(*creatures)) {
			if (!creature || creature->isRemoved() || !creature->isAlive()) {
				continue;
			}
			if (!player->canSeeCreature(creature)) {
				continue;
			}

			if (tile->getPosition() == player->getPosition() && count == 9 && !playerAdded) {
				creature = player;
			}

			if (creature->getID() == player->getID()) {
				playerAdded = true;
			}

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);

			if (++count == 10) {
				return;
			}
		}
	}

	if (items) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			AddItem(msg, *it);

			if (++count == 10) {
				return;
			}
		}
	}
}

void ProtocolGame::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, NetworkMessage &msg) {
	int32_t skip = -1;
	int32_t startz, endz, zstep;

	if (z > MAP_INIT_SURFACE_LAYER) {
		startz = z - MAP_LAYER_VIEW_LIMIT;
		endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, z + MAP_LAYER_VIEW_LIMIT);
		zstep = 1;
	} else {
		startz = MAP_INIT_SURFACE_LAYER;
		endz = 0;
		zstep = -1;
	}

	for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
		GetFloorDescription(msg, x, y, nz, width, height, z - nz, skip);
	}

	if (skip >= 0) {
		msg.addByte(skip);
		msg.addByte(0xFF);
	}
}

void ProtocolGame::GetFloorDescription(NetworkMessage &msg, int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, int32_t offset, int32_t &skip) {
	for (int32_t nx = 0; nx < width; nx++) {
		for (int32_t ny = 0; ny < height; ny++) {
			const auto &tile = g_game().map.getTile(static_cast<uint16_t>(x + nx + offset), static_cast<uint16_t>(y + ny + offset), static_cast<uint8_t>(z));
			if (tile) {
				if (skip >= 0) {
					msg.addByte(skip);
					msg.addByte(0xFF);
				}

				skip = 0;
				GetTileDescription(tile, msg);
			} else if (skip == 0xFE) {
				msg.addByte(0xFF);
				msg.addByte(0xFF);
				skip = -1;
			} else {
				++skip;
			}
		}
	}
}

void ProtocolGame::sendSessionEndInformation(SessionEndInformations information) {
	if (!oldProtocol) {
		auto output = OutputMessagePool::getOutputMessage();
		output->addByte(0x18);
		output->addByte(information);
		send(output);
	}
	disconnect();
}

void ProtocolGame::sendItemInspection(uint16_t itemId, uint8_t itemCount, const std::shared_ptr<Item> &item, bool cyclopedia) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x76);
	msg.addByte(0x00);
	msg.addByte(cyclopedia ? 0x01 : 0x00);
	msg.add<uint32_t>(player->getID()); // 13.00 Creature ID
	msg.addByte(0x01);

	const ItemType &it = Item::items[itemId];

	if (item) {
		msg.addString(item->getName());
		AddItem(msg, item);
	} else {
		msg.addString(it.name);
		AddItem(msg, it.id, itemCount, 0);
	}
	msg.addByte(0);

	auto descriptions = Item::getDescriptions(it, item);
	msg.addByte(descriptions.size());
	for (const auto &description : descriptions) {
		msg.addString(description.first);
		msg.addString(description.second);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHighscoresNoData() {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xB1);
	msg.addByte(0x01); // No data available
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHighscores(const std::vector<HighscoreCharacter> &characters, uint8_t categoryId, uint32_t vocationId, uint16_t page, uint16_t pages, uint32_t updateTimer) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xB1);
	msg.addByte(0x00); // All data available

	msg.addByte(1); // Worlds
	auto serverName = g_configManager().getString(SERVER_NAME);
	msg.addString(serverName); // First World
	msg.addString(serverName); // Selected World

	msg.addByte(0); // Game World Category: 0xFF(-1) - Selected World
	msg.addByte(0); // BattlEye World Type

	auto vocationPosition = msg.getBufferPosition();
	uint8_t vocations = 1;

	msg.skipBytes(1); // Vocation Count
	msg.add<uint32_t>(0xFFFFFFFF); // All Vocations - hardcoded
	msg.addString("(all)"); // All Vocations - hardcoded

	uint32_t selectedVocation = 0xFFFFFFFF;
	const auto vocationsMap = g_vocations().getVocations();
	for (const auto &it : vocationsMap) {
		const auto &vocation = it.second;
		auto fromVoc = vocation->getFromVocation();
		auto loopVocationId = vocation->getId();
		if (fromVoc == static_cast<uint32_t>(loopVocationId)) {
			auto vocClientId = vocation->getClientId();
			msg.add<uint32_t>(vocClientId); // Vocation Id
			msg.addString(vocation->getVocName()); // Vocation Name
			++vocations;
			if (vocClientId == vocationId) {
				selectedVocation = vocClientId;
			}
		}
	}
	msg.add<uint32_t>(selectedVocation); // Selected Vocation

	uint8_t selectedCategory = 0;
	const auto &highscoreCategories = g_game().getHighscoreCategories();
	msg.addByte(highscoreCategories.size()); // Category Count
	for (const HighscoreCategory &category : highscoreCategories) {
		msg.addByte(category.m_id); // Category Id
		msg.addString(category.m_name); // Category Name
		if (category.m_id == categoryId) {
			selectedCategory = categoryId;
		}
	}
	msg.addByte(selectedCategory); // Selected Category

	msg.add<uint16_t>(page); // Current page
	msg.add<uint16_t>(pages); // Pages

	msg.addByte(characters.size()); // Character Count
	for (const HighscoreCharacter &character : characters) {
		msg.add<uint32_t>(character.rank); // Rank
		msg.addString(character.name); // Character Name
		msg.addString(character.loyaltyTitle); // Character Loyalty Title
		msg.addByte(character.vocation); // Vocation Id
		msg.addString(serverName); // World
		msg.add<uint16_t>(character.level); // Level
		msg.addByte((player->getGUID() == character.id)); // Player Indicator Boolean
		msg.add<uint64_t>(character.points); // Points
	}

	msg.addByte(0xFF); // ??
	msg.addByte(0); // ??
	msg.addByte(1); // ??
	msg.add<uint32_t>(updateTimer); // Last Update
	msg.setBufferPosition(vocationPosition);
	msg.addByte(vocations);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBestiaryEntryChanged(uint16_t raceid) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xD9);
	msg.add<uint16_t>(raceid);
	writeToOutputBuffer(msg);
}

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

void ProtocolGame::sendBestiaryCharms() {
	if (!player || oldProtocol) {
		return;
	}

	const auto playerLevel = player->getLevel();
	uint64_t resetAllCharmsCost = 100000 + (playerLevel > 100 ? playerLevel * 11000 : 0);
	if (player->hasCharmExpansion()) {
		resetAllCharmsCost = (resetAllCharmsCost * 75) / 100;
	}

	NetworkMessage msg;
	msg.addByte(0xD8);
	msg.add<uint64_t>(resetAllCharmsCost);

	const auto &charmList = g_game().getCharmList();
	msg.addByte(charmList.size());
	for (const auto &c_type : charmList) {
		msg.addByte(c_type->id);
		if (g_iobestiary().hasCharmUnlockedRuneBit(c_type, player->getUnlockedRunesBit())) {
			const auto charmTier = player->getCharmTier(c_type->id);
			msg.addByte(charmTier);
			uint16_t raceId = player->parseRacebyCharm(c_type->id);
			if (raceId > 0) {
				msg.addByte(0x01);
				msg.add<uint16_t>(raceId);
				uint32_t removeCharmCost = player->getLevel() * 100;
				if (player->hasCharmExpansion()) {
					removeCharmCost = (removeCharmCost * 75) / 100;
				}
				msg.add<uint32_t>(removeCharmCost);
			} else {
				msg.addByte(0x00);
			}
		} else {
			msg.addByte(0x00);
			msg.addByte(0x00);
		}
	}

	std::list<charmRune_t> usedCharms = g_iobestiary().getCharmUsedRuneBitAll(player);
	uint8_t availableCharmSlots;
	if (player->isPremium() && player->hasCharmExpansion()) {
		availableCharmSlots = 0xFF;
	} else {
		uint8_t totalCharmSlots = player->isPremium() ? 6 : 2;
		availableCharmSlots = totalCharmSlots - usedCharms.size();
	}

	msg.addByte(availableCharmSlots);

	auto finishedMonstersSet = g_iobestiary().getBestiaryStageTwo(player);
	std::unordered_map<uint16_t, uint8_t> charmsAssigned;
	for (charmRune_t charmRune : usedCharms) {
		const auto &tmpCharm = g_iobestiary().getBestiaryCharm(charmRune);
		if (!tmpCharm) {
			continue;
		}

		uint16_t tmpRaceId = player->parseRacebyCharm(tmpCharm->id);
		charmsAssigned[tmpRaceId]++;
	}

	for (const auto &[raceId, amount] : charmsAssigned) {
		if (amount >= 2) {
			std::erase(finishedMonstersSet, raceId);
		}
	}

	msg.add<uint16_t>(finishedMonstersSet.size());
	for (uint16_t tmpRaceId : finishedMonstersSet) {
		msg.add<uint32_t>(tmpRaceId);
	}

	writeToOutputBuffer(msg);
	sendCharmResourcesBalance(
		player->getCharmPoints(),
		player->getMinorCharmEchoes(),
		player->getMaxCharmPoints(),
		player->getMaxMinorCharmEchoes()
	);
}

void ProtocolGame::sendOpenPrivateChannel(const std::string &receiver) {
	NetworkMessage msg;
	msg.addByte(0xAD);
	msg.addString(receiver);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendExperienceTracker(int64_t rawExp, int64_t finalExp) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAF);
	msg.add<int64_t>(rawExp);
	msg.add<int64_t>(finalExp);
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

void ProtocolGame::sendCreatureOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit) {
	if (!canSee(creature)) {
		return;
	}

	Outfit_t newOutfit = outfit;
	if (player->isWearingSupportOutfit()) {
		player->setCurrentMount(0);
		newOutfit.lookMount = 0;
	}

	NetworkMessage msg;
	msg.addByte(0x8E);
	msg.add<uint32_t>(creature->getID());
	AddOutfit(msg, newOutfit);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureLight(const std::shared_ptr<Creature> &creature) {
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	AddCreatureLight(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureIcon(const std::shared_ptr<Creature> &creature) {
	if (!creature || !player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(creature->getID());
	// Type 14 for this
	msg.addByte(14);
	addCreatureIcon(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWorldLight(const LightInfo &lightInfo) {
	NetworkMessage msg;
	AddWorldLight(msg, lightInfo);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTibiaTime(int32_t time) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xEF);
	msg.addByte(time / 60);
	msg.addByte(time % 60);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureWalkthrough(const std::shared_ptr<Creature> &creature, bool walkthrough) {
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x92);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(walkthrough ? 0x00 : 0x01);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureShield(const std::shared_ptr<Creature> &creature) {
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x91);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getPartyShield(creature->getPlayer()));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureEmblem(const std::shared_ptr<Creature> &creature) {
	if (!creature || !canSee(creature) || oldProtocol) {
		return;
	}

	auto tile = creature->getTile();
	if (!tile) {
		return;
	}

	// Remove creature from client and re-add to update
	Position pos = creature->getPosition();
	int32_t stackpos = tile->getClientIndexOfCreature(player, creature);
	sendRemoveTileThing(pos, stackpos);
	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
	AddCreature(msg, creature, false, creature->getID());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSkull(const std::shared_ptr<Creature> &creature) {
	if (g_game().getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x90);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getSkullClient(creature));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureType(const std::shared_ptr<Creature> &creature, uint8_t creatureType) {
	NetworkMessage msg;
	msg.addByte(0x95);
	msg.add<uint32_t>(creature->getID());
	if (creatureType == CREATURETYPE_SUMMON_OTHERS) {
		creatureType = CREATURETYPE_SUMMON_PLAYER;
	}
	msg.addByte(creatureType); // type or any byte idk
	if (!oldProtocol && creatureType == CREATURETYPE_SUMMON_PLAYER) {
		std::shared_ptr<Creature> master = creature->getMaster();
		if (master) {
			msg.add<uint32_t>(master->getID());
		} else {
			msg.add<uint32_t>(0);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSquare(const std::shared_ptr<Creature> &creature, SquareColor_t color) {
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x93);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(0x01);
	msg.addByte(color);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTutorial(uint8_t tutorialId) {
	NetworkMessage msg;
	msg.addByte(0xDC);
	msg.addByte(tutorialId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddMarker(const Position &pos, uint8_t markType, const std::string &desc) {
	NetworkMessage msg;
	msg.addByte(0xDD);

	if (!oldProtocol) {
		msg.addByte(enumToValue(CyclopediaMapData_t::MinimapMarker));
	}

	msg.addPosition(pos);
	msg.addByte(markType);
	msg.addString(desc);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterNoData(CyclopediaCharacterInfoType_t characterInfoType, uint8_t errorCode) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(static_cast<uint8_t>(characterInfoType));
	msg.addByte(errorCode);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterBaseInformation() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_BASEINFORMATION);
	msg.addByte(0x00);
	msg.addString(player->getName());
	msg.addString(player->getVocation()->getVocName());
	msg.add<uint16_t>(player->getLevel());
	AddOutfit(msg, player->getDefaultOutfit(), false);

	msg.addByte(0x01); // Store summary & Character titles
	msg.addString(player->title().getCurrentTitleName()); // character title
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterGeneralStats() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_GENERALSTATS);
	// Send no error
	// 1: No data available at the moment.
	// 2: You are not allowed to see this character's data.
	// 3: You are not allowed to inspect this character.
	msg.addByte(0x00); // 0x00 Here means 'no error'

	msg.add<uint64_t>(player->getExperience());
	msg.add<uint16_t>(player->getLevel());
	msg.addByte(player->getLevelPercent());
	msg.add<uint16_t>(player->getBaseXpGain()); // BaseXPGainRate
	msg.add<uint16_t>(player->getDisplayGrindingXpBoost()); // LowLevelBonus
	msg.add<uint16_t>(player->getDisplayXpBoostPercent()); // XPBoost
	msg.add<uint16_t>(player->getStaminaXpBoost()); // StaminaMultiplier(100=x1.0)
	msg.add<uint16_t>(player->getXpBoostTime()); // xpBoostRemainingTime
	msg.addByte(player->getXpBoostTime() > 0 ? 0x00 : 0x01); // canBuyXpBoost
	msg.add<uint32_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint32_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint32_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint32_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));
	msg.addByte(player->getSoul());
	msg.add<uint16_t>(player->getStaminaMinutes());

	std::shared_ptr<Condition> condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	msg.add<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);
	msg.add<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);
	msg.add<uint16_t>(player->getSpeed());
	msg.add<uint16_t>(player->getBaseSpeed());
	msg.add<uint32_t>(player->getCapacity());
	msg.add<uint32_t>(player->getBaseCapacity());
	msg.add<uint32_t>(player->hasFlag(PlayerFlags_t::HasInfiniteCapacity) ? 1000000 : player->getFreeCapacity());
	msg.addByte(8);
	msg.addByte(1);
	msg.add<uint16_t>(player->getMagicLevel());
	msg.add<uint16_t>(player->getBaseMagicLevel());
	msg.add<uint16_t>(player->getLoyaltyMagicLevel());
	msg.add<uint16_t>(player->getMagicLevelPercent() * 100);

	for (uint8_t i = SKILL_FIRST; i < SKILL_CRITICAL_HIT_CHANCE; ++i) {
		static const uint8_t HardcodedSkillIds[] = { 11, 9, 8, 10, 7, 6, 13 };
		const auto skill = static_cast<skills_t>(i);
		msg.addByte(HardcodedSkillIds[i]);
		msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(skill), std::numeric_limits<uint16_t>::max()));
		msg.add<uint16_t>(player->getBaseSkill(skill));
		msg.add<uint16_t>(player->getLoyaltySkill(skill));
		msg.add<uint16_t>(player->getSkillPercent(skill) * 100);
	}

	auto bufferPosition = msg.getBufferPosition();
	msg.skipBytes(1);
	uint8_t total = 0;
	for (size_t i = 0; i < COMBAT_COUNT; i++) {
		auto specializedMagicLevel = player->getSpecializedMagicLevel(indexToCombatType(i));
		if (specializedMagicLevel > 0) {
			++total;
			msg.addByte(getCipbiaElement(indexToCombatType(i)));
			msg.add<uint16_t>(specializedMagicLevel);
		}
	}
	msg.setBufferPosition(bufferPosition);
	msg.addByte(total);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterRecentDeaths(uint16_t page, uint16_t pages, const std::vector<RecentDeathEntry> &entries) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_RECENTDEATHS);
	msg.addByte(0x00); // 0x00 Here means 'no error'
	msg.add<uint16_t>(page);
	msg.add<uint16_t>(pages);
	msg.add<uint16_t>(entries.size());
	for (const RecentDeathEntry &entry : entries) {
		msg.add<uint32_t>(entry.timestamp);
		msg.addString(entry.cause);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterRecentPvPKills(uint16_t page, uint16_t pages, const std::vector<RecentPvPKillEntry> &entries) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_RECENTPVPKILLS);
	msg.addByte(0x00); // 0x00 Here means 'no error'
	msg.add<uint16_t>(page);
	msg.add<uint16_t>(pages);
	msg.add<uint16_t>(entries.size());
	for (const RecentPvPKillEntry &entry : entries) {
		msg.add<uint32_t>(entry.timestamp);
		msg.addString(entry.description);
		msg.addByte(entry.status);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterAchievements(uint16_t secretsUnlocked, const std::vector<std::pair<Achievement, uint32_t>> &achievementsUnlocked) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_ACHIEVEMENTS);
	msg.addByte(0x00); // 0x00 Here means 'no error'
	msg.add<uint16_t>(player->achiev().getPoints());
	msg.add<uint16_t>(secretsUnlocked);
	msg.add<uint16_t>(static_cast<uint16_t>(achievementsUnlocked.size()));
	for (const auto &[achievement, addedTimestamp] : achievementsUnlocked) {
		msg.add<uint16_t>(achievement.id);
		msg.add<uint32_t>(addedTimestamp);
		if (achievement.secret) {
			msg.addByte(0x01);
			msg.addString(achievement.name);
			msg.addString(achievement.description);
			msg.addByte(achievement.grade);
		} else {
			msg.addByte(0x00);
		}
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterItemSummary(const ItemsTierCountList &inventoryItems, const ItemsTierCountList &storeInboxItems, const StashItemList &stashItems, const ItemsTierCountList &depotBoxItems, const ItemsTierCountList &inboxItems) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_ITEMSUMMARY);
	msg.addByte(0x00); // 0x00 Here means 'no error'

	auto writeItemList = [&](const ItemsTierCountList &items, uint16_t &itemCount) {
		const auto startPosition = msg.getBufferPosition();
		msg.skipBytes(2);

		for (const auto &[key, count] : items) {
			const auto &[itemID, tier] = key;

			const ItemType &it = Item::items[itemID];
			msg.add<uint16_t>(itemID);

			if (it.upgradeClassification > 0) {
				msg.addByte(tier);
			}

			msg.add<uint32_t>(count);
			++itemCount;
		}

		const auto endPosition = msg.getBufferPosition();
		msg.setBufferPosition(startPosition);
		msg.add<uint16_t>(itemCount);
		msg.setBufferPosition(endPosition);
	};

	// Inventory Items
	uint16_t inventoryItemsCount = 0;
	writeItemList(inventoryItems, inventoryItemsCount);

	// Store Inbox Items
	uint16_t storeInboxItemsCount = 0;
	writeItemList(storeInboxItems, storeInboxItemsCount);

	// Supply Stash Items
	msg.add<uint16_t>(stashItems.size());
	for (const auto &[itemId, itemCount] : stashItems) {
		msg.add<uint16_t>(itemId); // Item ID
		const ItemType &it = Item::items[itemId];
		if (it.upgradeClassification > 0) {
			msg.addByte(0x00);
		}
		msg.add<uint32_t>(itemCount); // Item count
	}

	// Depot Box Items
	uint16_t depotBoxItemsCount = 0;
	writeItemList(depotBoxItems, depotBoxItemsCount);

	// Inbox Items
	uint16_t inboxItemsCount = 0;
	writeItemList(inboxItems, inboxItemsCount);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterOutfitsMounts() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITSMOUNTS);
	msg.addByte(0x00);
	Outfit_t currentOutfit = player->getDefaultOutfit();

	uint16_t outfitSize = 0;
	auto startOutfits = msg.getBufferPosition();
	msg.skipBytes(2);

	const auto outfits = Outfits::getInstance().getOutfits(player->getSex());
	for (const auto &outfit : outfits) {
		uint8_t addons;
		if (!player->getOutfitAddons(outfit, addons)) {
			continue;
		}
		const std::string from = outfit->from;
		++outfitSize;

		msg.add<uint16_t>(outfit->lookType);
		msg.addString(outfit->name);
		msg.addByte(addons);
		if (from == "store") {
			msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_STORE);
		} else if (from == "quest") {
			msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_QUEST);
		} else {
			msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_NONE);
		}
		if (outfit->lookType == currentOutfit.lookType) {
			msg.add<uint32_t>(1000);
		} else {
			msg.add<uint32_t>(0);
		}
	}
	if (outfitSize > 0) {
		msg.addByte(currentOutfit.lookHead);
		msg.addByte(currentOutfit.lookBody);
		msg.addByte(currentOutfit.lookLegs);
		msg.addByte(currentOutfit.lookFeet);
	}

	uint16_t mountSize = 0;
	auto startMounts = msg.getBufferPosition();
	msg.skipBytes(2);
	for (const auto &mount : g_game().mounts->getMounts()) {
		const std::string type = mount->type;
		if (player->hasMount(mount)) {
			++mountSize;

			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
			if (type == "store") {
				msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_STORE);
			} else if (type == "quest") {
				msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_QUEST);
			} else {
				msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_NONE);
			}
			msg.add<uint32_t>(1000);
		}
	}
	if (mountSize > 0) {
		msg.addByte(currentOutfit.lookMountHead);
		msg.addByte(currentOutfit.lookMountBody);
		msg.addByte(currentOutfit.lookMountLegs);
		msg.addByte(currentOutfit.lookMountFeet);
	}

	uint16_t familiarsSize = 0;
	auto startFamiliars = msg.getBufferPosition();
	msg.skipBytes(2);
	const auto familiars = Familiars::getInstance().getFamiliars(player->getVocationId());
	for (const auto &familiar : familiars) {
		const std::string type = familiar->type;
		if (!player->getFamiliar(familiar)) {
			continue;
		}
		++familiarsSize;
		msg.add<uint16_t>(familiar->lookType);
		msg.addString(familiar->name);
		if (type == "quest") {
			msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_QUEST);
		} else {
			msg.addByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_NONE);
		}
		msg.add<uint32_t>(0);
	}

	msg.setBufferPosition(startOutfits);
	msg.add<uint16_t>(outfitSize);
	msg.setBufferPosition(startMounts);
	msg.add<uint16_t>(mountSize);
	msg.setBufferPosition(startFamiliars);
	msg.add<uint16_t>(familiarsSize);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterStoreSummary() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_STORESUMMARY);
	msg.addByte(0x00); // 0x00 Here means 'no error'
	msg.add<uint32_t>(player->getXpBoostTime()); // Remaining Store Xp Boost Time
	auto remaining = player->kv()->get("daily-reward-xp-boost");
	msg.add<uint32_t>(remaining ? static_cast<uint32_t>(remaining->getNumber()) : 0); // Remaining Daily Reward Xp Boost Time

	auto cyclopediaSummary = player->cyclopedia().getSummary();

	msg.addByte(static_cast<uint8_t>(magic_enum::enum_count<Blessings>()));
	for (auto bless : magic_enum::enum_values<Blessings>()) {
		std::string name = toStartCaseWithSpace(magic_enum::enum_name(bless).data());
		msg.addString(name);
		auto blessValue = enumToValue(bless);
		if (player->hasBlessing(blessValue)) {
			msg.addByte(static_cast<uint16_t>(player->blessings[blessValue - 1]));
		} else {
			msg.addByte(0x00);
		}
	}

	uint8_t preySlotsUnlocked = 0;
	// Prey third slot unlocked
	if (const auto &slotP = player->getPreySlotById(PreySlot_Three);
	    slotP && slotP->state != PreyDataState_Locked) {
		preySlotsUnlocked++;
	}
	// Task hunting third slot unlocked
	if (const auto &slotH = player->getTaskHuntingSlotById(PreySlot_Three);
	    slotH && slotH->state != PreyTaskDataState_Locked) {
		preySlotsUnlocked++;
	}
	msg.addByte(preySlotsUnlocked); // getPreySlotById + getTaskHuntingSlotById

	msg.addByte(cyclopediaSummary.m_preyWildcards); // getPreyCardsObtained
	msg.addByte(cyclopediaSummary.m_instantRewards); // getRewardCollectionObtained
	msg.addByte(player->hasCharmExpansion() ? 0x01 : 0x00);
	msg.addByte(cyclopediaSummary.m_hirelings); // getHirelingsObtained

	std::vector<uint16_t> m_hSkills;
	for (const auto &[skillId, skillName] : g_game().getHirelingSkills()) {
		if (player->kv()->scoped("hireling-skills")->get(skillName)) {
			[[maybe_unused]] auto &skill_ref = m_hSkills.emplace_back(skillId);
			g_logger().debug("skill id: {}, name: {}", skillId, skillName);
		}
	}
	msg.addByte(m_hSkills.size());
	for (const auto &id : m_hSkills) {
		msg.addByte(id - 1000);
	}

	/*std::vector<uint16_t> m_hOutfits;
	for (const auto &it : g_game().getHirelingOutfits()) {
	    if (player->kv()->scoped("hireling-outfits")->get(it.second)) {
	        m_hOutfits.emplace_back(it.first);
	        g_logger().debug("outfit id: {}, name: {}", it.first, it.second);
	    }
	}
	msg.addByte(m_hOutfits.size());
	for (const auto &id : m_hOutfits) {
	    msg.addByte(0x01); // TODO need to get the correct id from hireling outfit
	}*/
	msg.addByte(0x00); // hireling outfit size

	auto houseItems = player->cyclopedia().getResult(static_cast<uint8_t>(Summary_t::HOUSE_ITEMS));
	msg.add<uint16_t>(houseItems.size());
	for (const auto &[itemId, count] : houseItems) {
		const ItemType &it = Item::items[itemId];
		msg.add<uint16_t>(it.id); // Item ID
		msg.addString(it.name);
		msg.addByte(count);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterInspection() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_INSPECTION);
	msg.addByte(0x00);
	uint8_t inventoryItems = 0;
	auto startInventory = msg.getBufferPosition();
	msg.skipBytes(1);
	for (std::underlying_type<Slots_t>::type slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; slot++) {
		std::shared_ptr<Item> inventoryItem = player->getInventoryItem(static_cast<Slots_t>(slot));
		if (inventoryItem) {
			++inventoryItems;

			msg.addByte(slot);
			msg.addString(inventoryItem->getName());
			AddItem(msg, inventoryItem);

			uint8_t itemImbuements = 0;
			auto startImbuements = msg.getBufferPosition();
			msg.skipBytes(1);
			for (uint8_t slotid = 0; slotid < inventoryItem->getImbuementSlot(); slotid++) {
				ImbuementInfo imbuementInfo;
				if (!inventoryItem->getImbuementInfo(slotid, &imbuementInfo)) {
					continue;
				}

				msg.add<uint16_t>(imbuementInfo.imbuement->getIconID());
				itemImbuements++;
			}

			auto endImbuements = msg.getBufferPosition();
			msg.setBufferPosition(startImbuements);
			msg.addByte(itemImbuements);
			msg.setBufferPosition(endImbuements);

			auto descriptions = Item::getDescriptions(Item::items[inventoryItem->getID()], inventoryItem);
			msg.addByte(descriptions.size());
			for (const auto &description : descriptions) {
				msg.addString(description.first);
				msg.addString(description.second);
			}
		}
	}
	msg.addString(player->getName());
	AddOutfit(msg, player->getDefaultOutfit(), false);

	// Player overall summary
	uint8_t playerDescriptionSize = 0;
	auto playerDescriptionPosition = msg.getBufferPosition();
	msg.skipBytes(1);

	// Player title
	if (player->title().getCurrentTitle() != 0) {
		playerDescriptionSize++;
		msg.addString("Character Title");
		msg.addString(player->title().getCurrentTitleName());
	}

	// Level description
	playerDescriptionSize++;
	msg.addString("Level");
	msg.addString(std::to_string(player->getLevel()));

	// Vocation description
	playerDescriptionSize++;
	msg.addString("Vocation");
	msg.addString(player->getVocation()->getVocName());

	// Loyalty title
	if (!player->getLoyaltyTitle().empty()) {
		playerDescriptionSize++;
		msg.addString("Loyalty Title");
		msg.addString(player->getLoyaltyTitle());
	}

	// Marriage description
	if (const auto spouseId = player->getMarriageSpouse(); spouseId > 0) {
		if (const auto &spouse = g_game().getPlayerByID(spouseId, true); spouse) {
			playerDescriptionSize++;
			msg.addString("Married to");
			msg.addString(spouse->getName());
		}
	}

	// Prey description
	for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
		if (const auto &slot = player->getPreySlotById(static_cast<PreySlot_t>(slotId));
		    slot && slot->isOccupied()) {
			playerDescriptionSize++;
			std::string activePrey = fmt::format("Active Prey {}", slotId + 1);
			msg.addString(activePrey);

			std::string desc;
			if (auto mtype = g_monsters().getMonsterTypeByRaceId(slot->selectedRaceId)) {
				desc.append(mtype->name);
			} else {
				desc.append("Unknown creature");
			}

			if (slot->bonus == PreyBonus_Damage) {
				desc.append(" (Improved Damage +");
			} else if (slot->bonus == PreyBonus_Defense) {
				desc.append(" (Improved Defense +");
			} else if (slot->bonus == PreyBonus_Experience) {
				desc.append(" (Improved Experience +");
			} else if (slot->bonus == PreyBonus_Loot) {
				desc.append(" (Improved Loot +");
			}
			desc.append(fmt::format("{}%, remaining", slot->bonusPercentage));
			uint8_t hours = slot->bonusTimeLeft / 3600;
			uint8_t minutes = (slot->bonusTimeLeft - (hours * 3600)) / 60;
			desc.append(fmt::format("{}:{}{}h", hours, (minutes < 10 ? "0" : ""), minutes));
			msg.addString(desc);
		}
	}

	// Outfit description
	playerDescriptionSize++;
	msg.addString("Outfit");
	if (const auto outfit = Outfits::getInstance().getOutfitByLookType(player, player->getDefaultOutfit().lookType)) {
		msg.addString(outfit->name);
	} else {
		msg.addString("unknown");
	}

	msg.setBufferPosition(startInventory);
	msg.addByte(inventoryItems);

	msg.setBufferPosition(playerDescriptionPosition);
	msg.addByte(playerDescriptionSize);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterBadges() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_BADGES);
	msg.addByte(0x00);
	msg.addByte(0x01); // ShowAccountInformation, if 0x01 will show IsOnline, IsPremium, character title, badges

	const auto loggedPlayer = g_game().getPlayerByName(player->getName());
	msg.addByte(loggedPlayer ? 0x01 : 0x00); // IsOnline
	msg.addByte(player->isPremium() ? 0x01 : 0x00); // IsPremium (GOD has always 'Premium')
	// Character loyalty title
	msg.addString(player->getLoyaltyTitle());

	uint8_t badgesSize = 0;
	auto badgesSizePosition = msg.getBufferPosition();
	msg.skipBytes(1);
	for (const auto &badge : g_game().getBadges()) {
		if (player->badge().hasBadge(badge.m_id)) {
			msg.add<uint32_t>(badge.m_id);
			msg.addString(badge.m_name);
			badgesSize++;
		}
	}

	msg.setBufferPosition(badgesSizePosition);
	msg.addByte(badgesSize);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterTitles() {
	if (!player || oldProtocol) {
		return;
	}

	auto titles = g_game().getTitles();

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_TITLES);
	msg.addByte(0x00); // 0x00 Here means 'no error'
	msg.addByte(player->title().getCurrentTitle());
	msg.addByte(static_cast<uint8_t>(titles.size()));
	for (const auto &title : titles) {
		msg.addByte(title.m_id);
		auto titleName = player->title().getNameBySex(player->getSex(), title.m_maleName, title.m_femaleName);
		msg.addString(titleName);
		msg.addString(title.m_description);
		msg.addByte(title.m_permanent ? 0x01 : 0x00);
		auto isUnlocked = player->title().isTitleUnlocked(title.m_id);
		msg.addByte(isUnlocked ? 0x01 : 0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterOffenceStats() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_OFFENCESTATS);
	msg.addByte(0x00); // 0x00 Here means 'no error'

	const auto &skilsEquipment = player->getSkillsEquipment();

	addCyclopediaCriticalSkill(player, msg, SKILL_CRITICAL_HIT_CHANCE, skilsEquipment[SKILL_CRITICAL_HIT_CHANCE]);
	addCyclopediaCriticalSkill(player, msg, SKILL_CRITICAL_HIT_DAMAGE, skilsEquipment[SKILL_CRITICAL_HIT_DAMAGE]);

	addCyclopediaSkills(player, msg, SKILL_LIFE_LEECH_AMOUNT, skilsEquipment[SKILL_LIFE_LEECH_AMOUNT]);
	addCyclopediaSkills(player, msg, SKILL_MANA_LEECH_AMOUNT, skilsEquipment[SKILL_MANA_LEECH_AMOUNT]);

	const double onslaughtTotal = getForgeSkillStat(CONST_SLOT_LEFT);
	const double onslaughtEquipment = getForgeSkillStat(CONST_SLOT_LEFT, false);
	msg.addDouble(onslaughtTotal); // Onslaught Total
	msg.addDouble(onslaughtEquipment);
	msg.addDouble(onslaughtTotal - onslaughtEquipment);
	msg.addDouble(0.0); // NOTHING

	msg.addDouble(player->getCleavePercent() / 100.);

	// Perfect shot range (12.70)
	for (uint8_t range = 1; range <= 7; range++) {
		msg.add<uint16_t>(static_cast<uint16_t>(player->getPerfectShotDamage(range)));
	}

	const auto flatBonus = player->calculateFlatDamageHealing();
	uint16_t flatDamageHealingUnknown = 0;
	msg.add<uint16_t>(flatBonus); // Flat Damage and Healing Total
	msg.add<uint16_t>(flatBonus);
	msg.add<uint16_t>(flatDamageHealingUnknown);

	const auto &weapon = player->getWeapon();
	if (weapon) {
		const ItemType &it = Item::items[weapon->getID()];
		if (it.weaponType == WEAPON_WAND) {
			msg.add<uint16_t>(it.maxHitChance);
			msg.add<uint16_t>(0);
			msg.add<uint16_t>(0);
			msg.addByte(0x00);
			msg.add<uint16_t>(0);
			msg.add<uint16_t>(0);
			msg.addByte(getCipbiaElement(it.combatType));
			msg.addDouble(0.0);
			msg.addByte(0x00);
			msg.addByte(0x00);
		} else if (it.weaponType == WEAPON_DISTANCE || it.weaponType == WEAPON_AMMO || it.weaponType == WEAPON_MISSILE) {
			int32_t physicalAttack = std::max<int32_t>(0, weapon->getAttack());
			physicalAttack += player->weaponProficiency().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE);
			int32_t elementalAttack = 0;
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				elementalAttack = std::max<int32_t>(0, it.abilities->elementDamage);
			}
			int32_t attackValue = physicalAttack + elementalAttack;
			if (it.weaponType == WEAPON_AMMO) {
				std::shared_ptr<Item> weaponItem = player->getWeapon(true);
				if (weaponItem) {
					attackValue += weaponItem->getAttack();
				}
			}

			int32_t distanceValue = player->getSkillLevel(SKILL_DISTANCE);
			int32_t attackSkill = player->getDistanceAttackSkill(distanceValue, attackValue);
			const auto attackRawTotal = player->attackRawTotal(flatBonus, attackValue, distanceValue);
			const auto attackTotal = player->attackTotal(flatBonus, attackValue, distanceValue);

			msg.add<uint16_t>(attackTotal);
			msg.add<uint16_t>(flatBonus);
			msg.add<uint16_t>(static_cast<uint16_t>(attackValue));
			msg.addByte(0x07);
			msg.add<uint16_t>(attackSkill);
			msg.add<uint16_t>(attackTotal - attackRawTotal);
			msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

			// Converted Damage
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				if (physicalAttack) {
					msg.addDouble(elementalAttack / static_cast<double>(attackValue));
				} else {
					msg.addDouble(0.0);
				}
				msg.addByte(getCipbiaElement(it.abilities->elementType));
			} else {
				handleImbuementDamage(msg, player);
			}

			const auto distanceAccuracy = player->getDamageAccuracy(it);
			const auto distanceAccuracySize = distanceAccuracy.size();
			msg.addByte(distanceAccuracy.size());
			for (uint8_t i = 0; i < distanceAccuracySize; ++i) {
				msg.addByte(i + 1);
				msg.addDouble(distanceAccuracy[i] / 100.);
			}
		} else {
			int32_t physicalAttack = std::max<int32_t>(0, weapon->getAttack());
			physicalAttack += player->weaponProficiency().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE);
			int32_t elementalAttack = 0;
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				elementalAttack = std::max<int32_t>(0, it.abilities->elementDamage);
			}
			int32_t weaponAttack = physicalAttack + elementalAttack;
			int32_t weaponSkill = player->getWeaponSkill(weapon);
			int32_t attackSkill = player->getAttackSkill(weapon);
			uint8_t skillId = player->getWeaponSkillId(weapon);
			const auto attackRawTotal = player->attackRawTotal(flatBonus, weaponAttack, weaponSkill);
			const auto attackTotal = player->attackTotal(flatBonus, weaponAttack, weaponSkill);

			msg.add<uint16_t>(attackTotal);
			msg.add<uint16_t>(flatBonus);
			msg.add<uint16_t>(static_cast<uint16_t>(weaponAttack));
			msg.addByte(skillId);
			msg.add<uint16_t>(attackSkill);
			msg.add<uint16_t>(attackTotal - attackRawTotal);
			msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

			// Converted Damage
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				if (physicalAttack) {
					msg.addDouble(elementalAttack / static_cast<double>(weaponAttack));
				} else {
					msg.addDouble(0);
				}
				msg.addByte(getCipbiaElement(it.abilities->elementType));
			} else {
				handleImbuementDamage(msg, player);
			}
			msg.addByte(0x00);
		}
	} else {
		uint16_t attackValue = 7;
		int32_t fistValue = player->getSkillLevel(SKILL_FIST);
		int32_t attackSkill = player->getDistanceAttackSkill(fistValue, attackValue);
		const auto attackRawTotal = player->attackRawTotal(flatBonus, attackValue, fistValue);
		const auto attackTotal = player->attackTotal(flatBonus, attackValue, fistValue);

		msg.add<uint16_t>(attackTotal);
		msg.add<uint16_t>(flatBonus);
		msg.add<uint16_t>(attackValue);
		msg.addByte(11);
		msg.add<uint16_t>(attackSkill);
		msg.add<uint16_t>(attackTotal - attackRawTotal);
		msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

		msg.addDouble(0.0);
		msg.addByte(0x00);
		msg.addByte(0x00);
	}

	msg.addDouble(player->weaponProficiency().getPowerfulFoeDamage()); // Influenced/Bosses damage

	const auto &runesCritical = player->weaponProficiency().getRunesCritical();
	const auto &autoAttackCritical = player->weaponProficiency().getAutoAttackCritical();

	const auto &bestiariesDamage = player->weaponProficiency().getActiveBestiariesDamage();
	msg.add<uint16_t>(bestiariesDamage.size()); // Bestiary Damage size
	for (const auto &[name, amount] : bestiariesDamage) {
		msg.addString(name); // Bestiary category name
		msg.addDouble(amount); // Amount
	}

	const auto &elementalCriticalChanceOpt = player->weaponProficiency().getActiveElementalCriticalType(WeaponProficiencyBonus_t::ELEMENTAL_HIT_CHANCE);
	msg.addByte(elementalCriticalChanceOpt.has_value()); // Has Element Critical Chance
	if (elementalCriticalChanceOpt.has_value()) {
		msg.addByte(elementalCriticalChanceOpt.value().first); // Element Critical Chance Element ID
		msg.addDouble(elementalCriticalChanceOpt.value().second); // Applied Element Critical Chance Value
	}

	msg.addDouble(runesCritical.chance); // Offensive Runes Critical Chance
	msg.addDouble(autoAttackCritical.chance); // Auto Attack Critical Chance

	const auto &elementalCriticalDamageOpt = player->weaponProficiency().getActiveElementalCriticalType(WeaponProficiencyBonus_t::ELEMENTAL_CRITICAL_EXTRA_DAMAGE);
	msg.addByte(elementalCriticalDamageOpt.has_value()); // Has Element Critical Damage
	if (elementalCriticalDamageOpt.has_value()) {
		msg.addByte(elementalCriticalDamageOpt.value().first); // Element Critical Damage Element ID
		msg.addDouble(elementalCriticalDamageOpt.value().second); // Applied Element Critical Damage Value
	}

	msg.addDouble(runesCritical.damage); // Offensive Runes Critical Damage
	msg.addDouble(autoAttackCritical.damage); // Auto Attack Critical Damage

	msg.add<uint16_t>(player->weaponProficiency().getStat(WeaponProficiencyBonus_t::LIFE_GAIN_ON_HIT)); // Life Gain on Hit
	msg.add<uint16_t>(player->weaponProficiency().getStat(WeaponProficiencyBonus_t::MANA_GAIN_ON_HIT)); // Mana Gain on Hit
	msg.add<uint16_t>(player->weaponProficiency().getStat(WeaponProficiencyBonus_t::LIFE_GAIN_ON_KILL)); // Life Gain on Kill
	msg.add<uint16_t>(player->weaponProficiency().getStat(WeaponProficiencyBonus_t::MANA_GAIN_ON_KILL)); // Mana Gain on Kill

	skills_t skill = SKILL_NONE;
	if (weapon) {
		switch (Item::items[weapon->getID()].type) {
			case ITEM_TYPE_SWORD:
				skill = SKILL_SWORD;
				break;
			case ITEM_TYPE_AXE:
				skill = SKILL_AXE;
				break;
			case ITEM_TYPE_CLUB:
				skill = SKILL_CLUB;
				break;
			case ITEM_TYPE_WAND:
				skill = SKILL_MAGLEVEL;
				break;
			case ITEM_TYPE_DISTANCE:
				skill = SKILL_DISTANCE;
				break;
			default:
				break;
		}
	}

	const auto &skillPercentage = player->weaponProficiency().getSkillPercentage(skill);

	double playerSkill = 0.0;
	if (skillPercentage.skill != SKILL_NONE) {
		playerSkill = skillPercentage.skill == SKILL_MAGLEVEL ? player->getMagicLevel() : player->getSkillLevel(skillPercentage.skill);
	}

	bool hasAutoAttackSkill = skillPercentage.skill != SKILL_NONE && skillPercentage.autoAttack;
	msg.addByte(hasAutoAttackSkill); // Has Auto Attack Skill
	if (hasAutoAttackSkill) {
		msg.addByte(static_cast<uint8_t>(getCipbiaSkill(skillPercentage.skill))); // Auto Attack Skill ID
		msg.addDouble(skillPercentage.autoAttack); // Percent Auto Attack Skill
		msg.addDouble(std::round(playerSkill * skillPercentage.autoAttack)); // Applied Auto Attack Value
	}

	bool hasSpellDamage = skillPercentage.skill != SKILL_NONE && skillPercentage.spellDamage;
	msg.addByte(hasSpellDamage); // Has Spell Damage
	if (hasSpellDamage) {
		msg.addByte(static_cast<uint8_t>(getCipbiaSkill(skillPercentage.skill))); // Spell Damage Skill ID
		msg.addDouble(skillPercentage.spellDamage); // Percent Spell Damage
		msg.addDouble(std::round(playerSkill * skillPercentage.spellDamage)); // Applied Spell Damage Value
	}

	bool hasSpellHealing = skillPercentage.skill != SKILL_NONE && skillPercentage.spellHealing;
	msg.addByte(hasSpellHealing); // Has Spell Healing
	if (hasSpellHealing) {
		msg.addByte(static_cast<uint8_t>(getCipbiaSkill(skillPercentage.skill))); // Spell Healing Skill ID
		msg.addDouble(skillPercentage.spellHealing); // Percent Spell Healing Skill
		msg.addDouble(std::round(playerSkill * skillPercentage.spellHealing)); // Applied Spell Healing Value
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterDefenceStats() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_DEFENCESTATS);
	msg.addByte(0x00); // 0x00 Here means 'no error'

	const double dodgeTotal = getForgeSkillStat(CONST_SLOT_ARMOR) + player->wheel().getStat(WheelStat_t::DODGE);
	msg.addDouble(dodgeTotal);
	msg.addDouble(getForgeSkillStat(CONST_SLOT_ARMOR, false));
	msg.addDouble(getForgeSkillStat(CONST_SLOT_ARMOR) - getForgeSkillStat(CONST_SLOT_ARMOR, false));
	msg.addDouble(0.00);
	msg.addDouble(player->wheel().getStat(WheelStat_t::DODGE));

	msg.add<uint32_t>(player->getMagicShieldCapacityFlat() * (1 + player->getMagicShieldCapacityPercent()));
	msg.add<uint16_t>(static_cast<uint16_t>(player->getMagicShieldCapacityFlat())); // Direct bonus
	msg.addDouble(player->getMagicShieldCapacityPercent()); // Percentage bonus

	msg.add<uint16_t>(static_cast<uint16_t>(player->getReflectFlat(COMBAT_PHYSICALDAMAGE)));

	msg.add<uint16_t>(player->getArmor());
	msg.add<uint16_t>(player->getMantra());

	const auto shieldingSkill = player->getSkillLevel(SKILL_SHIELD);
	const uint16_t defenseWheel = player->wheel().getMajorStatConditional("Combat Mastery", WheelMajor_t::DEFENSE);
	msg.add<uint16_t>(player->getDefense(true));
	msg.add<uint16_t>(player->getDefenseEquipment());
	msg.addByte(0x06);
	msg.add<uint16_t>(shieldingSkill);
	msg.add<uint16_t>(defenseWheel);
	msg.add<uint16_t>(0);

	const auto wheelMultiplier = player->wheel().getMitigationMultiplier();
	msg.addDouble(player->getMitigation() / 100.);
	msg.addDouble(0.0);
	msg.addDouble(player->getDefenseEquipment() / 10000.);
	msg.addDouble(player->getSkillLevel(SKILL_SHIELD) * player->getVocation()->mitigationFactor / 10000.);
	msg.addDouble(wheelMultiplier / 100.);
	msg.addDouble(player->getCombatTacticsMitigation());

	// Store the "combats" to increase in absorb values function and send to client later
	uint8_t combats = 0;
	auto startCombats = msg.getBufferPosition();
	msg.skipBytes(1);

	// Calculate and parse the combat absorbs values
	calculateAbsorbValues(player, msg, combats);

	// Now set the buffer position skiped and send the total combats count
	auto endCombats = msg.getBufferPosition();
	msg.setBufferPosition(startCombats);
	msg.addByte(combats);
	msg.setBufferPosition(endCombats);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCyclopediaCharacterMiscStats() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xDA);
	msg.addByte(CYCLOPEDIA_CHARACTERINFO_MISCSTATS);
	msg.addByte(0x00); // 0x00 Here means 'no error'

	const double momentumTotal = getForgeSkillStat(CONST_SLOT_HEAD) + player->wheel().getBonusData().momentum;
	msg.addDouble(momentumTotal);
	msg.addDouble(getForgeSkillStat(CONST_SLOT_HEAD, false));
	msg.addDouble(getForgeSkillStat(CONST_SLOT_HEAD) - getForgeSkillStat(CONST_SLOT_HEAD, false));
	msg.addDouble(player->wheel().getBonusData().momentum);
	msg.addDouble(0.00);

	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEGS));
	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEGS), false);
	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEGS) - getForgeSkillStat(CONST_SLOT_LEGS, false));
	msg.addDouble(0.09);

	msg.addDouble(getForgeSkillStat(CONST_SLOT_FEET, false));
	msg.addDouble(getForgeSkillStat(CONST_SLOT_FEET, false));
	msg.addDouble(0.00);

	uint8_t haveBlesses = 0;
	for (auto bless : magic_enum::enum_values<Blessings>()) {
		if (bless == Blessings::TwistOfFate) {
			continue;
		}

		if (player->hasBlessing(enumToValue(bless))) {
			++haveBlesses;
		}
	}

	msg.addByte(haveBlesses);
	msg.addByte(magic_enum::enum_count<Blessings>() - 1); // Skip Twist of Fate

	auto activeConcoctions = player->getActiveConcoctions();
	for (const auto &[concoctionId, duration] : activeConcoctions) {
		if (duration == 0) {
			g_logger().error(
				"sendCyclopediaCharacterMiscStats: Player {} has concoction with itemId {} and timeLeft 0, this should not happen.",
				player->getName(), concoctionId
			);
			player->updateConcoction(concoctionId, duration);
		}
	}

	msg.addByte(activeConcoctions.size());
	for (const auto &[concoctionId, duration] : activeConcoctions) {
		msg.add<uint16_t>(concoctionId);
		msg.addByte(0x00);
		msg.addByte(0x00);
		msg.add<uint32_t>(duration);
	}

	const auto &activeFoods = player->getActiveFoods();
	msg.addByte(activeFoods.size());
	for (const auto &[foodId, duration] : activeFoods) {
		msg.add<uint16_t>(foodId);
		msg.addByte(0x00);
		msg.addByte(0x00);
		msg.add<uint32_t>(duration);
	}

	const auto &weaponProficiencyAugments = player->weaponProficiency().getActiveAugments();
	msg.addByte(weaponProficiencyAugments.size());
	for (const auto &[key, value] : weaponProficiencyAugments) {
		msg.add<uint16_t>(key.first); // Spell ID
		msg.addByte(key.second); // Augment Type
		msg.addDouble(value); // Spell Augment Value
	}

	const auto &wheelAugments = player->wheel().getActiveAugments();
	msg.addByte(wheelAugments.size());
	for (const auto &[key, value] : wheelAugments) {
		msg.add<uint16_t>(key.first); // Spell ID
		msg.addByte(key.second); // Augment Type
		msg.addDouble(value); // Spell Augment Value
	}

	const auto &equippedAugments = player->getEquippedAugments();
	msg.addByte(equippedAugments.size());
	for (const auto &[key, value] : equippedAugments) {
		msg.add<uint16_t>(key.first); // Spell ID
		msg.addByte(key.second); // Augment Type
		msg.addDouble(value); // Spell Augment Value
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendReLoginWindow(uint8_t unfairFightReduction) {
	NetworkMessage msg;
	msg.addByte(0x28);
	msg.addByte(0x00);
	msg.addByte(unfairFightReduction);
	if (!oldProtocol) {
		msg.addByte(0x00); // use death redemption (boolean)
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendStats() {
	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBasicData() {
	if (!player) {
		return;
	}

	auto vocation = player->getVocation();
	if (!vocation) {
		return;
	}

	auto vocationId = player->getVocationId();

	NetworkMessage msg;
	msg.addByte(0x9F);
	if (player->isPremium() || player->isVip()) {
		msg.addByte(1);
		msg.add<uint32_t>(getTimeNow() + ((player->getPremiumDays() + 1) * 86400));
	} else {
		msg.addByte(0);
		msg.add<uint32_t>(0);
	}
	msg.addByte(vocation->getClientId());

	// Prey window
	if (vocationId == 0 && player->getGroup()->id < GROUP_TYPE_GAMEMASTER) {
		msg.addByte(0);
	} else {
		msg.addByte(1); // has reached Main (allow player to open Prey window)
	}

	// Filter only valid ids
	std::list<uint16_t> spellsList = g_spells().getSpellsByVocation(vocationId);
	std::vector<std::shared_ptr<InstantSpell>> validSpells;
	for (uint16_t sid : spellsList) {
		auto spell = g_spells().getInstantSpellById(sid);
		if (spell && spell->getSpellId() > 0) {
			validSpells.emplace_back(spell);
		}
	}

	// Send total size of spells
	msg.add<uint16_t>(validSpells.size());
	// Send each spell valid ids
	for (const auto &spell : validSpells) {
		if (!spell) {
			continue;
		}

		// Only send valid spells to old client
		if (oldProtocol) {
			msg.addByte(spell->getSpellId());
			continue;
		}

		if (spell->isLearnable() && !player->hasLearnedInstantSpell(spell->getName())) {
			msg.add<uint16_t>(0);
		} else if (spell && spell->isLearnable() && player->hasLearnedInstantSpell(spell->getName())) {
			// Ignore spell if not have wheel grade (or send if you have)
			auto grade = player->wheel().getSpellUpgrade(spell->getName());
			if (static_cast<uint8_t>(grade) == 0) {
				msg.add<uint16_t>(0);
			} else {
				msg.add<uint16_t>(spell->getSpellId());
			}
		} else {
			msg.add<uint16_t>(spell->getSpellId());
		}
	}

	if (!oldProtocol) {
		msg.addByte(player->getVocation()->getMagicShield()); // bool - determine whether magic shield is active or not
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBlessingWindow() {
	if (!player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x9B);

	bool isRetro = g_configManager().getBoolean(TOGGLE_SERVER_IS_RETRO);

	msg.addByte(isRetro ? 0x07 : 0x08);
	for (auto blessing : magic_enum::enum_values<Blessings>()) {
		if (isRetro && blessing == Blessings::TwistOfFate) {
			continue;
		}

		const auto blessingValue = enumToValue(blessing);
		const auto blessingId = 1 << blessingValue;
		msg.add<uint16_t>(blessingId);
		msg.addByte(player->getBlessingCount(blessingValue));
		msg.addByte(player->getBlessingCount(blessingValue, true));
	}

	// Start at "The Wisdom Of Solitude"
	uint8_t blessCount = 0;
	for (auto bless : magic_enum::enum_values<Blessings>()) {
		if (bless == Blessings::TwistOfFate) {
			continue;
		}

		if (player->hasBlessing(enumToValue(bless))) {
			blessCount++;
		}
	}

	const auto isPromoted = player->isPromoted();
	const auto factor = (isRetro ? 6.31 : 8);
	const auto skillReduction = factor * blessCount;
	const auto promotionReduction = (isPromoted ? 30 : 0);
	const auto minReduction = skillReduction + promotionReduction;
	const auto maxLossPvpDeath = calculateMaxPvpReduction(blessCount, isPromoted);

	msg.addByte(isPromoted);
	msg.addByte(30); // Reduction bonus with promotion
	msg.addByte(minReduction);
	msg.addByte(isRetro ? minReduction : maxLossPvpDeath);
	msg.addByte(minReduction);

	const auto playerSkull = player->getSkull();
	const auto &playerAmulet = player->getThing(CONST_SLOT_NECKLACE);
	bool hasSkull = (playerSkull == Skulls_t::SKULL_RED || playerSkull == Skulls_t::SKULL_BLACK);
	bool usingAol = (playerAmulet && playerAmulet->getItem()->getID() == ITEM_AMULETOFLOSS);
	if (hasSkull) {
		msg.addByte(100);
		msg.addByte(100);
	} else if (usingAol) {
		msg.addByte(0);
		msg.addByte(0);
	} else {
		msg.addByte(calculateEquipmentLoss(blessCount, true));
		msg.addByte(calculateEquipmentLoss(blessCount, true));
	}

	msg.addByte(hasSkull);
	msg.addByte(usingAol);

	msg.addByte(0x00);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBlessStatus() {
	if (!player) {
		return;
	}

	// Ignore Twist of Fate (Id 1)
	uint8_t blessCount = 0;
	for (auto bless : magic_enum::enum_values<Blessings>()) {
		if (bless == Blessings::TwistOfFate) {
			continue;
		}
		if (player->hasBlessing(enumToValue(bless))) {
			blessCount++;
		}
	}

	NetworkMessage msg;
	msg.addByte(0x9C);

	if (oldProtocol) {
		msg.add<uint16_t>(blessCount >= 5 ? 0x01 : 0x00);
	} else {
		bool glow = player->getVocationId() > VOCATION_NONE && ((g_configManager().getBoolean(INVENTORY_GLOW) && blessCount >= 5) || player->getLevel() < g_configManager().getNumber(ADVENTURERSBLESSING_LEVEL));
		msg.add<uint16_t>(glow ? 1 : 0); // Show up the glowing effect in items if you have all blesses or adventurer's blessing
		msg.addByte((blessCount >= 7) ? 3 : ((blessCount >= 5) ? 2 : 1)); // 1 = Disabled | 2 = normal | 3 = green
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPremiumTrigger() {
	if (g_configManager().getBoolean(FREE_PREMIUM) || g_configManager().getBoolean(VIP_SYSTEM_ENABLED)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x9E);

	msg.addByte(16);
	for (uint16_t i = 0; i <= 15; i++) {
		// PREMIUM_TRIGGER_TRAIN_OFFLINE = false, PREMIUM_TRIGGER_XP_BOOST = false, PREMIUM_TRIGGER_MARKET = false, PREMIUM_TRIGGER_VIP_LIST = false, PREMIUM_TRIGGER_DEPOT_SPACE = false, PREMIUM_TRIGGER_INVITE_PRIVCHAT = false
		msg.addByte(0x01);
	}

	writeToOutputBuffer(msg);
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

	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(internalType);
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
			msg.addPosition(message.position);
			msg.add<uint32_t>(message.primary.value);
			msg.addByte(message.primary.color);
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

void ProtocolGame::sendClosePrivate(uint16_t channelId) {
	NetworkMessage msg;
	msg.addByte(0xB3);
	msg.add<uint16_t>(channelId);
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

	if (channelUsers) {
		msg.add<uint16_t>(channelUsers->size());
		for (const auto &it : *channelUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (invitedUsers) {
		msg.add<uint16_t>(invitedUsers->size());
		for (const auto &it : *invitedUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelMessage(const std::string &author, const std::string &text, SpeakClasses type, uint16_t channel) {
	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);
	msg.addString(author);
	msg.add<uint16_t>(0x00);
	msg.addByte(type);
	msg.add<uint16_t>(channel);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(const std::unordered_set<PlayerIcon> &iconSet, const IconBakragore iconBakragore) {
	NetworkMessage msg;
	msg.addByte(0xA2);

	std::bitset<static_cast<size_t>(PlayerIcon::Count)> iconsBitSet;
	for (const auto &icon : iconSet) {
		iconsBitSet.set(enumToValue(icon));
	}

	uint32_t icons = iconsBitSet.to_ulong();

	if (oldProtocol) {
		// Send as uint16_t in old protocol
		msg.add<uint16_t>(static_cast<uint16_t>(icons));
	} else {
		// Send as uint64_t in new protocol
		msg.add<uint64_t>(icons);
		msg.addByte(enumToValue(iconBakragore)); // Icons Bakragore
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIconBakragore(const IconBakragore icon) {
	NetworkMessage msg;
	msg.addByte(0xA2);
	msg.add<uint64_t>(0); // Send empty normal icons
	msg.addByte(enumToValue(icon));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUnjustifiedPoints(const uint8_t &dayProgress, const uint8_t &dayLeft, const uint8_t &weekProgress, const uint8_t &weekLeft, const uint8_t &monthProgress, const uint8_t &monthLeft, const uint8_t &skullDuration) {
	NetworkMessage msg;
	msg.addByte(0xB7);
	msg.addByte(dayProgress);
	msg.addByte(dayLeft);
	msg.addByte(weekProgress);
	msg.addByte(weekLeft);
	msg.addByte(monthProgress);
	msg.addByte(monthLeft);
	msg.addByte(skullDuration);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenPvpSituations(uint8_t openPvpSituations) {
	NetworkMessage msg;
	msg.addByte(0xB8);
	msg.addByte(openPvpSituations);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendContainer(uint8_t cid, const std::shared_ptr<Container> &container, bool hasParent, uint16_t firstIndex) {
	if (!player || !container) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6E);

	msg.addByte(cid);

	if (container->getID() == ITEM_BROWSEFIELD) {
		AddItem(msg, ITEM_BAG, 1, container->getTier());
		msg.addString("Browse Field");
	} else {
		AddItem(msg, container);
		msg.addString(container->getName());
	}

	const auto itemsStoreInboxToSend = container->getStoreInboxFilteredItems();

	msg.addByte(container->capacity());

	msg.addByte(hasParent ? 0x01 : 0x00);

	// Depot search
	if (!oldProtocol) {
		msg.addByte((player->isDepotSearchAvailable() && container->isInsideDepot(true)) ? 0x01 : 0x00);
	}

	msg.addByte(container->isUnlocked() ? 0x01 : 0x00); // Drag and drop
	msg.addByte(container->hasPagination() ? 0x01 : 0x00); // Pagination

	uint32_t containerSize = container->size();
	if (!itemsStoreInboxToSend.empty()) {
		containerSize = itemsStoreInboxToSend.size();
	}
	msg.add<uint16_t>(containerSize);
	msg.add<uint16_t>(firstIndex);

	uint32_t maxItemsToSend;

	if (container->hasPagination() && firstIndex > 0) {
		maxItemsToSend = std::min<uint32_t>(container->capacity(), containerSize - firstIndex);
	} else {
		maxItemsToSend = container->capacity();
	}

	const ItemDeque &itemList = container->getItemList();
	if (firstIndex >= containerSize) {
		msg.addByte(0x00);
	} else if (container->getID() == ITEM_STORE_INBOX && !itemsStoreInboxToSend.empty()) {
		msg.addByte(std::min<uint32_t>(maxItemsToSend, containerSize));
		for (const auto &item : itemsStoreInboxToSend) {
			AddItem(msg, item);
		}
	} else {
		msg.addByte(std::min<uint32_t>(maxItemsToSend, containerSize));

		uint32_t i = 0;
		for (auto it = itemList.begin() + firstIndex, end = itemList.end(); i < maxItemsToSend && it != end; ++it, ++i) {
			AddItem(msg, *it);
		}
	}

	// From here on down is for version 13.21+
	if (oldProtocol) {
		writeToOutputBuffer(msg);
		return;
	}

	if (container->isStoreInbox()) {
		const auto &categories = container->getStoreInboxValidCategories();
		const auto enumName = container->getAttribute<std::string>(ItemAttribute_t::STORE_INBOX_CATEGORY);
		auto category = magic_enum::enum_cast<ContainerCategory_t>(enumName);
		if (category.has_value()) {
			bool toSendCategory = false;
			// Check if category exist in the deque
			for (const auto &tempCategory : categories) {
				if (tempCategory == category.value()) {
					toSendCategory = true;
					g_logger().debug("found category {}", toSendCategory);
				}
			}

			if (!toSendCategory) {
				std::shared_ptr<Container> container = player->getContainerByID(cid);
				if (container) {
					container->removeAttribute(ItemAttribute_t::STORE_INBOX_CATEGORY);
				}
			}
			sendContainerCategory<ContainerCategory_t>(msg, categories, static_cast<uint8_t>(category.value()));
		} else {
			sendContainerCategory<ContainerCategory_t>(msg, categories);
		}
	} else {
		msg.addByte(0x00);
		msg.addByte(0x00);
	}

	// New container menu options
	if (container->isMovable()) { // Pickupable/Moveable (?)
		msg.addByte(1);
	} else {
		msg.addByte(0);
	}

	if (container->getHoldingPlayer()) { // Player holding the item (?)
		msg.addByte(1);
	} else {
		msg.addByte(0);
	}

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
	uint16_t itemsToSend = std::min<size_t>(shoplist.size(), std::numeric_limits<uint16_t>::max());
	msg.add<uint16_t>(itemsToSend);

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

void ProtocolGame::sendClientCheck() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x63);
	msg.add<uint32_t>(1);
	msg.addByte(1);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendGameNews() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x98);
	msg.add<uint32_t>(1); // unknown
	msg.addByte(1); //(0 = open | 1 = highlight)
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendResourcesBalance(uint64_t money /*= 0*/, uint64_t bank /*= 0*/, uint64_t preyCards /*= 0*/, uint64_t taskHunting /*= 0*/, uint64_t forgeDust /*= 0*/, uint64_t forgeSliver /*= 0*/, uint64_t forgeCores /*= 0*/) {
	sendResourceBalance(RESOURCE_BANK, bank);
	sendResourceBalance(RESOURCE_INVENTORY_MONEY, money);
	sendResourceBalance(RESOURCE_PREY_CARDS, preyCards);
	sendResourceBalance(RESOURCE_TASK_HUNTING, taskHunting);
	sendResourceBalance(RESOURCE_FORGE_DUST, forgeDust);
	sendResourceBalance(RESOURCE_FORGE_SLIVER, forgeSliver);
	sendResourceBalance(RESOURCE_FORGE_CORES, forgeCores);
}

void ProtocolGame::sendResourceBalance(Resource_t resourceType, uint64_t value) {
	if (oldProtocol && resourceType > RESOURCE_PREY_CARDS) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xEE);
	msg.addByte(resourceType);
	msg.add<uint64_t>(value);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCharmResourcesBalance(uint32_t charm /*= 0*/, uint32_t minorCharm /*= 0*/, uint32_t maxCharm /*= 0*/, uint32_t maxMinorCharm /*= 0*/) {
	sendCharmResourceBalance(RESOURCE_CHARM, charm);
	sendCharmResourceBalance(RESOURCE_MINOR_CHARM, minorCharm);
	sendCharmResourceBalance(RESOURCE_MAX_CHARM, maxCharm);
	sendCharmResourceBalance(RESOURCE_MAX_MINOR_CHARM, maxMinorCharm);
};

void ProtocolGame::sendCharmResourceBalance(CharmResource_t resourceType, uint32_t value) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xEE);

	msg.addByte(resourceType);
	msg.add<uint32_t>(value);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSaleItemList(const std::vector<ShopBlock> &shopVector, const std::map<uint16_t, uint16_t> &inventoryMap) {
	sendResourceBalance(RESOURCE_BANK, player->getBankBalance());

	uint16_t currency = player->getShopOwner() ? player->getShopOwner()->getCurrency() : static_cast<uint16_t>(ITEM_GOLD_COIN);
	if (currency == ITEM_GOLD_COIN) {
		// Since we already have full inventory map we shouldn't call getMoney here - it is simply wasting cpu power
		uint64_t playerMoney = 0;
		auto it = inventoryMap.find(ITEM_CRYSTAL_COIN);
		if (it != inventoryMap.end()) {
			playerMoney += static_cast<uint64_t>(it->second) * 10000;
		}
		it = inventoryMap.find(ITEM_PLATINUM_COIN);
		if (it != inventoryMap.end()) {
			playerMoney += static_cast<uint64_t>(it->second) * 100;
		}
		it = inventoryMap.find(ITEM_GOLD_COIN);
		if (it != inventoryMap.end()) {
			playerMoney += static_cast<uint64_t>(it->second);
		}
		sendResourceBalance(RESOURCE_INVENTORY_MONEY, playerMoney);
	} else {
		uint64_t customCurrencyValue = 0;
		auto search = inventoryMap.find(currency);
		if (search != inventoryMap.end()) {
			customCurrencyValue += static_cast<uint64_t>(search->second);
		}
		sendResourceBalance(oldProtocol ? RESOURCE_INVENTORY_MONEY : RESOURCE_INVENTORY_CURRENCY_CUSTOM, customCurrencyValue);
	}

	NetworkMessage msg;
	msg.addByte(0x7B);

	if (oldProtocol) {
		msg.add<uint64_t>(player->getMoney() + player->getBankBalance());
	}

	uint16_t itemsToSend = 0;
	const uint16_t ItemsToSendLimit = oldProtocol ? 0xFF : 0xFFFF;
	auto msgPosition = msg.getBufferPosition();
	msg.skipBytes(oldProtocol ? 1 : 2);

	for (const ShopBlock &shopBlock : shopVector) {
		if (shopBlock.itemSellPrice == 0) {
			continue;
		}

		auto it = inventoryMap.find(shopBlock.itemId);
		if (it != inventoryMap.end()) {
			msg.add<uint16_t>(shopBlock.itemId);
			if (oldProtocol) {
				msg.addByte(static_cast<uint8_t>(std::min<uint16_t>(it->second, std::numeric_limits<uint8_t>::max())));
			} else {
				msg.add<uint16_t>(std::min<uint16_t>(it->second, std::numeric_limits<uint16_t>::max()));
			}
			if (++itemsToSend >= ItemsToSendLimit) {
				break;
			}
		}
	}

	msg.setBufferPosition(msgPosition);
	if (oldProtocol) {
		msg.addByte(static_cast<uint8_t>(itemsToSend));
	} else {
		msg.add<uint16_t>(itemsToSend);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketEnter(uint32_t depotId) {
	NetworkMessage msg;
	msg.addByte(0xF6);

	if (oldProtocol) {
		msg.add<uint64_t>(player->getBankBalance());
	}

	msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(IOMarket::getPlayerOfferCount(player->getGUID()), std::numeric_limits<uint8_t>::max())));

	std::shared_ptr<DepotLocker> depotLocker = player->getDepotLocker(depotId);
	if (!depotLocker) {
		msg.add<uint16_t>(0x00);
		writeToOutputBuffer(msg);
		return;
	}

	player->setInMarket(true);

	// Only use here locker items, itemVector is for use of Game::createMarketOffer
	auto [itemVector, lockerItems] = player->requestLockerItems(depotLocker, true);
	auto totalItemsCountPosition = msg.getBufferPosition();
	msg.skipBytes(2); // Total items count

	const uint16_t entriesLimit = std::numeric_limits<uint16_t>::max();
	uint16_t entriesSent = 0;
	bool limitReached = false;
	for (const auto &[itemId, tierAndCountMap] : lockerItems) {
		for (const auto &[tier, count] : tierAndCountMap) {
			if (entriesSent >= entriesLimit) {
				limitReached = true;
				break;
			}
			msg.add<uint16_t>(itemId);
			if (!oldProtocol && Item::items[itemId].upgradeClassification > 0) {
				msg.addByte(tier);
			}
			msg.add<uint16_t>(static_cast<uint16_t>(count));
			++entriesSent;
		}
		if (limitReached) {
			break;
		}
	}

	auto endPosition = msg.getBufferPosition();
	msg.setBufferPosition(totalItemsCountPosition);
	msg.add<uint16_t>(entriesSent);
	msg.setBufferPosition(endPosition);

	writeToOutputBuffer(msg);

	updateCoinBalance();
	sendResourcesBalance(player->getMoney(), player->getBankBalance(), player->getPreyCards(), player->getTaskHuntingPoints());
}

void ProtocolGame::sendCoinBalance() {
	if (!player) {
		return;
	}

	// send is updating
	// TODO: export this to it own function
	NetworkMessage msg;
	msg.addByte(0xF2);
	msg.addByte(0x01);
	writeToOutputBuffer(msg);

	msg.reset();

	// send update
	msg.addByte(0xDF);
	msg.addByte(0x01);

	msg.add<uint32_t>(player->coinBalance); // Normal Coins
	msg.add<uint32_t>(player->coinTransferableBalance); // Transferable Coins

	if (!oldProtocol) {
		msg.add<uint32_t>(player->coinBalance); // Reserved Auction Coins
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketLeave() {
	NetworkMessage msg;
	msg.addByte(0xF7);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseItem(uint16_t itemId, const MarketOfferList &buyOffers, const MarketOfferList &sellOffers, uint8_t tier) {
	NetworkMessage msg;

	msg.addByte(0xF9);
	if (!oldProtocol) {
		msg.addByte(MARKETREQUEST_ITEM_BROWSE);
	}

	msg.add<uint16_t>(itemId);
	if (!oldProtocol && Item::items[itemId].upgradeClassification > 0) {
		msg.addByte(tier);
	}

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer &offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer &offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	updateCoinBalance();
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketAcceptOffer(const MarketOfferEx &offer) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (!oldProtocol) {
		msg.addByte(MARKETREQUEST_ITEM_BROWSE);
	}

	msg.add<uint16_t>(offer.itemId);
	if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
		msg.addByte(offer.tier);
	}

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.addString(offer.playerName);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnOffers(const MarketOfferList &buyOffers, const MarketOfferList &sellOffers) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_OFFERS);
	}

	msg.add<uint32_t>(buyOffers.size());
	for (const MarketOffer &offer : buyOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	msg.add<uint32_t>(sellOffers.size());
	for (const MarketOffer &offer : sellOffers) {
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketCancelOffer(const MarketOfferEx &offer) {
	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_OFFERS_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_OFFERS);
	}

	if (offer.type == MARKETACTION_BUY) {
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
		msg.add<uint32_t>(0x00);
	} else {
		msg.add<uint32_t>(0x00);
		msg.add<uint32_t>(0x01);
		msg.add<uint32_t>(offer.timestamp);
		msg.add<uint16_t>(offer.counter);
		msg.add<uint16_t>(offer.itemId);
		if (!oldProtocol && Item::items[offer.itemId].upgradeClassification > 0) {
			msg.addByte(offer.tier);
		}
		msg.add<uint16_t>(offer.amount);
		if (oldProtocol) {
			msg.add<uint32_t>(offer.price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(offer.price));
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMarketBrowseOwnHistory(const HistoryMarketOfferList &buyOffers, const HistoryMarketOfferList &sellOffers) {
	uint32_t i = 0;
	std::map<uint32_t, uint16_t> counterMap;
	uint32_t buyOffersToSend = std::min<uint32_t>(buyOffers.size(), 810 + std::max<int32_t>(0, 810 - sellOffers.size()));
	uint32_t sellOffersToSend = std::min<uint32_t>(sellOffers.size(), 810 + std::max<int32_t>(0, 810 - buyOffers.size()));

	NetworkMessage msg;
	msg.addByte(0xF9);
	if (oldProtocol) {
		msg.add<uint16_t>(MARKETREQUEST_OWN_HISTORY_OLD);
	} else {
		msg.addByte(MARKETREQUEST_OWN_HISTORY);
	}

	msg.add<uint32_t>(buyOffersToSend);
	for (auto it = buyOffers.begin(); i < buyOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.add<uint16_t>(it->itemId);
		if (!oldProtocol && Item::items[it->itemId].upgradeClassification > 0) {
			msg.addByte(it->tier);
		}
		msg.add<uint16_t>(it->amount);
		if (oldProtocol) {
			msg.add<uint32_t>(it->price);
		} else {
			msg.add<uint64_t>(static_cast<uint64_t>(it->price));
		}
		msg.addByte(it->state);
	}

	counterMap.clear();
	i = 0;

	msg.add<uint32_t>(sellOffersToSend);
	for (auto it = sellOffers.begin(); i < sellOffersToSend; ++it, ++i) {
		msg.add<uint32_t>(it->timestamp);
		msg.add<uint16_t>(counterMap[it->timestamp]++);
		msg.add<uint16_t>(it->itemId);
		if (Item::items[it->itemId].upgradeClassification > 0) {
			msg.addByte(it->tier);
		}
		msg.add<uint16_t>(it->amount);
		msg.add<uint64_t>(it->price);
		msg.addByte(it->state);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendForgingData() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x86);

	std::map<uint8_t, uint16_t> tierCorePrices;
	std::map<uint8_t, uint64_t> convergenceFusionPrices;
	std::map<uint8_t, uint64_t> convergenceTransferPrices;

	const auto classifications = g_game().getItemsClassifications();
	msg.addByte(classifications.size());
	for (const auto &classification : classifications) {
		msg.addByte(classification->id);
		msg.addByte(classification->tiers.size());
		for (const auto &[tier, tierInfo] : classification->tiers) {
			msg.addByte(tier - 1);
			msg.add<uint64_t>(tierInfo.regularPrice);
			tierCorePrices[tier] = tierInfo.corePrice;
			convergenceFusionPrices[tier] = tierInfo.convergenceFusionPrice;
			convergenceTransferPrices[tier] = tierInfo.convergenceTransferPrice;
		}
	}

	// Version 13.30
	// Forge Config Bytes

	// Exalted core table per tier
	msg.addByte(static_cast<uint8_t>(tierCorePrices.size()));
	for (const auto &[tier, cores] : tierCorePrices) {
		msg.addByte(tier);
		msg.addByte(cores);
	}

	// Convergence fusion prices per tier
	msg.addByte(static_cast<uint8_t>(convergenceFusionPrices.size()));
	for (const auto &[tier, price] : convergenceFusionPrices) {
		msg.addByte(tier - 1);
		msg.add<uint64_t>(price);
	}

	// Convergence transfer prices per tier
	msg.addByte(static_cast<uint8_t>(convergenceTransferPrices.size()));
	for (const auto &[tier, price] : convergenceTransferPrices) {
		msg.addByte(tier);
		msg.add<uint64_t>(price);
	}

	// (conversion) (left column top) Cost to make 1 bottom item - 20
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_COST_ONE_SLIVER)));
	// (conversion) (left column bottom) How many items to make - 3
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_SLIVER_AMOUNT)));
	// (conversion) (middle column top) Cost to make 1 - 50
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_CORE_COST)));
	// (conversion) (right column top) Current stored dust limit minus this number = cost to increase stored dust limit - 75
	msg.addByte(75);
	// (conversion) (right column bottom) Starting stored dust limit
	msg.add<uint16_t>(player->getForgeDustLevel());
	// (conversion) (right column bottom) Max stored dust limit - 325
	msg.add<uint16_t>(g_configManager().getNumber(FORGE_MAX_DUST));
	// (normal fusion) dust cost - 100
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_FUSION_DUST_COST)));
	// (convergence fusion) dust cost - 130
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_CONVERGENCE_FUSION_DUST_COST)));
	// (normal transfer) dust cost - 100
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_TRANSFER_DUST_COST)));
	// (convergence transfer) dust cost - 160
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_CONVERGENCE_TRANSFER_DUST_COST)));
	// (fusion) Base success rate - 50
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_BASE_SUCCESS_RATE)));
	// (fusion) Bonus success rate - 15
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_BONUS_SUCCESS_RATE)));
	// (fusion) Tier loss chance after reduction - 50
	msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(FORGE_TIER_LOSS_REDUCTION)));

	// Update player resources
	parseSendResourceBalance();

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenForge() {
	// We will use it when sending the bytes to send the item information to the client
	std::map<uint16_t, std::map<uint8_t, uint16_t>> fusionItemsMap;
	std::map<int32_t, std::map<uint16_t, std::map<uint8_t, uint16_t>>> convergenceFusionItemsMap;
	std::map<int32_t, std::map<uint16_t, std::map<uint8_t, uint16_t>>> convergenceTransferItemsMap;
	std::map<uint16_t, std::map<uint8_t, uint16_t>> donorTierItemMap;
	std::map<uint16_t, std::map<uint8_t, uint16_t>> receiveTierItemMap;

	auto maxConfigTier = g_configManager().getNumber(FORGE_MAX_ITEM_TIER);

	/*
	 *Start - Parsing items informations
	 */
	for (const auto &item : player->getAllInventoryItems(true)) {
		if (item->hasImbuements()) {
			continue;
		}

		auto itemClassification = item->getClassification();
		auto itemTier = item->getTier();
		auto maxTier = (itemClassification == 4 ? maxConfigTier : itemClassification);
		// Save fusion items on map
		if (itemClassification != 0 && itemTier < maxTier) {
			getForgeInfoMap(item, fusionItemsMap);
		}

		if (itemClassification > 0) {
			if (itemClassification < 4 && itemTier > maxTier) {
				continue;
			}
			// Save transfer (donator of tier) items on map
			if (itemTier > 1) {
				getForgeInfoMap(item, donorTierItemMap);
			}
			// Save transfer (receiver of tier) items on map
			if (itemTier == 0) {
				getForgeInfoMap(item, receiveTierItemMap);
			}
			if (itemClassification == 4) {
				auto slotPosition = item->getSlotPosition();
				if ((slotPosition & SLOTP_TWO_HAND) != 0) {
					slotPosition = SLOTP_HAND;
				}
				getForgeInfoMap(item, convergenceFusionItemsMap[slotPosition]);
				getForgeInfoMap(item, convergenceTransferItemsMap[item->getClassification()]);
			}
		}
	}

	// Checking size of map to send in the addByte (total fusion items count)
	uint8_t fusionTotalItemsCount = 0;
	for (const auto &[itemId, tierAndCountMap] : fusionItemsMap) {
		for (const auto &[itemTier, itemCount] : tierAndCountMap) {
			if (itemCount >= 2) {
				fusionTotalItemsCount++;
			}
		}
	}

	/*
	 * Start - Sending bytes
	 */
	NetworkMessage msg;

	// Header byte (135)
	msg.addByte(0x87);

	msg.add<uint16_t>(fusionTotalItemsCount);
	for (const auto &[itemId, tierAndCountMap] : fusionItemsMap) {
		for (const auto &[itemTier, itemCount] : tierAndCountMap) {
			if (itemCount >= 2) {
				msg.addByte(0x01); // Number of friend items?
				msg.add<uint16_t>(itemId);
				msg.addByte(itemTier);
				msg.add<uint16_t>(itemCount);
			}
		}
	}

	// msg.add<uint16_t>(convergenceItemsMap.size());
	auto convergenceFusionCountPosition = msg.getBufferPosition();
	msg.skipBytes(2);
	uint16_t convergenceFusionCount = 0;
	/*
	for each convergence fusion (1 per item slot, only class 4):
	1 byte: count fusable items
	for each fusable item:
	    2 bytes: item id
	    1 byte: tier
	    2 bytes: count
	*/
	for (const auto &[slot, itemMap] : convergenceFusionItemsMap) {
		uint8_t totalItemsCount = 0;
		auto totalItemsCountPosition = msg.getBufferPosition();
		msg.skipBytes(1); // Total items count
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto &[tier, itemCount] : tierAndCountMap) {
				if (tier >= maxConfigTier) {
					continue;
				}
				totalItemsCount++;
				msg.add<uint16_t>(itemId);
				msg.addByte(tier);
				msg.add<uint16_t>(itemCount);
			}
		}
		auto endPosition = msg.getBufferPosition();
		msg.setBufferPosition(totalItemsCountPosition);
		if (totalItemsCount > 0) {
			msg.addByte(totalItemsCount);
			msg.setBufferPosition(endPosition);
			convergenceFusionCount++;
		}
	}

	auto transferTotalCountPosition = msg.getBufferPosition();
	msg.setBufferPosition(convergenceFusionCountPosition);
	msg.add<uint16_t>(convergenceFusionCount);
	msg.setBufferPosition(transferTotalCountPosition);

	auto transferTotalCount = donorTierItemMap.size();
	msg.addByte(transferTotalCount);
	if (transferTotalCount > 0) {
		for (const auto &[itemId, tierAndCountMap] : donorTierItemMap) {
			// Let's access the itemType to check the item's (donator of tier) classification level
			// Must be the same as the item that will receive the tier
			const ItemType &donorType = Item::items[itemId];
			auto donorSlotPosition = donorType.slotPosition;
			if ((donorSlotPosition & SLOTP_TWO_HAND) != 0) {
				donorSlotPosition = SLOTP_HAND;
			}

			// Total count of item (donator of tier)
			auto donorTierTotalItemsCount = tierAndCountMap.size();
			msg.add<uint16_t>(donorTierTotalItemsCount);
			for (const auto &[donorItemTier, donorItemCount] : tierAndCountMap) {
				msg.add<uint16_t>(itemId);
				msg.addByte(donorItemTier);
				msg.add<uint16_t>(donorItemCount);
			}

			uint16_t receiveTierTotalItemCount = 0;
			for (const auto &[iteratorItemId, unusedTierAndCountMap] : receiveTierItemMap) {
				// Let's access the itemType to check the item's (receiver of tier) classification level
				const ItemType &receiveType = Item::items[iteratorItemId];
				auto receiveSlotPosition = receiveType.slotPosition;
				if ((receiveSlotPosition & SLOTP_TWO_HAND) != 0) {
					receiveSlotPosition = SLOTP_HAND;
				}
				if (donorType.upgradeClassification == receiveType.upgradeClassification && donorSlotPosition == receiveSlotPosition) {
					receiveTierTotalItemCount++;
				}
			}

			// Total count of item (receiver of tier)
			msg.add<uint16_t>(receiveTierTotalItemCount);
			if (receiveTierTotalItemCount > 0) {
				for (const auto &[receiveItemId, receiveTierAndCountMap] : receiveTierItemMap) {
					// Let's access the itemType to check the item's (receiver of tier) classification level
					const ItemType &receiveType = Item::items[receiveItemId];
					auto receiveSlotPosition = receiveType.slotPosition;
					if ((receiveSlotPosition & SLOTP_TWO_HAND) != 0) {
						receiveSlotPosition = SLOTP_HAND;
					}
					if (donorType.upgradeClassification == receiveType.upgradeClassification && donorSlotPosition == receiveSlotPosition) {
						for (const auto &[receiveItemTier, receiveItemCount] : receiveTierAndCountMap) {
							msg.add<uint16_t>(receiveItemId);
							msg.add<uint16_t>(receiveItemCount);
						}
					}
				}
			}
		}
	}

	auto convergenceCountPosition = msg.getBufferPosition();
	msg.skipBytes(1);
	uint8_t convergenceTransferCount = 0;

	/*
	for each convergence transfer:
	    2 bytes: count donors
	    for each donor:
	        2 bytes: item id
	        1 byte: tier
	        2 bytes: count
	    2 bytes: count receivers
	    for each receiver:
	        2 bytes: item id
	        2 bytes: count
	*/
	for (const auto &[slot, itemMap] : convergenceTransferItemsMap) {
		uint16_t donorCount = 0;
		uint16_t receiverCount = 0;
		auto donorCountPosition = msg.getBufferPosition();
		msg.skipBytes(2); // Donor count
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto [tier, itemCount] : tierAndCountMap) {
				if (tier >= 1) {
					donorCount++;
					msg.add<uint16_t>(itemId);
					msg.addByte(tier);
					msg.add<uint16_t>(itemCount);
				} else {
					receiverCount++;
				}
			}
		}
		if (donorCount == 0 && receiverCount == 0) {
			msg.setBufferPosition(donorCountPosition);
			continue;
		}
		auto receiverCountPosition = msg.getBufferPosition();
		msg.setBufferPosition(donorCountPosition);
		msg.add<uint16_t>(donorCount);
		++convergenceTransferCount;
		msg.setBufferPosition(receiverCountPosition);
		msg.add<uint16_t>(receiverCount);
		for (const auto &[itemId, tierAndCountMap] : itemMap) {
			for (const auto [tier, itemCount] : tierAndCountMap) {
				if (tier == 0) {
					msg.add<uint16_t>(itemId);
					msg.add<uint16_t>(itemCount);
				}
			}
		}
	}
	auto dustLevelPosition = msg.getBufferPosition();
	msg.setBufferPosition(convergenceCountPosition);
	msg.addByte(convergenceTransferCount);
	msg.setBufferPosition(dustLevelPosition);

	msg.add<uint16_t>(player->getForgeDustLevel()); // Player dust limit
	writeToOutputBuffer(msg);
	// Update forging informations
	sendForgingData();
}

void ProtocolGame::sendForgeResult(ForgeAction_t actionType, uint16_t leftItemId, uint8_t leftTier, uint16_t rightItemId, uint8_t rightTier, bool success, uint8_t bonus, uint8_t coreCount, bool convergence) {
	NetworkMessage msg;
	msg.addByte(0x8A);

	// 0 = fusion | 1 = transfer
	msg.addByte(static_cast<uint8_t>(actionType));
	msg.addByte(convergence);

	if (convergence && actionType == ForgeAction_t::FUSION) {
		success = true;
		std::swap(leftItemId, rightItemId);
	}

	msg.addByte(success);

	msg.add<uint16_t>(leftItemId);
	msg.addByte(leftTier);
	msg.add<uint16_t>(rightItemId);
	msg.addByte(rightTier);

	if (actionType == ForgeAction_t::TRANSFER) {
		msg.addByte(0x00); // Bonus type always none for transfer
	} else {
		msg.addByte(bonus); // Roll fusion bonus
		// Core kept
		if (bonus == 2) {
			msg.addByte(coreCount);
		} else if (bonus >= 4 && bonus <= 8) {
			msg.add<uint16_t>(leftItemId);
			msg.addByte(leftTier);
		}
	}

	writeToOutputBuffer(msg);
	g_logger().debug("Send forge fusion: type {}, left item {}, left tier {}, right item {}, rightTier {}, success {}, bonus {}, coreCount {}, convergence {}", fmt::underlying(actionType), leftItemId, leftTier, rightItemId, rightTier, success, bonus, coreCount, convergence);
	sendOpenForge();
}

void ProtocolGame::sendForgeHistory(uint8_t page) {
	static constexpr size_t entriesPerPage = 9;
	const auto requestedPage = static_cast<size_t>(page) + 1;
	const auto &historyVector = player->forgeHistory().get();
	const auto historyVectorLen = historyVector.size();

	uint16_t currentPage = 1;
	uint16_t lastPage = 1;

	std::vector<ForgeHistory> historyPerPage;
	if (historyVectorLen > 0) {
		const auto lastPageCount = ((historyVectorLen - 1) / entriesPerPage) + 1;
		lastPage = static_cast<uint16_t>(std::min<size_t>(lastPageCount, std::numeric_limits<uint16_t>::max()));
		currentPage = static_cast<uint16_t>(std::min(requestedPage, static_cast<size_t>(lastPage)));

		const auto pageFirstEntry = historyVectorLen - (static_cast<size_t>(currentPage) - 1) * entriesPerPage;
		const auto pageLastEntry = historyVectorLen > static_cast<size_t>(currentPage) * entriesPerPage ? historyVectorLen - static_cast<size_t>(currentPage) * entriesPerPage : 0;

		for (auto entry = pageFirstEntry; entry > pageLastEntry; --entry) {
			[[maybe_unused]] auto &history_ref = historyPerPage.emplace_back(historyVector[entry - 1]);
		}
	}

	auto historyPageToSend = historyPerPage.size();
	NetworkMessage msg;
	msg.addByte(0x88);
	msg.add<uint16_t>(currentPage - 1); // Current page
	msg.add<uint16_t>(lastPage); // Last page
	msg.addByte(static_cast<uint8_t>(historyPageToSend)); // History to send

	if (historyPageToSend > 0) {
		for (const auto &history : historyPerPage) {
			auto action = magic_enum::enum_integer(history.actionType);
			msg.add<uint32_t>(static_cast<uint32_t>(history.createdAt));
			msg.addByte(action);
			msg.addString(history.description);
			msg.addByte((history.bonus >= 1 && history.bonus < 8) ? 0x01 : 0x00);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendForgeError(const ReturnValue returnValue) {
	sendMessageDialog(getReturnMessage(returnValue));
	closeForgeWindow();
}

void ProtocolGame::sendMarketDetail(uint16_t itemId, uint8_t tier) {
	NetworkMessage msg;
	msg.addByte(0xF8);
	msg.add<uint16_t>(itemId);
	const ItemType &it = Item::items[itemId];

	if (!oldProtocol && it.upgradeClassification > 0) {
		msg.addByte(tier);
	}

	if (it.armor != 0) {
		msg.addString(std::to_string(it.armor));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.isRanged()) {
		std::ostringstream ss;
		bool separator = false;

		if (it.attack != 0) {
			ss << "attack +" << it.attack;
			separator = true;
		}

		if (it.hitChance != 0) {
			if (separator) {
				ss << ", ";
			}
			ss << "chance to hit +" << static_cast<int16_t>(it.hitChance) << "%";
			separator = true;
		}

		if (it.shootRange != 0) {
			if (separator) {
				ss << ", ";
			}
			ss << static_cast<uint16_t>(it.shootRange) << " fields";
		}
		msg.addString(ss.str());
	} else {
		std::string attackDescription;
		if (it.abilities && it.abilities->elementType != COMBAT_NONE && it.abilities->elementDamage != 0) {
			attackDescription = fmt::format("{} {}", it.abilities->elementDamage, getCombatName(it.abilities->elementType));
		}

		if (it.attack != 0 && !attackDescription.empty()) {
			attackDescription = fmt::format("{} physical + {}", it.attack, attackDescription);
		} else if (it.attack != 0 && attackDescription.empty()) {
			attackDescription = std::to_string(it.attack);
		}

		msg.addString(attackDescription);
	}

	if (it.isContainer()) {
		msg.addString(std::to_string(it.maxItems));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.defense != 0 || it.isMissile()) {
		if (it.extraDefense != 0) {
			std::ostringstream ss;
			ss << it.defense << ' ' << std::showpos << it.extraDefense << std::noshowpos;
			msg.addString(ss.str());
		} else {
			msg.addString(std::to_string(it.defense));
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!it.description.empty()) {
		const std::string &descr = it.description;
		if (descr.back() == '.') {
			msg.addString(std::string(descr, 0, descr.length() - 1));
		} else {
			msg.addString(descr);
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.decayTime != 0) {
		std::ostringstream ss;
		ss << it.decayTime << " seconds";
		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (size_t i = 0; i < COMBAT_COUNT; ++i) {
			if (it.abilities->absorbPercent[i] == 0) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}%", getCombatName(indexToCombatType(i)), it.abilities->absorbPercent[i]);
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqLevel != 0) {
		msg.addString(std::to_string(it.minReqLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.minReqMagicLevel != 0) {
		msg.addString(std::to_string(it.minReqMagicLevel));
	} else {
		msg.add<uint16_t>(0x00);
	}

	msg.addString(it.vocationString);
	msg.addString(it.runeSpellName);

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;

		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; i++) {
			if (i == SKILL_MANA_LEECH_CHANCE || i == SKILL_LIFE_LEECH_CHANCE) {
				continue;
			}

			if (!it.abilities->skills[i]) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}", getSkillName(i), it.abilities->skills[i]);
		}

		for (uint8_t i = SKILL_CRITICAL_HIT_CHANCE; i <= SKILL_LAST; i++) {
			if (i == SKILL_MANA_LEECH_CHANCE || i == SKILL_LIFE_LEECH_CHANCE) {
				continue;
			}

			auto skills = it.abilities->skills[i];
			if (!skills) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format("{} {:+}%", getSkillName(i), skills / 100.0);
		}

		if (it.abilities->stats[STAT_MAGICPOINTS] != 0) {
			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << fmt::format(" magic level {:+}", it.abilities->stats[STAT_MAGICPOINTS]);
		}

		// Version 12.72 (Specialized magic level modifier)
		for (uint8_t i = 1; i <= 11; i++) {
			if (it.abilities->specializedMagicLevel[i]) {
				if (separator) {
					ss << ", ";
				} else {
					separator = true;
				}
				std::string combatName = getCombatName(indexToCombatType(i));
				ss << std::showpos << combatName << std::noshowpos << "magic level +" << it.abilities->specializedMagicLevel[i];
			}
		}

		if (it.abilities->speed != 0) {
			if (separator) {
				ss << ", ";
			}

			ss << fmt::format("speed {:+}", (it.abilities->speed >> 1));
		}

		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (it.charges != 0) {
		msg.addString(std::to_string(it.charges));
	} else {
		msg.add<uint16_t>(0x00);
	}

	std::string weaponName = getWeaponName(it.weaponType);

	if (it.slotPosition & SLOTP_TWO_HAND) {
		if (!weaponName.empty()) {
			weaponName += ", two-handed";
		} else {
			weaponName = "two-handed";
		}
	}

	msg.addString(weaponName);

	if (it.weight != 0) {
		std::ostringstream ss;
		if (it.weight < 10) {
			ss << "0.0" << it.weight;
		} else if (it.weight < 100) {
			ss << "0." << it.weight;
		} else {
			std::string weightString = std::to_string(it.weight);
			weightString.insert(weightString.end() - 2, '.');
			ss << weightString;
		}
		ss << " oz";
		msg.addString(ss.str());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!oldProtocol) {
		std::string augmentsDescription = it.parseAugmentDescription(true);
		if (!augmentsDescription.empty()) {
			msg.addString(augmentsDescription);
		} else {
			msg.add<uint16_t>(0x00); // no augments
		}
	}

	if (it.imbuementSlot > 0) {
		msg.addString(std::to_string(it.imbuementSlot));
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (!oldProtocol) {
		// Version 12.70 new skills
		if (it.abilities) {
			std::ostringstream string;
			if (it.abilities->magicShieldCapacityFlat > 0) {
				string.clear();
				string << std::showpos << it.abilities->magicShieldCapacityFlat << std::noshowpos << " and " << it.abilities->magicShieldCapacityPercent << "%";
				msg.addString(string.str());
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->cleavePercent > 0) {
				string.clear();
				string << it.abilities->cleavePercent << "%";
				msg.addString(string.str());
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->reflectFlat[COMBAT_PHYSICALDAMAGE] > 0) {
				string.clear();
				string << it.abilities->reflectFlat[COMBAT_PHYSICALDAMAGE];
				msg.addString(string.str());
			} else {
				msg.add<uint16_t>(0x00);
			}

			if (it.abilities->perfectShotDamage > 0) {
				string.clear();
				string << std::showpos << it.abilities->perfectShotDamage << std::noshowpos << " at range " << unsigned(it.abilities->perfectShotRange);
				msg.addString(string.str());
			} else {
				msg.add<uint16_t>(0x00);
			}
		} else {
			// Send empty skills
			// Cleave modifier
			msg.add<uint16_t>(0x00);
			// Magic shield capacity
			msg.add<uint16_t>(0x00);
			// Damage reflection modifier
			msg.add<uint16_t>(0x00);
			// Perfect shot modifier
			msg.add<uint16_t>(0x00);
		}

		// Upgrade classification
		if (it.upgradeClassification > 0) {
			msg.addString(std::to_string(it.upgradeClassification));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (it.elementalBond != COMBAT_NONE) {
			msg.addString(toPascalCase(getCombatName(it.elementalBond)));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (it.mantra > 0) {
			msg.addString(std::to_string(it.mantra));
		} else {
			msg.add<uint16_t>(0x00);
		}

		if (clientVersion >= 1510) {
			msg.addString(getMarketDetailImbuementEffect(itemId));
		}

		if (it.upgradeClassification > 0 && tier > 0) {
			std::ostringstream ss;

			double chance;
			if (it.isWeapon()) {
				chance = (0.05 * tier * tier) + (0.4 * tier) + 0.05;
				ss << fmt::format("{} ({:.2f}% Onslaught)", static_cast<uint16_t>(tier), chance);
			} else if (it.isHelmet()) {
				chance = (0.05 * tier * tier) + (1.9 * tier) + 0.05;
				ss << fmt::format("{} ({:.2f}% Momentum)", static_cast<uint16_t>(tier), chance);
			} else if (it.isArmor()) {
				chance = (0.0307576 * tier * tier) + (0.440697 * tier) + 0.026;
				ss << fmt::format("{} ({:.2f}% Ruse)", static_cast<uint16_t>(tier), chance);
			} else if (it.isLegs()) {
				chance = (0.0127 * tier * tier) + (0.1070 * tier) + 0.0073;
				ss << fmt::format("{} ({:.2f}% Transcendence)", static_cast<uint16_t>(tier), chance);
			} else if (it.isBoots()) {
				chance = (0.4 * tier * tier) + (1.7 * tier) + 0.4;
				ss << fmt::format("{} ({:.2f}% Amplification)", static_cast<uint16_t>(tier), chance);
			}
			msg.addString(ss.str());
		} else if (it.upgradeClassification > 0 && tier == 0) {
			msg.addString(std::to_string(tier));
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	const auto &purchaseStatsMap = IOMarket::getInstance().getPurchaseStatistics();
	auto purchaseIterator = purchaseStatsMap.find(itemId);
	if (purchaseIterator != purchaseStatsMap.end()) {
		const auto &tierStatsMap = purchaseIterator->second;
		auto tierStatsIter = tierStatsMap.find(tier);
		if (tierStatsIter != tierStatsMap.end()) {
			const auto &purchaseStatistics = tierStatsIter->second;
			msg.addByte(0x01);
			msg.add<uint32_t>(purchaseStatistics.numTransactions);
			if (oldProtocol) {
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.totalPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.highestPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), purchaseStatistics.lowestPrice));
			} else {
				msg.add<uint64_t>(purchaseStatistics.totalPrice);
				msg.add<uint64_t>(purchaseStatistics.highestPrice);
				msg.add<uint64_t>(purchaseStatistics.lowestPrice);
			}
		} else {
			msg.addByte(0x00);
		}
	} else {
		msg.addByte(0x00); // send to old protocol ?
	}

	const auto &saleStatsMap = IOMarket::getInstance().getSaleStatistics();
	auto saleIterator = saleStatsMap.find(itemId);
	if (saleIterator != saleStatsMap.end()) {
		const auto &tierStatsMap = saleIterator->second;
		auto tierStatsIter = tierStatsMap.find(tier);
		if (tierStatsIter != tierStatsMap.end()) {
			const auto &saleStatistics = tierStatsIter->second;
			msg.addByte(0x01);
			msg.add<uint32_t>(saleStatistics.numTransactions);
			if (oldProtocol) {
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.totalPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.highestPrice));
				msg.add<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.lowestPrice));
			} else {
				msg.add<uint64_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), saleStatistics.totalPrice));
				msg.add<uint64_t>(saleStatistics.highestPrice);
				msg.add<uint64_t>(saleStatistics.lowestPrice);
			}
		} else {
			msg.addByte(0x00);
		}
	} else {
		msg.addByte(0x00); // send to old protocol ?
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTradeItemRequest(const std::string &traderName, const std::shared_ptr<Item> &item, bool ack) {
	NetworkMessage msg;

	if (ack) {
		msg.addByte(0x7D);
	} else {
		msg.addByte(0x7E);
	}

	msg.addString(traderName);

	if (std::shared_ptr<Container> tradeContainer = item->getContainer()) {
		std::list<std::shared_ptr<Container>> listContainer { tradeContainer };
		std::list<std::shared_ptr<Item>> itemList { tradeContainer };
		while (!listContainer.empty()) {
			const auto &container = listContainer.front();
			for (const auto &containerItem : container->getItemList()) {
				const auto &tmpContainer = containerItem->getContainer();
				if (tmpContainer) {
					listContainer.emplace_back(tmpContainer);
				}
				itemList.emplace_back(containerItem);
			}

			// Removes the object after processing everything, avoiding memory usage after freeing
			listContainer.pop_front();
		}

		msg.addByte(itemList.size());
		for (const std::shared_ptr<Item> &listItem : itemList) {
			AddItem(msg, listItem);
		}
	} else {
		msg.addByte(0x01);
		AddItem(msg, item);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseTrade() {
	NetworkMessage msg;
	msg.addByte(0x7F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseContainer(uint8_t cid) {
	NetworkMessage msg;
	msg.addByte(0x6F);
	msg.addByte(cid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureTurn(const std::shared_ptr<Creature> &creature, uint32_t stackPos) {
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(creature->getPosition());
	msg.addByte(static_cast<uint8_t>(stackPos));
	msg.add<uint16_t>(0x63);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(creature->getDirection());
	msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
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
	if (std::shared_ptr<Player> speaker = creature->getPlayer()) {
		msg.add<uint16_t>(speaker->getLevel());
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		msg.addByte(TALKTYPE_MONSTER_SAY);
	} else {
		msg.addByte(type);
	}

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
		if (std::shared_ptr<Player> speaker = creature->getPlayer()) {
			msg.add<uint16_t>(speaker->getLevel());
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		msg.addByte(TALKTYPE_CHANNEL_O);
	} else {
		msg.addByte(type);
	}

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

	if (oldProtocol && type >= TALKTYPE_MONSTER_LAST_OLDPROTOCOL && type != TALKTYPE_CHANNEL_R2) {
		msg.addByte(TALKTYPE_PRIVATE_TO);
	} else {
		msg.addByte(type);
	}

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelTarget() {
	NetworkMessage msg;
	msg.addByte(0xA3);
	msg.add<uint32_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChangeSpeed(const std::shared_ptr<Creature> &creature, uint16_t speed) {
	NetworkMessage msg;
	msg.addByte(0x8F);
	msg.add<uint32_t>(creature->getID());
	msg.add<uint16_t>(creature->getBaseSpeed());
	msg.add<uint16_t>(speed);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelWalk() {
	if (player) {
		NetworkMessage msg;
		msg.addByte(0xB5);
		msg.addByte(player->getDirection());
		writeToOutputBuffer(msg);
	}
}

void ProtocolGame::sendSkills() {
	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPing() {
	if (player) {
		NetworkMessage msg;
		msg.addByte(0x1D);
		writeToOutputBuffer(msg);
	}
}

void ProtocolGame::sendPingBack() {
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position &from, const Position &to, uint16_t type) {
	if (oldProtocol && type > 0xFF) {
		return;
	}
	NetworkMessage msg;
	if (oldProtocol) {
		msg.addByte(0x85);
		msg.addPosition(from);
		msg.addPosition(to);
		msg.addByte(static_cast<uint8_t>(type));
	} else {
		msg.addByte(0x83);
		msg.addPosition(from);
		msg.addByte(MAGIC_EFFECTS_CREATE_DISTANCEEFFECT);
		msg.add<uint16_t>(type);
		msg.addByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x))));
		msg.addByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int32_t>(to.y) - static_cast<int32_t>(from.y))));
		msg.addByte(MAGIC_EFFECTS_END_LOOP);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRestingStatus(uint8_t protection) {
	if (oldProtocol || !player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA9);
	msg.addByte(protection); // 1 / 0

	uint8_t dailyStreak = 0;
	auto dailyRewardKV = player->kv()->scoped("daily-reward")->get("streak");
	if (dailyRewardKV && dailyRewardKV.has_value()) {
		dailyStreak = static_cast<uint8_t>(dailyRewardKV->getNumber());
	}

	msg.addByte(dailyStreak < 2 ? 0 : 1);
	if (dailyStreak < 2) {
		msg.addString("Resting Area (no active bonus)");
	} else {
		std::ostringstream ss;
		ss << "Active Resting Area Bonuses: ";
		if (dailyStreak < DAILY_REWARD_DOUBLE_HP_REGENERATION) {
			ss << "\nHit Points Regeneration";
		} else {
			ss << "\nDouble Hit Points Regeneration";
		}
		if (dailyStreak >= DAILY_REWARD_MP_REGENERATION) {
			if (dailyStreak < DAILY_REWARD_DOUBLE_MP_REGENERATION) {
				ss << ",\nMana Points Regeneration";
			} else {
				ss << ",\nDouble Mana Points Regeneration";
			}
		}
		if (dailyStreak >= DAILY_REWARD_STAMINA_REGENERATION) {
			ss << ",\nStamina Points Regeneration";
		}
		if (dailyStreak >= DAILY_REWARD_SOUL_REGENERATION) {
			ss << ",\nSoul Points Regeneration";
		}
		ss << ".";
		msg.addString(ss.str());
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMagicEffect(const Position &pos, uint16_t type) {
	if (!canSee(pos) || (oldProtocol && type > 0xFF)) {
		return;
	}

	NetworkMessage msg;
	if (oldProtocol) {
		msg.addByte(0x83);
		msg.addPosition(pos);
		msg.addByte(static_cast<uint8_t>(type));
	} else {
		msg.addByte(0x83);
		msg.addPosition(pos);
		msg.addByte(MAGIC_EFFECTS_CREATE_EFFECT);
		msg.add<uint16_t>(type);
		msg.addByte(MAGIC_EFFECTS_END_LOOP);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHealth(const std::shared_ptr<Creature> &creature) {
	if (creature->isHealthHidden()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8C);
	msg.add<uint32_t>(creature->getID());
	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(static_cast<uint8_t>(std::min<double>(100, std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100))));
	}

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

void ProtocolGame::sendPlayerVocation(const std::shared_ptr<Player> &target) {
	if (!player || !target || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(target->getID());
	msg.addByte(13); // vocation
	msg.addByte(target->getVocation()->getClientId());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFYIBox(const std::string &message) {
	NetworkMessage msg;
	msg.addByte(0x15);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

// tile
void ProtocolGame::sendMapDescription(const Position &pos) {
	NetworkMessage msg;
	msg.addByte(0x64);
	msg.addPosition(player->getPosition());
	GetMapDescription(pos.x - MAP_MAX_CLIENT_VIEW_PORT_X, pos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, pos.z, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddTileItem(const Position &pos, uint32_t stackpos, const std::shared_ptr<Item> &item) {
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
	AddItem(msg, item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileItem(const Position &pos, uint32_t stackpos, const std::shared_ptr<Item> &item) {
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
	AddItem(msg, item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveTileThing(const Position &pos, uint32_t stackpos, std::source_location location) {
	if (!canSee(pos)) {
		return;
	}

	g_logger().trace("Removing tile thing at position {}, stackpos {} from protocol game, location: {}, called from: {} {}", pos.toString(), stackpos, location.function_name(), location.file_name(), location.line());

	NetworkMessage msg;
	RemoveTileThing(msg, pos, stackpos);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileCreature(const Position &pos, uint32_t stackpos, const std::shared_ptr<Creature> &creature) {
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));

	bool known;
	uint32_t removedKnown = 0;
	checkCreatureAsKnown(creature->getID(), known, removedKnown);
	AddCreature(msg, creature, known, removedKnown);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTile(const std::shared_ptr<Tile> &tile, const Position &pos) {
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x69);
	msg.addPosition(pos);

	if (tile) {
		GetTileDescription(tile, msg);
		msg.addByte(0x00);
		msg.addByte(0xFF);
	} else {
		msg.addByte(0x01);
		msg.addByte(0xFF);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPendingStateEntered() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x0A);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendEnterWorld() {
	NetworkMessage msg;
	msg.addByte(0x0F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFightModes() {
	NetworkMessage msg;
	msg.addByte(0xA7);
	msg.addByte(player->fightMode);
	msg.addByte(player->chaseMode);
	msg.addByte(player->secureMode);
	msg.addByte(PVP_MODE_DOVE);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAllowBugReport() {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x1A);
	msg.addByte(0x00); // 0x01 = DISABLE bug report
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddCreature(const std::shared_ptr<Creature> &creature, const Position &pos, int32_t stackpos, bool isLogin) {
	if (!canSee(pos)) {
		return;
	}

	if (creature != player) {
		if (stackpos >= 10) {
			return;
		}

		NetworkMessage msg;
		msg.addByte(0x6A);
		msg.addPosition(pos);
		msg.addByte(static_cast<uint8_t>(stackpos));

		bool known;
		uint32_t removedKnown;
		checkCreatureAsKnown(creature->getID(), known, removedKnown);
		AddCreature(msg, creature, known, removedKnown);
		writeToOutputBuffer(msg);

		if (isLogin) {
			if (std::shared_ptr<Player> creaturePlayer = creature->getPlayer()) {
				if (!creaturePlayer->isAccessPlayer() || creaturePlayer->getAccountType() == ACCOUNT_TYPE_NORMAL) {
					sendMagicEffect(pos, CONST_ME_TELEPORT);
				}
			} else {
				sendMagicEffect(pos, CONST_ME_TELEPORT);
			}
		}

		return;
	}

	NetworkMessage msg;
	msg.addByte(0x17);

	msg.add<uint32_t>(player->getID());
	msg.add<uint16_t>(SERVER_BEAT); // beat duration (50)

	msg.addDouble(Creature::speedA, 3);
	msg.addDouble(Creature::speedB, 3);
	msg.addDouble(Creature::speedC, 3);

	// Allow bug report (Ctrl + Z)
	if (oldProtocol) {
		if (player->getAccountType() >= ACCOUNT_TYPE_NORMAL) {
			msg.addByte(0x01);
		} else {
			msg.addByte(0x00);
		}
	}

	msg.addByte(0x00); // can change pvp framing option
	msg.addByte(0x00); // expert mode button enabled

	msg.addString(g_configManager().getString(STORE_IMAGES_URL));
	msg.add<uint16_t>(static_cast<uint16_t>(g_configManager().getNumber(STORE_COIN_PACKET)));

	if (!oldProtocol) {
		const bool exivaEnabled = g_game().getWorldType() == WORLD_TYPE_NO_PVP;
		msg.addByte(exivaEnabled ? 0x01 : 0x00); // exiva button enabled
		if (exivaEnabled) {
			sendExivaRestrictions(true);
		}
	}

	writeToOutputBuffer(msg);

	// Allow bug report (Ctrl + Z)
	sendAllowBugReport();

	sendTibiaTime(g_game().getLightHour());
	sendPendingStateEntered();
	sendEnterWorld();
	sendMapDescription(pos);
	loggedIn = true;

	if (isLogin) {
		sendMagicEffect(pos, CONST_ME_TELEPORT);
		sendDisableLoginMusic();
	}

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<Slots_t>(i), player->getInventoryItem(static_cast<Slots_t>(i)));
	}

	player->weaponProficiency().clearAllStats();
	if (const auto equippedWeaponId = player->getWeaponId(true); equippedWeaponId != 0) {
		player->weaponProficiency().applyPerks(equippedWeaponId, false);
	}

	player->sendWeaponProficiency();
	sendStats();
	sendSkills();
	sendBlessStatus();
	sendPremiumTrigger();
	sendItemsPrice();
	sendPreyPrices();
	player->sendPreyData();
	player->sendTaskHuntingData();
	sendForgingData();

	// gameworld light-settings
	sendWorldLight(g_game().getWorldLightInfo());

	// player light level
	sendCreatureLight(creature);

	if (player->getPlayerVocationEnum() == Vocation_t::VOCATION_MONK_CIP) {
		sendMonkData(MonkData_t::Harmony, player->getHarmony());
		auto virtue = player->getVirtue();
		virtue = virtue != Virtue_t::None ? virtue : Virtue_t::Harmony;
		sendMonkData(MonkData_t::Virtue, enumToValue(virtue));
		sendMonkData(MonkData_t::Serenity, 1);
	}

	sendVIPGroups();

	const auto &vipEntries = IOLoginData::getVIPEntries(player->getAccountId());

	if (player->isAccessPlayer()) {
		for (const VIPEntry &entry : vipEntries) {
			VipStatus_t vipStatus;

			std::shared_ptr<Player> vipPlayer = g_game().getPlayerByGUID(entry.guid);
			if (!vipPlayer) {
				vipStatus = VipStatus_t::Offline;
			} else {
				vipStatus = vipPlayer->vip().getStatus();
			}

			sendVIP(entry.guid, entry.name, entry.description, entry.icon, entry.notify, vipStatus);
		}
	} else {
		for (const VIPEntry &entry : vipEntries) {
			VipStatus_t vipStatus;

			std::shared_ptr<Player> vipPlayer = g_game().getPlayerByGUID(entry.guid);
			if (!vipPlayer || vipPlayer->isInGhostMode()) {
				vipStatus = VipStatus_t::Offline;
			} else {
				vipStatus = vipPlayer->vip().getStatus();
			}

			sendVIP(entry.guid, entry.name, entry.description, entry.icon, entry.notify, vipStatus);
		}
	}

	sendInventoryIds();
	std::shared_ptr<Item> slotItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
	if (slotItem) {
		player->setMainBackpackUnassigned(slotItem->getContainer());
	}

	sendLootContainers();
	sendBasicData();
	sendHousesInfo();
	// Wheel of destiny cooldown
	if (!oldProtocol && g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		player->wheel().sendGiftOfLifeCooldown();
	}

	player->sendClientCheck();
	player->sendGameNews();
	player->sendIcons();

	// Send open containers after login.
	if (isLogin) {
		player->openPlayerContainers();
		player->sendSpellCooldowns();
	}
}

void ProtocolGame::sendMoveCreature(const std::shared_ptr<Creature> &creature, const Position &newPos, int32_t newStackPos, const Position &oldPos, int32_t oldStackPos, bool teleport) {
	if (creature == player) {
		if (oldStackPos >= 10) {
			sendMapDescription(newPos);
		} else if (teleport) {
			NetworkMessage msg;
			RemoveTileThing(msg, oldPos, oldStackPos);
			writeToOutputBuffer(msg);
			sendMapDescription(newPos);
		} else {
			NetworkMessage msg;
			if (oldPos.z == MAP_INIT_SURFACE_LAYER && newPos.z >= MAP_INIT_SURFACE_LAYER + 1) {
				RemoveTileThing(msg, oldPos, oldStackPos);
			} else {
				msg.addByte(0x6D);
				msg.addPosition(oldPos);
				msg.addByte(static_cast<uint8_t>(oldStackPos));
				msg.addPosition(newPos);
			}

			if (newPos.z > oldPos.z) {
				MoveDownCreature(msg, creature, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(msg, creature, newPos, oldPos);
			}

			if (oldPos.y > newPos.y) { // north, for old x
				msg.addByte(0x65);
				GetMapDescription(oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, newPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, 1, msg);
			} else if (oldPos.y < newPos.y) { // south, for old x
				msg.addByte(0x67);
				GetMapDescription(oldPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, newPos.y + (MAP_MAX_CLIENT_VIEW_PORT_Y + 1), newPos.z, (MAP_MAX_CLIENT_VIEW_PORT_X + 1) * 2, 1, msg);
			}

			if (oldPos.x < newPos.x) { // east, [with new y]
				msg.addByte(0x66);
				GetMapDescription(newPos.x + (MAP_MAX_CLIENT_VIEW_PORT_X + 1), newPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z, 1, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, msg);
			} else if (oldPos.x > newPos.x) { // west, [with new y]
				msg.addByte(0x68);
				GetMapDescription(newPos.x - MAP_MAX_CLIENT_VIEW_PORT_X, newPos.y - MAP_MAX_CLIENT_VIEW_PORT_Y, newPos.z, 1, (MAP_MAX_CLIENT_VIEW_PORT_Y + 1) * 2, msg);
			}
			writeToOutputBuffer(msg);
		}
	} else if (canSee(oldPos) && canSee(newPos)) {
		if (teleport || (oldPos.z == MAP_INIT_SURFACE_LAYER && newPos.z >= MAP_INIT_SURFACE_LAYER + 1) || oldStackPos >= 10) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendAddCreature(creature, newPos, newStackPos, false);
		} else {
			NetworkMessage msg;
			msg.addByte(0x6D);
			msg.addPosition(oldPos);
			msg.addByte(static_cast<uint8_t>(oldStackPos));
			msg.addPosition(newPos);
			writeToOutputBuffer(msg);
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileThing(oldPos, oldStackPos);
	} else if (canSee(newPos)) {
		sendAddCreature(creature, newPos, newStackPos, false);
	}
}

void ProtocolGame::sendInventoryItem(Slots_t slot, const std::shared_ptr<Item> &item) {
	NetworkMessage msg;
	if (item) {
		msg.addByte(0x78);
		msg.addByte(slot);
		AddItem(msg, item);
	} else {
		msg.addByte(0x79);
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendInventoryIds() {
	if (!player) {
		return;
	}

	const auto &items = player->getInventoryItemsId();

	NetworkMessage msg;
	msg.addByte(0xF5);
	const auto countPosition = msg.getBufferPosition();
	msg.skipBytes(2); // Total items count

	uint16_t totalItemsCount = 0;

	for (auto [key, amount] : items) {
		const auto &[itemId, tier] = key;

		if (amount >= 0x40000000) {
			g_logger().warn("[{}] player: {}, failed to write count for item: {} tier: {}, amount: {}, total count: {}, skipping item.", __FUNCTION__, player->getName(), itemId, tier, amount, totalItemsCount);
			continue;
		}

		msg.add<uint16_t>(itemId);
		msg.addByte(tier);
		if (!msg.writeCount(amount)) {
			g_logger().warn("[{}] player: {}, failed to write count for item: {} tier: {}, amount: {}, total count: {}, skipping item.", __FUNCTION__, player->getName(), itemId, tier, amount, totalItemsCount);
			continue;
		}

		++totalItemsCount;
	}

	msg.setBufferPosition(countPosition);
	msg.add<uint16_t>(totalItemsCount);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddContainerItem(uint8_t cid, uint16_t slot, const std::shared_ptr<Item> &item) {
	NetworkMessage msg;
	msg.addByte(0x70);
	msg.addByte(cid);
	msg.add<uint16_t>(slot);
	AddItem(msg, item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint16_t slot, const std::shared_ptr<Item> &item) {
	NetworkMessage msg;
	msg.addByte(0x71);
	msg.addByte(cid);
	msg.add<uint16_t>(slot);
	AddItem(msg, item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot, const std::shared_ptr<Item> &lastItem) {
	NetworkMessage msg;
	msg.addByte(0x72);
	msg.addByte(cid);
	msg.add<uint16_t>(slot);
	if (lastItem) {
		AddItem(msg, lastItem);
	} else {
		msg.add<uint16_t>(0x00);
	}
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

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, const std::string &text) {
	NetworkMessage msg;
	msg.addByte(0x97);
	msg.addByte(0x00);
	msg.add<uint32_t>(windowTextId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOutfitWindow() {
	NetworkMessage msg;
	msg.addByte(0xC8);

	Outfit_t currentOutfit = player->getDefaultOutfit();
	auto isSupportOutfit = player->isWearingSupportOutfit();
	const auto currentOutfitSupportsMount = g_game().outfitSupportsMount(currentOutfit.lookType);
	bool mounted = false;

	if (!isSupportOutfit && currentOutfitSupportsMount) {
		const auto currentMount = g_game().mounts->getMountByID(player->getLastMount());
		if (currentMount) {
			mounted = (currentOutfit.lookMount == currentMount->clientId);
			currentOutfit.lookMount = currentMount->clientId;
		}
	} else {
		currentOutfit.lookMount = 0;
	}

	auto &playerAttachedEffects = player->attachedEffects();
	// Wings
	const auto &currentWing = g_game().getAttachedEffects()->getWingByID(playerAttachedEffects.getCurrentWing());
	if (currentWing) {
		currentOutfit.lookWing = currentWing->id;
	}
	// Auras
	const auto &currentAura = g_game().getAttachedEffects()->getAuraByID(playerAttachedEffects.getCurrentAura());
	if (currentAura) {
		currentOutfit.lookAura = currentAura->id;
	}
	// Effects
	const auto &currentEffect = g_game().getAttachedEffects()->getEffectByID(playerAttachedEffects.getCurrentEffect());
	if (currentEffect) {
		currentOutfit.lookEffect = currentEffect->id;
	}
	// Shader
	const auto &currentShader = g_game().getAttachedEffects()->getShaderByID(playerAttachedEffects.getCurrentShader());
	if (currentShader) {
		currentOutfit.lookShader = currentShader->id;
	}
	AddOutfit(msg, currentOutfit);

	if (oldProtocol) {
		std::vector<ProtocolOutfit> protocolOutfits;
		const auto outfits = Outfits::getInstance().getOutfits(player->getSex());
		protocolOutfits.reserve(outfits.size());
		for (const auto &outfit : outfits) {
			uint8_t addons;
			if (!player->getOutfitAddons(outfit, addons)) {
				continue;
			}

			protocolOutfits.emplace_back(outfit->name, outfit->lookType, addons);
			[[maybe_unused]] auto &outfit_ref = protocolOutfits.back();
			// Game client doesn't allow more than 100 outfits
			if (protocolOutfits.size() == 150) {
				break;
			}
		}

		msg.addByte(protocolOutfits.size());
		for (const ProtocolOutfit &outfit : protocolOutfits) {
			msg.add<uint16_t>(outfit.lookType);
			msg.addString(outfit.name);
			msg.addByte(outfit.addons);
		}

		std::vector<std::shared_ptr<Mount>> mounts;
		for (const auto &mount : g_game().mounts->getMounts()) {
			if (player->hasMount(mount)) {
				mounts.emplace_back(mount);
			}
		}

		msg.addByte(mounts.size());
		for (const auto &mount : mounts) {
			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
		}

		if (isOTCR) {
			sendOutfitWindowCustomOTCR(msg); // g_game.enableFeature(GameWingsAurasEffectsShader)
		}
		writeToOutputBuffer(msg);
		return;
	}

	if (currentOutfit.lookMount == 0) {
		msg.addByte(isSupportOutfit || !currentOutfitSupportsMount ? 0 : currentOutfit.lookMountHead);
		msg.addByte(isSupportOutfit || !currentOutfitSupportsMount ? 0 : currentOutfit.lookMountBody);
		msg.addByte(isSupportOutfit || !currentOutfitSupportsMount ? 0 : currentOutfit.lookMountLegs);
		msg.addByte(isSupportOutfit || !currentOutfitSupportsMount ? 0 : currentOutfit.lookMountFeet);
	}
	msg.add<uint16_t>(currentOutfit.lookFamiliarsType);

	auto startOutfits = msg.getBufferPosition();
	// 100 is the limit of old protocol clients.
	uint16_t limitOutfits = std::numeric_limits<uint16_t>::max();
	uint16_t outfitSize = 0;
	msg.skipBytes(2);

	if (player->isAccessPlayer() && g_configManager().getBoolean(ENABLE_SUPPORT_OUTFIT)) {
		msg.add<uint16_t>(75);
		msg.addString("Gamemaster");
		msg.addByte(0);
		msg.addByte(0x00);
		++outfitSize;

		msg.add<uint16_t>(266);
		msg.addString("Customer Support");
		msg.addByte(0);
		msg.addByte(0x00);
		++outfitSize;

		msg.add<uint16_t>(302);
		msg.addString("Community Manager");
		msg.addByte(0);
		msg.addByte(0x00);
		++outfitSize;
	}

	const auto outfits = Outfits::getInstance().getOutfits(player->getSex());

	for (const auto &outfit : outfits) {
		uint8_t addons;
		if (player->getOutfitAddons(outfit, addons)) {
			msg.add<uint16_t>(outfit->lookType);
			msg.addString(outfit->name);
			msg.addByte(addons);
			msg.addByte(0x00);
			++outfitSize;
		} else if (outfit->lookType == 1210 || outfit->lookType == 1211) {
			if (player->canWear(1210, 0) || player->canWear(1211, 0)) {
				msg.add<uint16_t>(outfit->lookType);
				msg.addString(outfit->name);
				msg.addByte(3);
				msg.addByte(0x02);
				++outfitSize;
			}
		} else if (outfit->lookType == 1456 || outfit->lookType == 1457) {
			if (player->canWear(1456, 0) || player->canWear(1457, 0)) {
				msg.add<uint16_t>(outfit->lookType);
				msg.addString(outfit->name);
				msg.addByte(3);
				msg.addByte(0x03);
				++outfitSize;
			}
		} else if (outfit->from == "store") {
			msg.add<uint16_t>(outfit->lookType);
			msg.addString(outfit->name);
			msg.addByte(outfit->lookType >= 962 && outfit->lookType <= 975 ? 0 : 3);
			msg.addByte(0x01);
			msg.add<uint32_t>(0x00);
			++outfitSize;
		}

		if (outfitSize == limitOutfits) {
			break;
		}
	}

	auto endOutfits = msg.getBufferPosition();
	msg.setBufferPosition(startOutfits);
	msg.add<uint16_t>(outfitSize);
	msg.setBufferPosition(endOutfits);

	auto startMounts = msg.getBufferPosition();
	uint16_t limitMounts = std::numeric_limits<uint16_t>::max();
	uint16_t mountSize = 0;
	msg.skipBytes(2);

	const auto mounts = g_game().mounts->getMounts();
	for (const auto &mount : mounts) {
		if (player->hasMount(mount)) {
			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
			msg.addByte(0x00);
			++mountSize;
		} else if (mount->type == "store") {
			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
			msg.addByte(0x01);
			msg.add<uint32_t>(0x00);
			++mountSize;
		}

		if (mountSize == limitMounts) {
			break;
		}
	}

	auto endMounts = msg.getBufferPosition();
	msg.setBufferPosition(startMounts);
	msg.add<uint16_t>(mountSize);
	msg.setBufferPosition(endMounts);

	auto startFamiliars = msg.getBufferPosition();
	uint16_t limitFamiliars = std::numeric_limits<uint16_t>::max();
	uint16_t familiarSize = 0;
	msg.skipBytes(2);

	const auto familiars = Familiars::getInstance().getFamiliars(player->getVocationId());

	for (const auto &familiar : familiars) {
		if (!player->getFamiliar(familiar)) {
			continue;
		}

		msg.add<uint16_t>(familiar->lookType);
		msg.addString(familiar->name);
		msg.addByte(0x00);
		if (++familiarSize == limitFamiliars) {
			break;
		}
	}

	auto endFamiliars = msg.getBufferPosition();
	msg.setBufferPosition(startFamiliars);
	msg.add<uint16_t>(familiarSize);
	msg.setBufferPosition(endFamiliars);

	msg.addByte(0x00); // Try outfit
	msg.addByte(mounted ? 0x01 : 0x00);

	// Version 12.81 - Random mount 'bool'
	msg.addByte(isSupportOutfit || !currentOutfitSupportsMount ? 0x00 : (player->isRandomMounted() ? 0x01 : 0x00));

	if (isOTCR) {
		sendOutfitWindowCustomOTCR(msg); // g_game.enableFeature(GameWingsAurasEffectsShader)
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPodiumWindow(const std::shared_ptr<Item> &podium, const Position &position, uint16_t itemId, uint8_t stackpos) {
	if (!podium || oldProtocol) {
		g_logger().error("[{}] item is nullptr", __FUNCTION__);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC8);

	const auto podiumVisible = podium->getCustomAttribute("PodiumVisible");
	const auto lookType = podium->getCustomAttribute("LookType");
	const auto lookMount = podium->getCustomAttribute("LookMount");
	const auto lookDirection = podium->getCustomAttribute("LookDirection");
	if (lookType) {
		addOutfitAndMountBytes(msg, podium, lookType, "LookHead", "LookBody", "LookLegs", "LookFeet", true);
	} else {
		msg.add<uint16_t>(0);
	}

	if (lookMount) {
		addOutfitAndMountBytes(msg, podium, lookMount, "LookMountHead", "LookMountBody", "LookMountLegs", "LookMountFeet");
	} else {
		msg.add<uint16_t>(0);
		msg.addByte(0);
		msg.addByte(0);
		msg.addByte(0);
		msg.addByte(0);
	}
	msg.add<uint16_t>(0);

	auto startOutfits = msg.getBufferPosition();
	uint16_t limitOutfits = std::numeric_limits<uint16_t>::max();
	uint16_t outfitSize = 0;
	msg.skipBytes(2);

	const auto outfits = Outfits::getInstance().getOutfits(player->getSex());
	for (const auto &outfit : outfits) {
		uint8_t addons;
		if (!player->getOutfitAddons(outfit, addons)) {
			continue;
		}

		msg.add<uint16_t>(outfit->lookType);
		msg.addString(outfit->name);
		msg.addByte(addons);
		msg.addByte(0x00);
		if (++outfitSize == limitOutfits) {
			break;
		}
	}

	auto endOutfits = msg.getBufferPosition();
	msg.setBufferPosition(startOutfits);
	msg.add<uint16_t>(outfitSize);
	msg.setBufferPosition(endOutfits);

	auto startMounts = msg.getBufferPosition();
	uint16_t limitMounts = std::numeric_limits<uint16_t>::max();
	uint16_t mountSize = 0;
	msg.skipBytes(2);

	const auto mounts = g_game().mounts->getMounts();
	for (const auto &mount : mounts) {
		if (player->hasMount(mount)) {
			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
			msg.addByte(0x00);
			if (++mountSize == limitMounts) {
				break;
			}
		}
	}

	auto endMounts = msg.getBufferPosition();
	msg.setBufferPosition(startMounts);
	msg.add<uint16_t>(mountSize);
	msg.setBufferPosition(endMounts);

	msg.add<uint16_t>(0);

	msg.addByte(0x05);
	msg.addByte(lookMount ? 0x01 : 0x00);

	msg.add<uint16_t>(0);

	msg.addPosition(position);
	msg.add<uint16_t>(itemId);
	msg.addByte(stackpos);

	msg.addByte(podiumVisible ? podiumVisible->getAttribute<uint8_t>() : 0x01);
	msg.addByte(lookType ? 0x01 : 0x00);
	msg.addByte(lookDirection ? lookDirection->getAttribute<uint8_t>() : 2);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus) {
	if (oldProtocol && newStatus == VipStatus_t::Training) {
		return;
	}

	NetworkMessage msg;
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

void ProtocolGame::sendSpellCooldown(uint16_t spellId, uint32_t time) {
	NetworkMessage msg;
	msg.addByte(0xA4);
	if (oldProtocol && spellId >= 170) {
		msg.addByte(170);
	} else {
		if (oldProtocol) {
			msg.addByte(spellId);
		} else {
			msg.add<uint16_t>(spellId);
		}
	}
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA5);
	msg.addByte(groupId);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUseItemCooldown(uint32_t time) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA6);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyTimeLeft(const std::unique_ptr<PreySlot> &slot) {
	if (!player || !slot) {
		return;
	}

	NetworkMessage msg;

	msg.addByte(0xE7);
	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.add<uint16_t>(slot->bonusTimeLeft);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyData(const std::unique_ptr<PreySlot> &slot) {
	if (!player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xE8);
	std::vector<uint16_t> validRaceIds;
	for (auto raceId : slot->raceIdList) {
		if (g_monsters().getMonsterTypeByRaceId(raceId)) {
			validRaceIds.emplace_back(raceId);
		} else {
			g_logger().error("[ProtocolGame::sendPreyData] - Unknown monster type raceid: {}, removing prey slot from player {}", raceId, player->getName());
			// Remove wrong raceid from slot
			slot->removeMonsterType(raceId);
			// Send empty bytes (do not debug client)
			msg.addByte(0);
			msg.addByte(1);
			msg.add<uint32_t>(0);
			msg.addByte(0);
			writeToOutputBuffer(msg);
			return;
		}
	}

	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.addByte(static_cast<uint8_t>(slot->state));

	if (slot->state == PreyDataState_Locked) {
		msg.addByte(player->isPremium() ? 0x01 : 0x00);
	} else if (slot->state == PreyDataState_Inactive) {
		// Empty
	} else if (slot->state == PreyDataState_Active) {
		if (const auto mtype = g_monsters().getMonsterTypeByRaceId(slot->selectedRaceId)) {
			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
			} else {
				msg.addByte(outfit.lookHead);
				msg.addByte(outfit.lookBody);
				msg.addByte(outfit.lookLegs);
				msg.addByte(outfit.lookFeet);
				msg.addByte(outfit.lookAddons);
			}

			msg.addByte(static_cast<uint8_t>(slot->bonus));
			msg.add<uint16_t>(slot->bonusPercentage);
			msg.addByte(slot->bonusRarity);
			msg.add<uint16_t>(slot->bonusTimeLeft);
		}
	} else if (slot->state == PreyDataState_Selection) {
		msg.addByte(static_cast<uint8_t>(validRaceIds.size()));
		for (uint16_t raceId : validRaceIds) {
			const auto mtype = g_monsters().getMonsterTypeByRaceId(raceId);
			if (!mtype) {
				continue;
			}

			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
				continue;
			}

			msg.addByte(outfit.lookHead);
			msg.addByte(outfit.lookBody);
			msg.addByte(outfit.lookLegs);
			msg.addByte(outfit.lookFeet);
			msg.addByte(outfit.lookAddons);
		}
	} else if (slot->state == PreyDataState_SelectionChangeMonster) {
		msg.addByte(static_cast<uint8_t>(slot->bonus));
		msg.add<uint16_t>(slot->bonusPercentage);
		msg.addByte(slot->bonusRarity);
		msg.addByte(static_cast<uint8_t>(validRaceIds.size()));
		for (uint16_t raceId : validRaceIds) {
			const auto mtype = g_monsters().getMonsterTypeByRaceId(raceId);
			if (!mtype) {
				continue;
			}

			msg.addString(mtype->name);
			const Outfit_t outfit = mtype->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType == 0) {
				msg.add<uint16_t>(outfit.lookTypeEx);
				continue;
			}

			msg.addByte(outfit.lookHead);
			msg.addByte(outfit.lookBody);
			msg.addByte(outfit.lookLegs);
			msg.addByte(outfit.lookFeet);
			msg.addByte(outfit.lookAddons);
		}
	} else if (slot->state == PreyDataState_ListSelection) {
		const std::map<uint16_t, std::string> &bestiaryList = g_game().getBestiaryList();
		msg.add<uint16_t>(static_cast<uint16_t>(bestiaryList.size()));
		std::for_each(bestiaryList.begin(), bestiaryList.end(), [&msg](auto mType) {
			msg.add<uint16_t>(mType.first);
		});
	} else {
		g_logger().warn("[ProtocolGame::sendPreyData] - Unknown prey state: {}", fmt::underlying(slot->state));
		return;
	}

	if (oldProtocol) {
		auto currentTime = OTSYS_TIME();
		auto timeDiffMs = (slot->freeRerollTimeStamp > currentTime) ? (slot->freeRerollTimeStamp - currentTime) : 0;
		auto timeDiffMinutes = timeDiffMs / 60000;
		msg.add<uint16_t>(timeDiffMinutes ? timeDiffMinutes : 0);
	} else {
		msg.add<uint32_t>(std::max<uint32_t>(static_cast<uint32_t>(((slot->freeRerollTimeStamp - OTSYS_TIME()) / 1000)), 0));
		msg.addByte(static_cast<uint8_t>(slot->option));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPreyPrices() {
	if (!player) {
		return;
	}

	NetworkMessage msg;

	msg.addByte(0xE9);
	msg.add<uint32_t>(player->getPreyRerollPrice());
	if (!oldProtocol) {
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(PREY_BONUS_REROLL_PRICE)));
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(PREY_SELECTION_LIST_PRICE)));
		msg.add<uint32_t>(player->getTaskHuntingRerollPrice());
		msg.add<uint32_t>(player->getTaskHuntingRerollPrice());
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(TASK_HUNTING_SELECTION_LIST_PRICE)));
		msg.addByte(static_cast<uint8_t>(g_configManager().getNumber(TASK_HUNTING_BONUS_REROLL_PRICE)));
	}

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

////////////// Add common messages
void ProtocolGame::AddCreature(NetworkMessage &msg, const std::shared_ptr<Creature> &creature, bool known, uint32_t remove) {
	CreatureType_t creatureType = creature->getType();
	std::shared_ptr<Player> otherPlayer = creature->getPlayer();

	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		if (!oldProtocol && creature->isHealthHidden()) {
			msg.addByte(CREATURETYPE_HIDDEN);
		} else {
			msg.addByte(creatureType);
		}

		if (!oldProtocol && creatureType == CREATURETYPE_SUMMON_PLAYER) {
			if (std::shared_ptr<Creature> master = creature->getMaster()) {
				msg.add<uint32_t>(master->getID());
			} else {
				msg.add<uint32_t>(0x00);
			}
		}

		if (!oldProtocol && creature->isHealthHidden()) {
			msg.addString(std::string());
		} else {
			msg.addString(creature->getName());
		}
	}

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}

	msg.addByte(creature->getDirection());

	if (!creature->isInGhostMode() && !creature->isInvisible()) {
		const Outfit_t &outfit = creature->getCurrentOutfit();
		AddOutfit(msg, outfit);
	} else {
		static Outfit_t outfit;
		AddOutfit(msg, outfit);
	}

	LightInfo lightInfo = creature->getCreatureLight();
	msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	msg.addByte(lightInfo.color);

	msg.add<uint16_t>(creature->getStepSpeed());

	addCreatureIcon(msg, creature);

	msg.addByte(player->getSkullClient(creature));
	msg.addByte(player->getPartyShield(otherPlayer));

	if (!known) {
		msg.addByte(player->getGuildEmblem(otherPlayer));
	}

	if (!oldProtocol && creatureType == CREATURETYPE_MONSTER) {
		if (std::shared_ptr<Creature> master = creature->getMaster()) {
			if (std::shared_ptr<Player> masterPlayer = master->getPlayer()) {
				creatureType = CREATURETYPE_SUMMON_PLAYER;
			}
		}
	}

	if (!oldProtocol && creature->isHealthHidden()) {
		msg.addByte(CREATURETYPE_HIDDEN);
	} else {
		msg.addByte(creatureType); // Type (for summons)
	}

	if (!oldProtocol && creatureType == CREATURETYPE_SUMMON_PLAYER) {
		if (std::shared_ptr<Creature> master = creature->getMaster()) {
			msg.add<uint32_t>(master->getID());
		} else {
			msg.add<uint32_t>(0x00);
		}
	}

	if (!oldProtocol && creatureType == CREATURETYPE_PLAYER) {
		if (std::shared_ptr<Player> otherCreature = creature->getPlayer()) {
			msg.addByte(otherCreature->getVocation()->getClientId());
		} else {
			msg.addByte(0);
		}
	}

	auto bubble = creature->getSpeechBubble();
	msg.addByte(oldProtocol && bubble == SPEECHBUBBLE_HIRELING ? static_cast<uint8_t>(SPEECHBUBBLE_NONE) : bubble);
	msg.addByte(0xFF); // MARK_UNMARKED
	if (!oldProtocol) {
		msg.addByte(0x00); // inspection type
	} else {
		if (otherPlayer) {
			msg.add<uint16_t>(otherPlayer->getHelpers());
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);

	if (isOTCR) {
		msg.addString(creature->getShader()); // g_game.enableFeature(GameCreatureShader)
		msg.addByte(static_cast<uint8_t>(creature->getAttachedEffectList().size())); // g_game.enableFeature(GameCreatureAttachedEffect)
		for (const uint16_t id : creature->getAttachedEffectList()) {
			msg.add<uint16_t>(id); // g_game.enableFeature(GameCreatureAttachedEffect)
		}
	}
}

void ProtocolGame::AddPlayerStats(NetworkMessage &msg) {
	msg.addByte(0xA0);

	if (oldProtocol) {
		msg.add<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
		msg.add<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
	} else {
		msg.add<uint32_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<int32_t>::max()));
		msg.add<uint32_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<int32_t>::max()));
	}

	msg.add<uint32_t>(player->hasFlag(PlayerFlags_t::HasInfiniteCapacity) ? 1000000 : player->getFreeCapacity());
	if (oldProtocol) {
		msg.add<uint32_t>(player->getFreeCapacity());
	}

	msg.add<uint64_t>(player->getExperience());

	msg.add<uint16_t>(player->getLevel());
	msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));

	msg.add<uint16_t>(player->getBaseXpGain()); // base xp gain rate

	if (oldProtocol) {
		msg.add<uint16_t>(player->getVoucherXpBoost()); // xp voucher
	}

	msg.add<uint16_t>(player->getDisplayGrindingXpBoost()); // low level bonus
	msg.add<uint16_t>(player->getDisplayXpBoostPercent()); // xp boost
	msg.add<uint16_t>(player->getStaminaXpBoost()); // stamina multiplier (100 = 1.0x)

	if (!oldProtocol) {
		msg.add<uint32_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<int32_t>::max()));
		msg.add<uint32_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<int32_t>::max()));
	} else {
		msg.add<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
		msg.add<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));

		msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(player->getMagicLevel(), std::numeric_limits<uint8_t>::max())));
		msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(player->getBaseMagicLevel(), std::numeric_limits<uint8_t>::max())));
		msg.addByte(std::min<uint8_t>(static_cast<uint8_t>(player->getMagicLevelPercent()), 100));
	}

	msg.addByte(player->getSoul());

	msg.add<uint16_t>(player->getStaminaMinutes());

	msg.add<uint16_t>(player->getBaseSpeed());

	std::shared_ptr<Condition> condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	msg.add<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);

	msg.add<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);

	msg.add<uint16_t>(player->getXpBoostTime()); // xp boost time (seconds)
	msg.addByte(1); // enables exp boost in the store

	if (!oldProtocol) {
		msg.add<uint32_t>(player->getManaShield()); // remaining mana shield
		msg.add<uint32_t>(player->getMaxManaShield()); // total mana shield
	}
}

void ProtocolGame::AddPlayerSkills(NetworkMessage &msg) {
	msg.addByte(0xA1);

	if (oldProtocol) {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; ++i) {
			auto skill = static_cast<skills_t>(i);
			msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(skill), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(player->getBaseSkill(skill));
			msg.addByte(std::min<uint8_t>(100, static_cast<uint8_t>(player->getSkillPercent(skill))));
		}
	} else {
		msg.add<uint16_t>(player->getMagicLevel());
		msg.add<uint16_t>(player->getBaseMagicLevel());
		msg.add<uint16_t>(player->getLoyaltyMagicLevel());
		msg.add<uint16_t>(player->getMagicLevelPercent() * 100);

		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; ++i) {
			auto skill = static_cast<skills_t>(i);
			msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(skill), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(player->getBaseSkill(skill));
			msg.add<uint16_t>(player->getLoyaltySkill(skill));
			msg.add<uint16_t>(player->getSkillPercent(skill) * 100);
		}
	}

	// 13.10 List (U8 + U16)
	msg.addByte(0);

	// Used for Imbuement (Feather)
	msg.add<uint32_t>(player->getCapacity()); // Total Capacity
	msg.add<uint32_t>(player->getBaseCapacity()); // Base Total Capacity

	const auto flatBonus = player->calculateFlatDamageHealing();
	msg.add<uint16_t>(flatBonus); // Flat Damage and Healing Total

	const auto &weapon = player->getWeapon();
	if (weapon) {
		const ItemType &it = Item::items[weapon->getID()];
		if (it.weaponType == WEAPON_WAND) {
			msg.add<uint16_t>(it.maxHitChance);
			msg.addByte(getCipbiaElement(it.combatType));
			msg.addDouble(0.0);
			msg.addByte(0x00);
		} else if (it.weaponType == WEAPON_DISTANCE || it.weaponType == WEAPON_AMMO || it.weaponType == WEAPON_MISSILE) {
			int32_t physicalAttack = std::max<int32_t>(0, weapon->getAttack());
			physicalAttack += player->weaponProficiency().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE);
			int32_t elementalAttack = 0;
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				elementalAttack = std::max<int32_t>(0, it.abilities->elementDamage);
			}
			int32_t attackValue = physicalAttack + elementalAttack;
			if (it.weaponType == WEAPON_AMMO) {
				std::shared_ptr<Item> weaponItem = player->getWeapon(true);
				if (weaponItem) {
					attackValue += weaponItem->getAttack();
				}
			}

			int32_t distanceValue = player->getSkillLevel(SKILL_DISTANCE);
			const auto attackTotal = player->attackTotal(flatBonus, attackValue, distanceValue);

			msg.add<uint16_t>(attackTotal);
			msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

			// Converted Damage
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				if (physicalAttack) {
					msg.addDouble(elementalAttack / static_cast<double>(attackValue));
				} else {
					msg.addDouble(0.0);
				}
				msg.addByte(getCipbiaElement(it.abilities->elementType));
			} else {
				handleImbuementDamage(msg, player);
			}
		} else {
			int32_t physicalAttack = std::max<int32_t>(0, weapon->getAttack());
			physicalAttack += player->weaponProficiency().getStat(WeaponProficiencyBonus_t::ATTACK_DAMAGE);
			int32_t elementalAttack = 0;
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				elementalAttack = std::max<int32_t>(0, it.abilities->elementDamage);
			}
			int32_t weaponAttack = physicalAttack + elementalAttack;
			int32_t weaponSkill = player->getWeaponSkill(weapon);
			const auto attackTotal = player->attackTotal(flatBonus, weaponAttack, weaponSkill);

			msg.add<uint16_t>(attackTotal);
			msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

			// Converted Damage
			if (it.abilities && it.abilities->elementType != COMBAT_NONE) {
				if (physicalAttack) {
					msg.addDouble(elementalAttack / static_cast<double>(weaponAttack));
				} else {
					msg.addDouble(0);
				}
				msg.addByte(getCipbiaElement(it.abilities->elementType));
			} else {
				handleImbuementDamage(msg, player);
			}
		}
	} else {
		uint16_t attackValue = 7;
		int32_t fistValue = player->getSkillLevel(SKILL_FIST);
		const auto attackTotal = player->attackTotal(flatBonus, attackValue, fistValue);

		msg.add<uint16_t>(attackTotal);
		msg.addByte(CIPBIA_ELEMENTAL_PHYSICAL);

		msg.addDouble(0.0);
		msg.addByte(0x00);
	}

	// Imbuements
	msg.addDouble(player->getSkillLevel(SKILL_LIFE_LEECH_AMOUNT) / 10000.); // Life Leech
	msg.addDouble(player->getSkillLevel(SKILL_MANA_LEECH_AMOUNT) / 10000.); // Mana Leech
	msg.addDouble(player->getSkillLevel(SKILL_CRITICAL_HIT_CHANCE) / 10000.); // Crit Chance
	msg.addDouble(player->getSkillLevel(SKILL_CRITICAL_HIT_DAMAGE) / 10000.); // Crit Extra Damage
	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEFT)); // Onslaught

	msg.add<uint16_t>(player->getDefense(true));
	msg.add<uint16_t>(player->getArmor());
	msg.add<uint16_t>(player->getPartyMantra());
	msg.addDouble(player->getMitigation() / 100.); // Mitigation
	msg.addDouble(getForgeSkillStat(CONST_SLOT_ARMOR)); // Dodge (Ruse)
	msg.add<uint16_t>(static_cast<uint16_t>(player->getReflectFlat(COMBAT_PHYSICALDAMAGE))); // Damage Reflection

	// Store the "combats" to increase in absorb values function and send to client later
	uint8_t combats = 0;
	auto startCombats = msg.getBufferPosition();
	msg.skipBytes(1);

	// Calculate and parse the combat absorbs values
	calculateAbsorbValues(player, msg, combats, true);

	// Now set the buffer position skiped and send the total combats count
	auto endCombats = msg.getBufferPosition();
	msg.setBufferPosition(startCombats);
	msg.addByte(combats);
	msg.setBufferPosition(endCombats);

	// Forge Bonus
	msg.addDouble(getForgeSkillStat(CONST_SLOT_HEAD)); // Momentum
	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEGS)); // Transcedence
	msg.addDouble(getForgeSkillStat(CONST_SLOT_FEET, false)); // Amplification
}

void ProtocolGame::AddOutfit(NetworkMessage &msg, const Outfit_t &outfit, bool addMount /* = true*/) {
	msg.add<uint16_t>(outfit.lookType);
	if (outfit.lookType != 0) {
		msg.addByte(outfit.lookHead);
		msg.addByte(outfit.lookBody);
		msg.addByte(outfit.lookLegs);
		msg.addByte(outfit.lookFeet);
		msg.addByte(outfit.lookAddons);
	} else {
		msg.add<uint16_t>(outfit.lookTypeEx);
	}

	if (addMount) {
		msg.add<uint16_t>(outfit.lookMount);
		if (!oldProtocol && outfit.lookMount != 0) {
			msg.addByte(outfit.lookMountHead);
			msg.addByte(outfit.lookMountBody);
			msg.addByte(outfit.lookMountLegs);
			msg.addByte(outfit.lookMountFeet);
		}
	}
	if (isOTCR) {
		AddOutfitCustomOTCR(msg, outfit); // g_game.enableFeature(GameWingsAurasEffectsShader)
	}
}

void ProtocolGame::sendMessageDialog(const std::string &message) {
	NetworkMessage msg;
	msg.addByte(0xED);
	msg.addByte(0x14); // Unknown type
	msg.addString(message);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendImbuementResult(const std::string &message) {
	NetworkMessage msg;
	msg.addByte(0xED);
	msg.addByte(0x01);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddWorldLight(NetworkMessage &msg, LightInfo lightInfo) {
	msg.addByte(0x82);
	msg.addByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	msg.addByte(lightInfo.color);
}

void ProtocolGame::sendSpecialContainersAvailable() {
	if (oldProtocol || !player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x2A);
	msg.addByte(player->isStashMenuAvailable() ? 0x01 : 0x00);
	msg.addByte(player->isMarketMenuAvailable() ? 0x01 : 0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddCreatureLight(NetworkMessage &msg, const std::shared_ptr<Creature> &creature) {
	LightInfo lightInfo = creature->getCreatureLight();

	msg.addByte(0x8D);
	msg.add<uint32_t>(creature->getID());
	msg.addByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	msg.addByte(lightInfo.color);
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

void ProtocolGame::sendUpdateImpactTracker(CombatType_t type, int32_t amount) {
	if (!player || oldProtocol) {
		return;
	}

	auto clientElement = getCipbiaElement(type);
	if (clientElement == CIPBIA_ELEMENTAL_UNDEFINED) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xCC);
	if (type == COMBAT_HEALING) {
		msg.addByte(ANALYZER_HEAL);
		msg.add<uint32_t>(amount);
	} else {
		msg.addByte(ANALYZER_DAMAGE_DEALT);
		msg.add<uint32_t>(amount);
		msg.addByte(clientElement);
	}
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

void ProtocolGame::sendTaskHuntingData(const std::unique_ptr<TaskHuntingSlot> &slot) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xBB);
	msg.addByte(static_cast<uint8_t>(slot->id));
	msg.addByte(static_cast<uint8_t>(slot->state));
	if (slot->state == PreyTaskDataState_Locked) {
		msg.addByte(player->isPremium() ? 0x01 : 0x00);
	} else if (slot->state == PreyTaskDataState_Inactive) {
		// Empty
	} else if (slot->state == PreyTaskDataState_Selection) {
		std::shared_ptr<Player> user = player;
		msg.add<uint16_t>(static_cast<uint16_t>(slot->raceIdList.size()));
		std::for_each(slot->raceIdList.begin(), slot->raceIdList.end(), [&msg, user](uint16_t raceid) {
			msg.add<uint16_t>(raceid);
			msg.addByte(user->isCreatureUnlockedOnTaskHunting(g_monsters().getMonsterTypeByRaceId(raceid)) ? 0x01 : 0x00);
		});
	} else if (slot->state == PreyTaskDataState_ListSelection) {
		std::shared_ptr<Player> user = player;
		const std::map<uint16_t, std::string> &bestiaryList = g_game().getBestiaryList();
		msg.add<uint16_t>(static_cast<uint16_t>(bestiaryList.size()));
		std::for_each(bestiaryList.begin(), bestiaryList.end(), [&msg, user](auto mType) {
			msg.add<uint16_t>(mType.first);
			msg.addByte(user->isCreatureUnlockedOnTaskHunting(g_monsters().getMonsterType(mType.second)) ? 0x01 : 0x00);
		});
	} else if (slot->state == PreyTaskDataState_Active) {
		if (const auto &option = g_ioprey().getTaskRewardOption(slot)) {
			msg.add<uint16_t>(slot->selectedRaceId);
			if (slot->upgrade) {
				msg.addByte(0x01);
				msg.add<uint16_t>(option->secondKills);
			} else {
				msg.addByte(0x00);
				msg.add<uint16_t>(option->firstKills);
			}
			msg.add<uint16_t>(slot->currentKills);
			msg.addByte(slot->rarity);
		} else {
			g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown slot option {} on player {}", fmt::underlying(slot->id), player->getName());
			return;
		}
	} else if (slot->state == PreyTaskDataState_Completed) {
		if (const auto &option = g_ioprey().getTaskRewardOption(slot)) {
			msg.add<uint16_t>(slot->selectedRaceId);
			if (slot->upgrade) {
				msg.addByte(0x01);
				msg.add<uint16_t>(option->secondKills);
				msg.add<uint16_t>(std::min<uint16_t>(slot->currentKills, option->secondKills));
			} else {
				msg.addByte(0x00);
				msg.add<uint16_t>(option->firstKills);
				msg.add<uint16_t>(std::min<uint16_t>(slot->currentKills, option->firstKills));
			}
			msg.addByte(slot->rarity);
		} else {
			g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown slot option {} on player {}", fmt::underlying(slot->id), player->getName());
			return;
		}
	} else {
		g_logger().warn("[ProtocolGame::sendTaskHuntingData] - Unknown task hunting state: {}", fmt::underlying(slot->state));
		return;
	}

	msg.add<uint32_t>(std::max<uint32_t>(static_cast<uint32_t>(((slot->freeRerollTimeStamp - OTSYS_TIME()) / 1000)), 0));
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

void ProtocolGame::sendFeatures() {
	if (otclientV8 == 0) {
		return;
	}

	std::map<GameFeature_t, bool> features;
	// Place for non-standard OTCv8 features
	features[GameFeature_t::ExtendedOpcode] = true;

	if (features.empty()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x43);
	msg.add<uint16_t>(static_cast<uint16_t>(features.size()));
	for (const auto &[gameFeature, haveFeature] : features) {
		msg.addByte(static_cast<uint8_t>(gameFeature));
		msg.addByte(haveFeature ? 1 : 0);
	}
	writeToOutputBuffer(msg);
}

// OTCR
void ProtocolGame::sendOTCRFeatures() {
	isOTCR = true;
	const auto &enabledFeatures = g_configManager().getEnabledFeaturesOTC();
	const auto &disabledFeatures = g_configManager().getDisabledFeaturesOTC();
	NetworkMessage msg;
	msg.addByte(0x43);
	auto totalFeatures = static_cast<uint16_t>(enabledFeatures.size() + disabledFeatures.size());
	msg.add<uint16_t>(totalFeatures);
	for (auto feature : enabledFeatures) {
		uint8_t featureByte = feature;
		msg.addByte(featureByte);
		msg.addByte(0x01);
	}
	for (auto feature : disabledFeatures) {
		uint8_t featureByte = feature;
		msg.addByte(featureByte);
		msg.addByte(0x00);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendInventoryImbuements(const std::map<Slots_t, std::shared_ptr<Item>> &items) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x5D);

	msg.addByte(static_cast<uint8_t>(items.size()));
	for (const auto &[slot, item] : items) {
		msg.addByte(slot);
		AddItem(msg, item);

		uint8_t slots = item->getImbuementSlot();
		msg.addByte(slots);
		if (slots == 0) {
			continue;
		}

		for (uint8_t imbueSlot = 0; imbueSlot < slots; imbueSlot++) {
			ImbuementInfo imbuementInfo;
			if (!item->getImbuementInfo(imbueSlot, &imbuementInfo)) {
				msg.addByte(0x00);
				continue;
			}

			auto imbuement = imbuementInfo.imbuement;
			if (!imbuement) {
				msg.addByte(0x00);
				continue;
			}

			const BaseImbuement* baseImbuement = g_imbuements().getBaseByID(imbuement->getBaseID());
			msg.addByte(0x01);
			msg.addString(baseImbuement->name + " " + imbuement->getName());
			msg.add<uint16_t>(imbuement->getIconID());
			msg.add<uint32_t>(imbuementInfo.duration);

			std::shared_ptr<Tile> playerTile = player->getTile();
			// Check if the player is in a protection zone
			bool isInProtectionZone = playerTile && playerTile->hasFlag(TILESTATE_PROTECTIONZONE);
			// Check if the player is in fight mode
			bool isInFightMode = player->hasCondition(CONDITION_INFIGHT);
			// Get the category of the imbuement
			const CategoryImbuement* categoryImbuement = g_imbuements().getCategoryByID(imbuement->getCategory());
			// Parent of the imbued item
			auto parent = item->getParent();
			// If the imbuement is aggressive and the player is not in fight mode or is in a protection zone, or the item is in a container, ignore it.
			if (categoryImbuement && categoryImbuement->agressive && (isInProtectionZone || !isInFightMode)) {
				msg.addByte(0);
				continue;
			}
			// If the item is not in the backpack slot and it's not a agressive imbuement, ignore it.
			if (categoryImbuement && !categoryImbuement->agressive && parent && parent != player) {
				msg.addByte(0);
				continue;
			}

			msg.addByte(1);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendItemsPrice() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xCD);

	auto countBuffer = msg.getBufferPosition();
	uint16_t count = 0;
	msg.skipBytes(2);
	for (const auto &[itemId, tierAndPriceMap] : g_game().getItemsPrice()) {
		for (const auto &[tier, price] : tierAndPriceMap) {
			msg.add<uint16_t>(itemId);
			if (Item::items[itemId].upgradeClassification > 0) {
				msg.addByte(tier);
			}
			msg.add<uint64_t>(price);
			count++;
		}
	}
	msg.setBufferPosition(countBuffer);
	msg.add<uint16_t>(count);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenStash() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x29);
	StashItemList list = player->getStashItems();
	msg.add<uint16_t>(list.size());
	for (auto item : list) {
		msg.add<uint16_t>(item.first);
		msg.add<uint32_t>(item.second);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHelpers(uint32_t creatureId, uint16_t helpers) {
	if (!oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x94);
	msg.add<uint32_t>(creatureId);
	msg.add<uint16_t>(helpers);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDepotItems(const ItemsTierCountList &itemMap, uint16_t count) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x94);

	msg.add<uint16_t>(count);
	for (const auto &[key, count] : itemMap) {
		const auto &[itemId, tier] = key;

		msg.add<uint16_t>(itemId);

		if (tier > 0) {
			msg.addByte(tier - 1);
		}

		msg.add<uint16_t>(static_cast<uint16_t>(count));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseDepotSearch() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x9A);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDepotSearchResultDetail(uint16_t itemId, uint8_t tier, uint32_t depotCount, const ItemVector &depotItems, uint32_t inboxCount, const ItemVector &inboxItems, uint32_t stashCount) {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x99);
	msg.add<uint16_t>(itemId);
	if (Item::items[itemId].upgradeClassification > 0) {
		msg.addByte(tier);
	}

	msg.add<uint32_t>(depotCount);
	msg.addByte(static_cast<uint8_t>(depotItems.size()));
	for (const auto &item : depotItems) {
		AddItem(msg, item);
	}

	msg.add<uint32_t>(inboxCount);
	msg.addByte(static_cast<uint8_t>(inboxItems.size()));
	for (const auto &item : inboxItems) {
		AddItem(msg, item);
	}

	msg.addByte(stashCount > 0 ? 0x01 : 0x00);
	if (stashCount > 0) {
		msg.add<uint16_t>(itemId);
		msg.add<uint32_t>(stashCount);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateCreature(const std::shared_ptr<Creature> &creature) {
	if (oldProtocol || !creature || !player) {
		return;
	}

	auto tile = creature->getTile();
	if (!tile) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	int32_t stackPos = tile->getClientIndexOfCreature(player, creature);
	if (stackPos == -1 || stackPos >= 10) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(creature->getPosition());
	msg.addByte(static_cast<uint8_t>(stackPos));
	AddCreature(msg, creature, false, 0);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendForgeSkillStats(NetworkMessage &msg) const {
	if (oldProtocol) {
		return;
	}

	std::vector<Slots_t> slots { CONST_SLOT_LEFT, CONST_SLOT_ARMOR, CONST_SLOT_HEAD, CONST_SLOT_LEGS };
	for (const auto &slot : slots) {
		double_t skill = 0;
		const auto &item = player->getInventoryItem(slot);
		if (!item) {
			continue;
		}

		const ItemType &it = Item::items[item->getID()];
		if (it.isWeapon()) {
			skill = item->getFatalChance() * 100;
		}
		if (it.isArmor()) {
			skill = item->getDodgeChance() * 100;
		}
		if (it.isHelmet()) {
			skill = item->getMomentumChance() * 100;
		}
		if (it.isLegs()) {
			skill = item->getTranscendenceChance() * 100;
		}
		if (it.isBoots()) {
			skill = item->getAmplificationChance();
		}

		auto skillCast = static_cast<uint16_t>(skill);
		msg.add<uint16_t>(skillCast);
		msg.add<uint16_t>(skillCast);
	}
}

void ProtocolGame::sendBosstiaryData() {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x61);

	msg.add<uint16_t>(25); // Number of kills to achieve 'Bane Prowess'
	msg.add<uint16_t>(100); // Number of kills to achieve 'Bane expertise'
	msg.add<uint16_t>(300); // Number of kills to achieve 'Base Mastery'

	msg.add<uint16_t>(5); // Number of kills to achieve 'Archfoe Prowess'
	msg.add<uint16_t>(20); // Number of kills to achieve 'Archfoe Expertise'
	msg.add<uint16_t>(60); // Number of kills to achieve 'Archfoe Mastery'

	msg.add<uint16_t>(1); // Number of kills to achieve 'Nemesis Prowess'
	msg.add<uint16_t>(3); // Number of kills to achieve 'Nemesis Expertise'
	msg.add<uint16_t>(5); // Number of kills to achieve 'Nemesis Mastery'

	msg.add<uint16_t>(5); // Points will receive when reach 'Bane Prowess'
	msg.add<uint16_t>(15); // Points will receive when reach 'Bane Expertise'
	msg.add<uint16_t>(30); // Points will receive when reach 'Base Mastery'

	msg.add<uint16_t>(10); // Points will receive when reach 'Archfoe Prowess'
	msg.add<uint16_t>(30); // Points will receive when reach 'Archfoe Expertise'
	msg.add<uint16_t>(60); // Points will receive when reach 'Archfoe Mastery'

	msg.add<uint16_t>(10); // Points will receive when reach 'Nemesis Prowess'
	msg.add<uint16_t>(30); // Points will receive when reach 'Nemesis Expertise'
	msg.add<uint16_t>(60); // Points will receive when reach 'Nemesis Mastery'

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPodiumDetails(NetworkMessage &msg, const std::vector<uint16_t> &toSendMonsters, bool isBoss) const {
	auto toSendMonstersSize = static_cast<uint16_t>(toSendMonsters.size());
	msg.add<uint16_t>(toSendMonstersSize);
	for (const auto &raceId : toSendMonsters) {
		const auto mType = g_monsters().getMonsterTypeByRaceId(raceId, isBoss);
		if (!mType) {
			continue;
		}

		// Podium of tenacity only need raceId
		if (!isBoss) {
			msg.add<uint16_t>(raceId);
			continue;
		}

		auto monsterOutfit = mType->info.outfit;
		msg.add<uint16_t>(raceId);
		auto isLookType = monsterOutfit.lookType != 0;
		// "Tantugly's Head" boss have to send other looktype to the podium
		if (monsterOutfit.lookTypeEx == 35105) {
			monsterOutfit.lookTypeEx = 39003;
			msg.addString("Tentugly");
		} else {
			msg.addString(mType->name);
		}
		msg.add<uint16_t>(monsterOutfit.lookType);
		if (isLookType) {
			msg.addByte(monsterOutfit.lookHead);
			msg.addByte(monsterOutfit.lookBody);
			msg.addByte(monsterOutfit.lookLegs);
			msg.addByte(monsterOutfit.lookFeet);
			msg.addByte(monsterOutfit.lookAddons);
		} else {
			msg.add<uint16_t>(monsterOutfit.lookTypeEx);
		}
	}
}

void ProtocolGame::sendMonsterPodiumWindow(const std::shared_ptr<Item> &podium, const Position &position, uint16_t itemId, uint8_t stackPos) {
	if (!podium || oldProtocol) {
		g_logger().error("[{}] item is nullptr", __FUNCTION__);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC2);

	auto podiumVisible = podium->getCustomAttribute("PodiumVisible");
	auto lookType = podium->getCustomAttribute("LookType");
	auto lookDirection = podium->getCustomAttribute("LookDirection");

	bool isBossSelected = false;
	uint16_t lookValue = 0;
	if (lookType) {
		lookValue = static_cast<uint16_t>(lookType->getInteger());
		isBossSelected = lookValue > 0;
	}

	msg.add<uint16_t>(isBossSelected ? lookValue : 0); // Boss LookType
	if (isBossSelected) {
		auto lookHead = podium->getCustomAttribute("LookHead");
		auto lookBody = podium->getCustomAttribute("LookBody");
		auto lookLegs = podium->getCustomAttribute("LookLegs");
		auto lookFeet = podium->getCustomAttribute("LookFeet");

		msg.addByte(lookHead ? static_cast<uint8_t>(lookHead->getInteger()) : 0);
		msg.addByte(lookBody ? static_cast<uint8_t>(lookBody->getInteger()) : 0);
		msg.addByte(lookLegs ? static_cast<uint8_t>(lookLegs->getInteger()) : 0);
		msg.addByte(lookFeet ? static_cast<uint8_t>(lookFeet->getInteger()) : 0);

		auto lookAddons = podium->getCustomAttribute("LookAddons");
		msg.addByte(lookAddons ? static_cast<uint8_t>(lookAddons->getInteger()) : 0);
	} else {
		msg.add<uint16_t>(0); // Boss LookType
	}
	msg.add<uint16_t>(0); // Size of an unknown list. (No ingame visual effect)

	bool isBossPodium = podium->getID() == ITEM_PODIUM_OF_VIGOUR;
	msg.addByte(isBossPodium ? 0x01 : 0x00); // Bosstiary or bestiary
	if (isBossPodium) {
		const auto &unlockedBosses = g_ioBosstiary().getBosstiaryFinished(player, 2);
		sendPodiumDetails(msg, unlockedBosses, true);
	} else {
		const auto &unlockedMonsters = g_iobestiary().getBestiaryFinished(player);
		sendPodiumDetails(msg, unlockedMonsters, false);
	}

	msg.addPosition(position); // Position of the podium on the map
	msg.add<uint16_t>(itemId); // ClientID of the podium
	msg.addByte(stackPos); // StackPos of the podium on the map

	msg.addByte(podiumVisible ? static_cast<uint8_t>(podiumVisible->getInteger()) : 0x01); // A boolean saying if it's visible or not
	msg.addByte(lookType ? 0x01 : 0x00); // A boolean saying if there's a boss selected
	msg.addByte(lookDirection ? static_cast<uint8_t>(lookDirection->getInteger()) : 2); // Direction where the boss is looking
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBosstiaryCooldownTimer() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xBD);

	auto startBosses = msg.getBufferPosition();
	msg.skipBytes(2); // Boss count
	uint16_t bossesCount = 0;
	for (std::map<uint16_t, std::string> bossesMap = g_ioBosstiary().getBosstiaryMap();
	     const auto &[bossRaceId, _] : bossesMap) {
		const auto mType = g_ioBosstiary().getMonsterTypeByBossRaceId(bossRaceId);
		if (!mType) {
			continue;
		}

		auto timerValue = player->kv()->scoped("boss.cooldown")->get(toKey(std::to_string(bossRaceId)));
		if (!timerValue || !timerValue.has_value()) {
			continue;
		}

		auto scheduleTimerOpt = g_kv().scoped("eventscheduler")->get("boss-cooldown");
		uint8_t schedulePercentage = 0;
		if (scheduleTimerOpt) {
			schedulePercentage = static_cast<uint8_t>(scheduleTimerOpt->getNumber());
		}
		if (schedulePercentage == 0) {
			schedulePercentage = 100;
		}

		auto timer = timerValue->getNumber();
		timer = static_cast<uint32_t>(timer * schedulePercentage / 100);
		uint64_t sendTimer = timer > 0 ? static_cast<uint64_t>(timer) : 0;
		msg.add<uint32_t>(bossRaceId); // bossRaceId
		msg.add<uint64_t>(sendTimer); // Boss cooldown in seconds
		bossesCount++;
	}
	auto endBosses = msg.getBufferPosition();
	msg.setBufferPosition(startBosses);
	msg.add<uint16_t>(bossesCount);
	msg.setBufferPosition(endBosses);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBosstiaryEntryChanged(uint32_t bossid) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xE6);
	msg.add<uint32_t>(bossid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSingleSoundEffect(const Position &pos, SoundEffect_t id, SourceEffect_t source) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);
	msg.addByte(0x06); // Sound effect type
	msg.addByte(static_cast<uint8_t>(source)); // Sound source type
	msg.add<uint16_t>(static_cast<uint16_t>(id)); // Sound id
	msg.addByte(0x00); // Breaking the effects loop
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDoubleSoundEffect(
	const Position &pos,
	SoundEffect_t mainSoundId,
	SourceEffect_t mainSource,
	SoundEffect_t secondarySoundId,
	SourceEffect_t secondarySource
) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);

	// Primary sound
	msg.addByte(0x06); // Sound effect type
	msg.addByte(static_cast<uint8_t>(mainSource)); // Sound source type
	msg.add<uint16_t>(static_cast<uint16_t>(mainSoundId)); // Sound id

	// Secondary sound (Can be an array too, but not necessary here)
	msg.addByte(0x07); // Multiple effect type
	msg.addByte(0x01); // Useless ENUM (So far)
	msg.addByte(static_cast<uint8_t>(secondarySource)); // Sound source type
	msg.add<uint16_t>(static_cast<uint16_t>(secondarySoundId)); // Sound id

	msg.addByte(0x00); // Breaking the effects loop
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAmbientSoundEffect(const SoundAmbientEffect_t id) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addByte(0x00);
	msg.add<uint16_t>(id);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMusicSoundEffect(const SoundMusicEffect_t id) {
	if (oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addByte(0x01);
	msg.add<uint16_t>(id);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOpenWheelWindow(uint32_t ownerId) {
	if (!player || oldProtocol || !g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
		return;
	}

	NetworkMessage msg;
	player->wheel().sendOpenWheelWindow(msg, ownerId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDisableLoginMusic() {
	if (oldProtocol || !player || player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addByte(0x01);
	msg.add<uint16_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTakeScreenshot(Screenshot_t screenshotType) {
	if (screenshotType == SCREENSHOT_TYPE_NONE || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x75);
	msg.addByte(screenshotType);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAttachedEffect(const std::shared_ptr<Creature> &creature, uint16_t effectId) {
	if (!isOTCR) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x34);
	msg.add<uint32_t>(creature->getID());
	msg.add<uint16_t>(effectId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDetachEffect(const std::shared_ptr<Creature> &creature, uint16_t effectId) {
	if (!isOTCR) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x35);
	msg.add<uint32_t>(creature->getID());
	msg.add<uint16_t>(effectId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendShader(const std::shared_ptr<Creature> &creature, const std::string &shaderName) {
	if (!isOTCR) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x36);
	msg.add<uint32_t>(creature->getID());
	msg.addString(shaderName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMapShader(const std::string &shaderName) {
	if (!isOTCR) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x37);
	msg.addString(shaderName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPlayerTyping(const std::shared_ptr<Creature> &creature, uint8_t typing) {
	if (!isOTCR) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x38);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(typing);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddOutfitCustomOTCR(NetworkMessage &msg, const Outfit_t &outfit) {
	if (!isOTCR) {
		return;
	}

	msg.add<uint16_t>(outfit.lookWing);
	msg.add<uint16_t>(outfit.lookAura);
	msg.add<uint16_t>(outfit.lookEffect);
	const auto &shader = g_game().getAttachedEffects()->getShaderByID(outfit.lookShader);
	msg.addString(shader ? shader->name : "");
}

void ProtocolGame::sendOutfitWindowCustomOTCR(NetworkMessage &msg) {
	if (!isOTCR) {
		return;
	}
	// wings
	auto startWings = msg.getBufferPosition();
	uint16_t limitWings = std::numeric_limits<uint16_t>::max();
	uint16_t wingSize = 0;
	msg.skipBytes(1);
	const auto &wings = g_game().getAttachedEffects()->getWings();
	for (const auto &wing : wings) {
		if (player->attachedEffects().hasWing(wing)) {
			msg.add<uint16_t>(wing->id);
			msg.addString(wing->name);
			++wingSize;
		}
		if (wingSize == limitWings) {
			break;
		}
	}
	auto endWings = msg.getBufferPosition();
	msg.setBufferPosition(startWings);
	msg.add<uint8_t>(wingSize);
	msg.setBufferPosition(endWings);
	// auras
	auto startAuras = msg.getBufferPosition();
	uint16_t limitAuras = std::numeric_limits<uint16_t>::max();
	uint16_t auraSize = 0;
	msg.skipBytes(1);
	const auto &auras = g_game().getAttachedEffects()->getAuras();
	for (const auto &aura : auras) {
		if (player->attachedEffects().hasAura(aura)) {
			msg.add<uint16_t>(aura->id);
			msg.addString(aura->name);
			++auraSize;
		}
		if (auraSize == limitAuras) {
			break;
		}
	}
	auto endAuras = msg.getBufferPosition();
	msg.setBufferPosition(startAuras);
	msg.add<uint8_t>(auraSize);
	msg.setBufferPosition(endAuras);
	// effects
	auto startEffects = msg.getBufferPosition();
	uint16_t limitEffects = std::numeric_limits<uint16_t>::max();
	uint16_t effectSize = 0;
	msg.skipBytes(1);
	const auto &effects = g_game().getAttachedEffects()->getEffects();
	for (const auto &effect : effects) {
		if (player->attachedEffects().hasEffect(effect)) {
			msg.add<uint16_t>(effect->id);
			msg.addString(effect->name);
			++effectSize;
		}
		if (effectSize == limitEffects) {
			break;
		}
	}
	auto endEffects = msg.getBufferPosition();
	msg.setBufferPosition(startEffects);
	msg.add<uint8_t>(effectSize);
	msg.setBufferPosition(endEffects);
	// shader
	std::vector<const Shader*> shaders;
	for (const auto &shader : g_game().getAttachedEffects()->getShaders()) {
		if (player->attachedEffects().hasShader(shader.get())) {
			shaders.push_back(shader.get());
		}
	}
	msg.addByte(static_cast<uint8_t>(shaders.size()));
	for (const Shader* shader : shaders) {
		msg.add<uint16_t>(shader->id);
		msg.addString(shader->name);
	}
}

void ProtocolGame::sendCyclopediaHouseList(HouseMap houses) {
	NetworkMessage msg;
	msg.addByte(0xC7);
	msg.add<uint16_t>(houses.size());
	for (const auto &[clientId, houseData] : houses) {
		msg.add<uint32_t>(clientId);
		msg.addByte(0x01); // 0x00 = Renovation; 0x01 = Available

		auto houseState = houseData->getState();
		auto stateValue = magic_enum::enum_integer(houseState);
		msg.addByte(stateValue);
		if (houseState == CyclopediaHouseState::Available) {
			bool bidder = houseData->getBidderName() == player->getName();
			msg.addString(houseData->getBidderName());
			msg.addByte(bidder);
			uint8_t disableIndex = enumToValue(player->canBidHouse(clientId));
			msg.addByte(disableIndex);

			if (!houseData->getBidderName().empty()) {
				msg.add<uint32_t>(houseData->getBidEndDate());
				msg.add<uint64_t>(houseData->getHighestBid());
				if (bidder) {
					msg.add<uint64_t>(houseData->getBidHolderLimit());
				}
			}
		} else if (houseState == CyclopediaHouseState::Rented) {
			auto ownerName = IOLoginData::getNameByGuid(houseData->getOwner());
			msg.addString(ownerName);
			msg.add<uint32_t>(houseData->getPaidUntil());

			bool rented = ownerName.compare(player->getName()) == 0;
			msg.addByte(rented);
			if (rented) {
				msg.addByte(0);
				msg.addByte(0);
			}
		} else if (houseState == CyclopediaHouseState::Transfer) {
			auto ownerName = IOLoginData::getNameByGuid(houseData->getOwner());
			msg.addString(ownerName);
			msg.add<uint32_t>(houseData->getPaidUntil());

			bool isOwner = ownerName.compare(player->getName()) == 0;
			msg.addByte(isOwner);
			if (isOwner) {
				msg.addByte(0); // ?
				msg.addByte(0); // ?
			}
			msg.add<uint32_t>(houseData->getBidEndDate());
			msg.addString(houseData->getBidderName());
			msg.addByte(0); // ?
			msg.add<uint64_t>(houseData->getInternalBid());

			bool isNewOwner = player->getName() == houseData->getBidderName();
			msg.addByte(isNewOwner);
			if (isNewOwner) {
				uint8_t disableIndex = enumToValue(player->canAcceptTransferHouse(clientId));
				msg.addByte(disableIndex); // Accept Transfer Error
				msg.addByte(0); // Reject Transfer Error
			}

			if (isOwner) {
				msg.addByte(0); // Cancel Transfer Error
			}
		} else if (houseState == CyclopediaHouseState::MoveOut) {
			auto ownerName = IOLoginData::getNameByGuid(houseData->getOwner());
			msg.addString(ownerName);
			msg.add<uint32_t>(houseData->getPaidUntil());

			bool isOwner = ownerName.compare(player->getName()) == 0;
			msg.addByte(isOwner);
			if (isOwner) {
				msg.addByte(0); // ?
				msg.addByte(0); // ?
				msg.add<uint32_t>(houseData->getBidEndDate());
				msg.addByte(0);
			} else {
				msg.add<uint32_t>(houseData->getBidEndDate());
			}
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHouseAuctionMessage(uint32_t houseId, HouseAuctionType type, uint8_t index, bool bidSuccess /* = false*/) {
	NetworkMessage msg;
	const auto typeValue = enumToValue(type);

	msg.addByte(0xC3);
	msg.add<uint32_t>(houseId);
	msg.addByte(typeValue);
	if (bidSuccess && typeValue == 1) {
		msg.addByte(0x00);
	}
	msg.addByte(index);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHousesInfo() {
	NetworkMessage msg;

	uint32_t houseClientId = 0;
	const auto accountHouseCount = g_game().map.houses.getHouseCountByAccount(player->getAccountId());
	const auto house = g_game().map.houses.getHouseByPlayerId(player->getGUID());
	if (house) {
		houseClientId = house->getClientId();
	}

	msg.addByte(0xC6);
	msg.add<uint32_t>(houseClientId);
	msg.addByte(0x00);

	msg.addByte(accountHouseCount); // Houses Account

	msg.addByte(0x00);

	msg.addByte(3);
	msg.addByte(3);

	msg.addByte(0x01);

	msg.addByte(0x01);
	msg.add<uint32_t>(houseClientId);

	const auto &housesList = g_game().map.houses.getHouses();
	msg.add<uint16_t>(housesList.size());
	for (const auto &it : housesList) {
		msg.add<uint32_t>(it.second->getClientId());
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMonkData(MonkData_t type, uint8_t value) {
	NetworkMessage msg;

	msg.addByte(0xC1); // Custom opcode for monk data

	msg.addByte(enumToValue(type)); // Type of monk data (e.g., Harmony, Serenity)
	msg.addByte(value); // The value associated (e.g., enabled/disabled)

	writeToOutputBuffer(msg); // Sends the message to the client
}

void ProtocolGame::sendExivaRestrictions(
	bool isLogin /* = false */,
	const std::vector<std::string> &addedPlayerNames /* = {} */,
	const std::vector<std::string> &removedPlayerNames /* = {} */,
	const std::vector<std::string> &addedGuildNames /* = {} */,
	const std::vector<std::string> &removedGuildNames /* = {} */
) {
	const auto &restrictions = player->getExivaRestrictions();

	NetworkMessage msg;

	msg.addByte(0xCA);
	msg.addByte(restrictions.allowAll);
	msg.addByte(restrictions.allowOwnGuild);
	msg.addByte(restrictions.allowOwnParty);
	msg.addByte(restrictions.allowVipList);
	msg.addByte(restrictions.allowPlayerWhitelist);
	msg.addByte(restrictions.allowGuildWhitelist);

	if (isLogin) {
		msg.add<uint16_t>(restrictions.playerWhitelist.size());
		for (const auto &guid : restrictions.playerWhitelist) {
			msg.addString(IOLoginData::getNameByGuid(guid));
		}
		msg.add<uint16_t>(0x00);
	} else {
		msg.add<uint16_t>(addedPlayerNames.size());
		for (const auto &addedName : addedPlayerNames) {
			msg.addString(addedName);
		}
		msg.add<uint16_t>(removedPlayerNames.size());
		for (const auto &removedName : removedPlayerNames) {
			msg.addString(removedName);
		}
	}

	if (isLogin) {
		msg.add<uint16_t>(restrictions.guildWhitelist.size());
		for (const auto &guildId : restrictions.guildWhitelist) {
			auto guild = g_game().getGuild(guildId, true);
			msg.addString(guild ? guild->getName() : "");
		}
		msg.add<uint16_t>(0x00);
	} else {
		msg.add<uint16_t>(addedGuildNames.size());
		for (const auto &addedName : addedGuildNames) {
			msg.addString(addedName);
		}
		msg.add<uint16_t>(removedGuildNames.size());
		for (const auto &removedName : removedGuildNames) {
			msg.addString(removedName);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWeaponProficiency(uint16_t weaponId) {
	if (!player || oldProtocol) {
		return;
	}

	if (weaponId == 0) {
		weaponId = player->getWeaponId(true);
	}

	NetworkMessage msg;

	msg.addByte(0x5C);

	msg.add<uint16_t>(weaponId);
	msg.add<uint32_t>(weaponId > 0 ? player->weaponProficiency().getExperience(weaponId) : 0);
	msg.addByte(weaponId > 0 && player->weaponProficiency().isUpgradeAvailable(weaponId) ? 0x01 : 0x00);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWeaponProficiencyWindow(uint16_t weaponId) {
	if (!player || oldProtocol) {
		return;
	}

	if (weaponId == 0) {
		weaponId = player->getWeaponId(true);
	}

	NetworkMessage msg;
	msg.addByte(0xC4);

	msg.add<uint16_t>(weaponId);
	msg.add<uint32_t>(player->weaponProficiency().getExperience(weaponId));

	const auto &selectedPerks = player->weaponProficiency().getSelectedPerks(weaponId);

	const auto perkCount = static_cast<uint8_t>(std::min<size_t>(selectedPerks.size(), std::numeric_limits<uint8_t>::max()));
	msg.addByte(perkCount);
	for (size_t i = 0; i < perkCount; ++i) {
		const auto &perk = selectedPerks[i];
		msg.addByte(perk.level);
		msg.addByte(perk.index);
	}

	writeToOutputBuffer(msg);
}
