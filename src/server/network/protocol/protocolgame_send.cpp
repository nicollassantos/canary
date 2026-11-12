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
#include "creatures/players/player_repository.hpp"
#include "io/market_repository.hpp"
#include "io/iologindata.hpp"
#include "creatures/players/vip_repository.hpp"
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

void ProtocolGame::AddItem(NetworkMessage &msg, uint16_t id, uint8_t count, uint8_t tier) const {
	const ItemType &it = Item::items[id];

	msg.add<uint16_t>(it.id);

	if (oldProtocol && version >= 1000) {
		msg.addByte(0xFF);
	}

	if (it.stackable) {
		msg.addByte(count);
	}

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(count);
	}

	if (oldProtocol) {
		if (version >= 910) {
			if (it.animationType == ANIMATION_RANDOM) {
				msg.addByte(0xFE);
			} else if (it.animationType == ANIMATION_DESYNC) {
				msg.addByte(0xFF);
			}

			// OTCR Features
			if (isOTCR) {
				msg.addString(""); // g_game.enableFeature(GameItemShader)
			}
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

	const auto clientItemId = getViewerAwareItemId(player, item);
	const ItemType &it = Item::items[clientItemId];

	msg.add<uint16_t>(it.id);

	if (oldProtocol && version >= 1000) {
		msg.addByte(0xFF);
	}

	if (it.stackable) {
		msg.addByte(static_cast<uint8_t>(std::min<uint16_t>(std::numeric_limits<uint8_t>::max(), item->getItemCount())));
	}

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(item->getAttribute<uint8_t>(ItemAttribute_t::FLUIDTYPE));
	}

	if (oldProtocol) {
		if (version >= 910) {
			if (it.animationType == ANIMATION_RANDOM) {
				msg.addByte(0xFE);
			} else if (it.animationType == ANIMATION_DESYNC) {
				msg.addByte(0xFF);
			}

			// OTCR Features
			if (isOTCR) {
				msg.addString(item->getShader()); // g_game.enableFeature(GameItemShader)
			}
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

	if (initialConnectionBehavior.challenge.layout == ChallengeLayout::Cipsoft860LoginChallenge
	    || initialConnectionBehavior.challenge.layout == ChallengeLayout::Tibia1100LoginChallenge) {
		output->addByte(0x1F);
		challengeTimestamp = static_cast<uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
		output->add<uint32_t>(challengeTimestamp);

		challengeRandom = randNumber(generator);
		output->addByte(challengeRandom);
		output->writeLegacyInnerLength();
		output->writeChecksum(adlerChecksum(output->getOutputBuffer(), output->getLength()));

		send(output);
		return;
	}

	if (initialConnectionBehavior.challenge.layout != ChallengeLayout::CurrentLoginChallenge) {
		disconnect();
		return;
	}

	// Skip checksum
	output->skipBytes(sizeof(uint32_t));

	// Packet length & type
	output->addByte(0x01);
	output->addByte(0x1F);
	// Add timestamp & random number
	challengeTimestamp = static_cast<uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
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
	if (oldProtocol && version >= 910) {
		msg.add<uint16_t>(0x00); // Env effects
	}

	if (isCipsoft860Profile(protocolProfile)) {
		GetCipsoft860TileDescription(tile, msg);
		return;
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

void ProtocolGame::GetCipsoft860TileDescription(const std::shared_ptr<Tile> &tile, NetworkMessage &msg) {
	int32_t count = 0;
	const auto ground = tile->getGround();
	if (ground) {
		AddItem(msg, ground);
		count = 1;
	}

	static constexpr uint8_t maxStackCount = 10;
	const bool isPlayerTile = tile->getPosition() == player->getPosition();
	const uint8_t itemStackLimit = isPlayerTile ? maxStackCount - 1 : maxStackCount;
	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			AddItem(msg, *it);
			if (++count == itemStackLimit) {
				break;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	bool playerAdded = false;
	if (creatures && count < maxStackCount) {
		for (auto creature : std::ranges::reverse_view(*creatures)) {
			if (!creature || creature->isRemoved() || !creature->isAlive()) {
				continue;
			}
			if (!player->canSeeCreature(creature)) {
				continue;
			}

			if (isPlayerTile && !playerAdded && count == maxStackCount - 1) {
				creature = player;
			}

			if (creature->getID() == player->getID()) {
				playerAdded = true;
			}

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);
			if (++count == maxStackCount) {
				break;
			}
		}
	}

	if (creatures && !playerAdded && isPlayerTile && count < maxStackCount) {
		bool known;
		uint32_t removedKnown;
		checkCreatureAsKnown(player->getID(), known, removedKnown);
		AddCreature(msg, player, known, removedKnown);
		++count;
	}

	if (!items || count >= maxStackCount) {
		return;
	}

	for (auto it = ItemVector::const_reverse_iterator(items->getEndDownItem()), end = ItemVector::const_reverse_iterator(items->getBeginDownItem()); it != end; ++it) {
		AddItem(msg, *it);
		if (++count == maxStackCount) {
			return;
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
	if (!creature || !player || oldProtocol || !canSee(creature) || !knownCreatureSet.contains(creature->getID())) {
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
	if (isCipsoft860Profile(protocolProfile)) {
		msg.addByte(0x86);
		msg.add<uint32_t>(creature->getID());
		msg.addByte(color);
	} else {
		msg.addByte(0x93);
		msg.add<uint32_t>(creature->getID());
		msg.addByte(CREATURE_MARK_TIMED_SQUARE_TYPE);
		msg.addByte(color);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureMark(const std::shared_ptr<Creature> &creature, CreatureMark_t mark) {
	if (!canSee(creature) || !isCurrentProtocolProfile(protocolProfile)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x93);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(CREATURE_MARK_PERSISTENT_SQUARE_TYPE);
	msg.addByte(mark);
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

void ProtocolGame::sendReLoginWindow(uint8_t unfairFightReduction) {
	NetworkMessage msg;
	msg.addByte(0x28);

	if (version >= 1055) {
		msg.addByte(0x00);
	}
	if (version >= 862) {
		msg.addByte(unfairFightReduction);
	}
	if (!oldProtocol && version >= 1121) {
		msg.addByte(0x00); // use death redemption (boolean)
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendStats() {
	if (shouldSuppressPreLoginPacket()) {
		return;
	}

	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBasicData() {
	if (!player || version < 950) {
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
	if (!player || oldProtocol || version < 1100) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x9B);

	bool isRetro = ExpertPvp::isRetroPvpWorldType();

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
	if (!player || version < 1100) {
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
	if (version < 1100) {
		return;
	}

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

void ProtocolGame::sendClosePrivate(uint16_t channelId) {
	NetworkMessage msg;
	msg.addByte(0xB3);
	msg.add<uint16_t>(channelId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(const std::unordered_set<PlayerIcon> &iconSet, const IconBakragore iconBakragore) {
	if (shouldSuppressPreLoginPacket()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA2);

	std::bitset<static_cast<size_t>(PlayerIcon::Count)> iconsBitSet;
	using enum PlayerIcon;
	for (const auto &icon : iconSet) {
		if (oldProtocol && version < 1100) {
			if (icon == NewManaShield) {
				[[maybe_unused]] auto &updatedIcons = iconsBitSet.set(enumToValue(ManaShield));
				continue;
			}

			if (enumToValue(icon) > enumToValue(Bleeding)) {
				continue;
			}
		}

		[[maybe_unused]] auto &updatedIcons = iconsBitSet.set(enumToValue(icon));
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
	if (oldProtocol || shouldSuppressPreLoginPacket()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA2);
	msg.add<uint64_t>(0); // Send empty normal icons
	msg.addByte(enumToValue(icon));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUnjustifiedPoints(const uint8_t &dayProgress, const uint8_t &dayLeft, const uint8_t &weekProgress, const uint8_t &weekLeft, const uint8_t &monthProgress, const uint8_t &monthLeft, const uint8_t &skullDuration) {
	if (oldProtocol && version < 1100) {
		return;
	}

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
	if (oldProtocol && version < 1100) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xB8);
	msg.addByte(openPvpSituations);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendContainer(uint8_t cid, const std::shared_ptr<Container> &container, bool hasParent, uint16_t firstIndex) {
	if (!player || !container) {
		return;
	}

	if (shouldSuppressPreLoginPacket()) {
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

	if (oldProtocol && version < 984) {
		const ItemDeque &itemList = container->getItemList();
		const auto maxItemsToSend = std::min<uint32_t>(container->capacity(), itemList.size());
		msg.addByte(maxItemsToSend);

		uint32_t i = 0;
		for (auto it = itemList.begin(), end = itemList.end(); i < maxItemsToSend && it != end; ++it, ++i) {
			AddItem(msg, *it);
		}

		writeToOutputBuffer(msg);
		return;
	}

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

void ProtocolGame::sendClientCheck() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x63);
	msg.add<uint32_t>(1); // payload size
	msg.addByte(1); // raw client-check probe byte
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendGameNews() {
	if (!player || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x98);
	msg.add<uint32_t>(1); // game news category/group id
	msg.addByte(1); // bool: 0 = open, 1 = highlight
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendResourcesBalance(uint64_t money /*= 0*/, uint64_t bank /*= 0*/, uint64_t preyCards /*= 0*/, uint64_t taskHunting /*= 0*/, uint64_t forgeDust /*= 0*/, uint64_t forgeSliver /*= 0*/, uint64_t forgeCores /*= 0*/) {
	if (m_isLivestreamViewer) {
		return;
	}

	if (version < 1100 || !hasProtocolFeature(protocolProfile, ProtocolFeature::ResourceBalancePackets)) {
		return;
	}

	sendResourceBalance(RESOURCE_BANK, bank);
	sendResourceBalance(RESOURCE_INVENTORY_MONEY, money);
	sendResourceBalance(RESOURCE_PREY_CARDS, preyCards);
	sendResourceBalance(RESOURCE_TASK_HUNTING, taskHunting);
	sendResourceBalance(RESOURCE_FORGE_DUST, forgeDust);
	sendResourceBalance(RESOURCE_FORGE_SLIVER, forgeSliver);
	sendResourceBalance(RESOURCE_FORGE_CORES, forgeCores);
}

void ProtocolGame::sendResourceBalance(Resource_t resourceType, uint64_t value) {
	if (version < 1100 || !hasProtocolFeature(protocolProfile, ProtocolFeature::ResourceBalancePackets)) {
		return;
	}

	if (oldProtocol && resourceType > RESOURCE_PREY_CARDS) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xEE);
	msg.addByte(resourceType);
	if (hasProtocolFeature(protocolProfile, ProtocolFeature::CurrentPayload) && resourceBalanceUsesU32(resourceType)) {
		msg.add<uint32_t>(static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max())));
	} else {
		msg.add<uint64_t>(value);
	}
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
	if (version >= 953) {
		msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
	}
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
	if (!oldProtocol || version >= 1059) {
		msg.add<uint16_t>(creature->getBaseSpeed());
	}
	const bool usesLegacyDoubledSpeed = oldProtocol && !hasProtocolFeature(protocolProfile, ProtocolFeature::LoginSpeedFormula);
	const auto clientSpeed = usesLegacyDoubledSpeed
		? static_cast<uint16_t>(std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), static_cast<uint32_t>(speed) * 2))
		: speed;
	msg.add<uint16_t>(clientSpeed);
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
	if (shouldSuppressPreLoginPacket()) {
		return;
	}

	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPing() {
	if (player) {
		NetworkMessage msg;
		msg.addByte(oldProtocol && version < 953 ? 0x1E : 0x1D);
		writeToOutputBuffer(msg);
	}
}

void ProtocolGame::sendPingBack() {
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position &from, const Position &to, uint16_t type, SourceEffect_t source) {
	const bool useLegacyU16Effect = oldProtocol && hasProtocolFeature(protocolProfile, ProtocolFeature::MagicEffectU16);
	if (oldProtocol && !useLegacyU16Effect && type > 0xFF) {
		return;
	}
	NetworkMessage msg;
	if (oldProtocol) {
		msg.addByte(0x85);
		msg.addPosition(from);
		msg.addPosition(to);
		if (useLegacyU16Effect) {
			msg.add<uint16_t>(type);
		} else {
			msg.addByte(static_cast<uint8_t>(type));
		}
	} else {
		msg.addByte(0x83);
		msg.addPosition(from);
		msg.addByte(MAGIC_EFFECTS_CREATE_DISTANCEEFFECT);
		msg.add<uint16_t>(type);
		msg.addByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x))));
		msg.addByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int32_t>(to.y) - static_cast<int32_t>(from.y))));
		if (hasProtocolFeature(protocolProfile, ProtocolFeature::GraphicalEffectSourceByte)) {
			msg.addByte(magic_enum::enum_integer(source));
		}
		msg.addByte(MAGIC_EFFECTS_END_LOOP);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position &from, const Position &to, uint16_t type) {
	sendDistanceShoot(from, to, type, SourceEffect_t::OWN);
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

void ProtocolGame::sendMagicEffect(const Position &pos, uint16_t type, SourceEffect_t source) {
	const bool useLegacyU16Effect = oldProtocol && hasProtocolFeature(protocolProfile, ProtocolFeature::MagicEffectU16);
	if (!canSee(pos) || (oldProtocol && !useLegacyU16Effect && type > 0xFF)) {
		return;
	}

	NetworkMessage msg;
	if (oldProtocol) {
		msg.addByte(0x83);
		msg.addPosition(pos);
		if (useLegacyU16Effect) {
			msg.add<uint16_t>(type);
		} else {
			msg.addByte(static_cast<uint8_t>(type));
		}
	} else {
		msg.addByte(0x83);
		msg.addPosition(pos);
		msg.addByte(MAGIC_EFFECTS_CREATE_EFFECT);
		msg.add<uint16_t>(type);
		if (hasProtocolFeature(protocolProfile, ProtocolFeature::GraphicalEffectSourceByte)) {
			msg.addByte(magic_enum::enum_integer(source));
		}
		msg.addByte(MAGIC_EFFECTS_END_LOOP);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMagicEffect(const Position &pos, uint16_t type) {
	sendMagicEffect(pos, type, SourceEffect_t::OWN);
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
	if (hasProtocolFeature(protocolProfile, ProtocolFeature::TacticsWithoutFightMode)) {
		msg.addByte(player->chaseMode);
		msg.addByte(player->secureMode);
		msg.addByte(PVP_MODE_DOVE);
	} else {
		msg.addByte(player->fightMode);
		msg.addByte(player->chaseMode);
		msg.addByte(player->secureMode);
		if (!hasProtocolFeature(protocolProfile, ProtocolFeature::CurrentPayload)) {
			msg.addByte(ExpertPvp::isEnabled() && hasProtocolFeature(protocolProfile, ProtocolFeature::ExpertPvpModeByte) ? player->getPvpMode() : PVP_MODE_DOVE);
		}
	}
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
	msg.addByte(version >= 980 ? 0x17 : 0x0A);

	msg.add<uint32_t>(player->getID());
	msg.add<uint16_t>(SERVER_BEAT); // beat duration (50)

	if (version >= 981 && hasProtocolFeature(protocolProfile, ProtocolFeature::LoginSpeedFormula)) {
		msg.addDouble(Creature::speedA, 3);
		msg.addDouble(Creature::speedB, 3);
		msg.addDouble(Creature::speedC, 3);
	}

	// Allow bug report (Ctrl + Z)
	const bool hasInlineBugReportFlag = oldProtocol && (version >= 1054 || (protocolProfile && protocolProfile->hasFeature(ProtocolFeature::InlineLoginBugReportFlag)));
	if (hasInlineBugReportFlag) {
		if (player->getAccountType() >= ACCOUNT_TYPE_NORMAL) {
			msg.addByte(0x01);
		} else {
			msg.addByte(0x00);
		}
	}

	const bool expertPvpControlsEnabled = ExpertPvp::isEnabled() && hasProtocolFeature(protocolProfile, ProtocolFeature::ExpertPvpModeByte);
	if (version >= 1054) {
		msg.addByte(expertPvpControlsEnabled ? 0x01 : 0x00); // can change pvp framing option
	}

	if (version >= 1058) {
		msg.addByte(expertPvpControlsEnabled ? 0x01 : 0x00); // expert mode button enabled
	}

	if (version >= 1080) {
		msg.addString(g_configManager().getString(STORE_IMAGES_URL));
		msg.add<uint16_t>(static_cast<uint16_t>(g_configManager().getNumber(STORE_COIN_PACKET)));
	}

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
	if (version >= 980) {
		sendPendingStateEntered();
		sendEnterWorld();
	}
	sendMapDescription(pos);
	loggedIn = true;

	if (isLogin) {
		sendMagicEffect(pos, CONST_ME_TELEPORT);
		sendDisableLoginMusic();
	}

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<Slots_t>(i), player->getInventoryItem(static_cast<Slots_t>(i)));
	}

	sendStats();
	sendSkills();
	if (version >= 1100) {
		const bool sendModernLoginSideSystems = hasProtocolFeature(protocolProfile, ProtocolFeature::ModernLoginSideSystems);
		if (sendModernLoginSideSystems) {
			player->weaponProficiency().clearAllStats();
			if (const auto equippedWeaponId = player->getWeaponId(true); equippedWeaponId != 0) {
				player->weaponProficiency().applyPerks(equippedWeaponId, false);
			}

			player->sendWeaponProficiency();
		}

		sendBlessStatus();
		sendPremiumTrigger();
		if (sendModernLoginSideSystems) {
			sendItemsPrice();
			sendPreyPrices();
			player->sendPreyData();
			player->sendTaskHuntingData();
			sendForgingData();
		}
	}

	// gameworld light-settings
	sendWorldLight(g_game().getWorldLightInfo());

	// player light level
	sendCreatureLight(creature);

	if (player->getPlayerVocationEnum() == Vocation_t::VOCATION_MONK_CIP) {
		sendMonkData(MonkData_t::Harmony, player->getHarmony());
		const bool officialVocationData = hasProtocolFeature(protocolProfile, ProtocolFeature::OfficialVocationSpecificPlayerData);
		const auto virtue = player->getVirtue();
		if (virtue != Virtue_t::None || !officialVocationData) {
			sendMonkData(MonkData_t::Virtue, enumToValue(virtue != Virtue_t::None ? virtue : Virtue_t::Harmony));
		}
		sendMonkData(MonkData_t::Serenity, officialVocationData ? player->hasCondition(CONDITION_SERENE) : true);
	}

	if (version >= 1100) {
		sendVIPGroups();
	}

	const auto &vipEntries = g_vipRepository().getVIPEntries(player->getAccountId());

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

	if (version >= 1100) {
		sendInventoryIds();
	}
	std::shared_ptr<Item> slotItem = player->getInventoryItem(CONST_SLOT_BACKPACK);
	if (slotItem) {
		player->setMainBackpackUnassigned(slotItem->getContainer());
	}

	sendLootContainers();
	if (version >= 950) {
		sendBasicData();
	}
	if (!oldProtocol) {
		sendHousesInfo();
	}
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
		if (!oldProtocol) {
			player->sendSpellCooldowns();
		}
		g_callbacks().executeCallback(EventCallback_t::playerOnLoginComplete, player);
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
	if (oldProtocol && slot > CONST_SLOT_AMMO) {
		return;
	}

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
	if (oldProtocol) {
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

void ProtocolGame::sendSpellCooldown(uint16_t spellId, uint32_t time) {
	if (oldProtocol && version < 870) {
		return;
	}

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

////////////// Add common messages
void ProtocolGame::AddCreature(NetworkMessage &msg, const std::shared_ptr<Creature> &creature, bool known, uint32_t remove) {
	CreatureType_t creatureType = creature->getType();
	std::shared_ptr<Player> otherPlayer = creature->getPlayer();
	const bool cipsoft860 = isCipsoft860Profile(protocolProfile);

	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		if (cipsoft860) {
			msg.addString(creature->isHealthHidden() ? std::string() : creature->getName());
		} else if (!oldProtocol && creature->isHealthHidden()) {
			msg.addByte(CREATURETYPE_HIDDEN);
		} else {
			msg.addByte(creatureType);
		}

		if (!cipsoft860) {
			if (!oldProtocol && creatureType == CREATURETYPE_SUMMON_PLAYER) {
				if (auto master = creature->getMaster()) {
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

	if (!cipsoft860) {
		addCreatureIcon(msg, creature);
	}

	msg.addByte(player->getSkullClient(creature));
	msg.addByte(player->getPartyShield(otherPlayer));

	if (!known) {
		msg.addByte(player->getGuildEmblem(otherPlayer));
	}

	if (cipsoft860) {
		msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
		return;
	}

	if (!oldProtocol && creatureType == CREATURETYPE_MONSTER) {
		const auto master = creature->getMaster();
		if (master && master->getPlayer()) {
			creatureType = CREATURETYPE_SUMMON_PLAYER;
		}
	}

	if (!oldProtocol && creature->isHealthHidden()) {
		msg.addByte(CREATURETYPE_HIDDEN);
	} else {
		msg.addByte(creatureType); // Type (for summons)
	}

	if (!oldProtocol && creatureType == CREATURETYPE_SUMMON_PLAYER) {
		const auto master = creature->getMaster();
		msg.add<uint32_t>(master ? master->getID() : 0x00);
	}

	if (!oldProtocol && creatureType == CREATURETYPE_PLAYER) {
		const auto otherCreature = creature->getPlayer();
		msg.addByte(otherCreature ? otherCreature->getVocation()->getClientId() : 0);
	}

	auto bubble = creature->getSpeechBubble();
	msg.addByte(oldProtocol && bubble == SPEECHBUBBLE_HIRELING ? static_cast<uint8_t>(SPEECHBUBBLE_NONE) : bubble);
	msg.addByte(isCurrentProtocolProfile(protocolProfile) ? ExpertPvp::getSituationCreatureMark(otherPlayer, player) : CREATURE_MARK_UNMARKED);
	if (!oldProtocol) {
		msg.addByte(0x00); // inspection type
	} else if (otherPlayer) {
		msg.add<uint16_t>(otherPlayer->getHelpers());
	} else {
		msg.add<uint16_t>(0x00);
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

	if (isCipsoft860Profile(protocolProfile)) {
		uint32_t clientLevel = player->getLevel();
		uint64_t clientExperience = player->getExperience();
		uint8_t clientLevelPercent = player->getLevelPercent();
		if (clientExperience > cipsoft860MaxSignedExperience) {
			clientLevel = std::min<uint32_t>(clientLevel, cipsoft860MaxSignedExperienceLevel);
			clientExperience = Player::getExpForLevel(clientLevel);
			clientLevelPercent = 0;
		}

		msg.add<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
		msg.add<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
		msg.add<uint32_t>(player->hasFlag(PlayerFlags_t::HasInfiniteCapacity) ? 1000000 : player->getFreeCapacity());
		msg.add<uint32_t>(static_cast<uint32_t>(clientExperience));
		msg.add<uint16_t>(std::min<uint32_t>(clientLevel, std::numeric_limits<uint16_t>::max()));
		msg.addByte(std::min<uint8_t>(clientLevelPercent, 100));
		msg.add<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
		msg.add<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));
		msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(player->getMagicLevel(), std::numeric_limits<uint8_t>::max())));
		msg.addByte(std::min<uint8_t>(static_cast<uint8_t>(player->getMagicLevelPercent()), 100));
		msg.addByte(player->getSoul());
		msg.add<uint16_t>(player->getStaminaMinutes());
		return;
	}

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
	if (!oldProtocol) {
		if (hasProtocolFeature(protocolProfile, ProtocolFeature::PlayerDataLevelPercentU16)) {
			const auto levelPercent = std::min<uint16_t>(static_cast<uint16_t>(player->getLevelPercent() * 100), 10000);
			msg.add<uint16_t>(levelPercent);
		} else {
			msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));
		}
	} else {
		msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));
	}

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

	if (isCipsoft860Profile(protocolProfile)) {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; ++i) {
			auto skill = static_cast<skills_t>(i);
			msg.addByte(static_cast<uint8_t>(std::min<uint16_t>(player->getSkillLevel(skill), std::numeric_limits<uint8_t>::max())));
			msg.addByte(static_cast<uint8_t>(std::min<uint16_t>(player->getSkillPercent(skill), 100)));
		}
		return;
	}

	if (isTibia1100Profile(protocolProfile)) {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_FISHING; ++i) {
			auto skill = static_cast<skills_t>(i);
			msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(skill), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(player->getBaseSkill(skill));
			msg.addByte(std::min<uint8_t>(100, static_cast<uint8_t>(player->getSkillPercent(skill))));
		}

		for (uint8_t i = SKILL_CRITICAL_HIT_CHANCE; i <= SKILL_LAST; ++i) {
			auto skill = static_cast<skills_t>(i);
			msg.add<uint16_t>(std::min<int32_t>(player->getSkillLevel(skill), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(player->getBaseSkill(skill));
		}
		return;
	}

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
	const double totalCriticalChance = player->getBaseCritical().chance + (player->getSkillLevel(SKILL_CRITICAL_HIT_CHANCE) / 10000.0);
	msg.addDouble(std::min(totalCriticalChance, 1.0)); // Total Critical Hit Chance
	msg.addDouble(player->getBaseCritical().damage + (player->getSkillLevel(SKILL_CRITICAL_HIT_DAMAGE) / 10000.0)); // Total Critical Extra Damage
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

	if (addMount && version >= 870) {
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

void ProtocolGame::sendTakeScreenshot(Screenshot_t screenshotType, uint8_t skillId, uint16_t skillLevel, const std::string &achievementName, uint16_t raceId, uint8_t bestiaryStep) {
	if (screenshotType == SCREENSHOT_TYPE_NONE || oldProtocol) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x75);
	if (hasProtocolFeature(protocolProfile, ProtocolFeature::GameEventPayload)) {
		// 15.13+ repurposes 0x75 from a single screenshot-type byte into
		// a GameEvent payload with an event selector and event-specific fields.
		switch (screenshotType) {
			case SCREENSHOT_TYPE_ACHIEVEMENT:
				if (achievementName.empty()) {
					return;
				}
				msg.addByte(0x02);
				msg.addString(achievementName);
				break;
			case SCREENSHOT_TYPE_BESTIARYENTRYCOMPLETED:
			case SCREENSHOT_TYPE_BESTIARYENTRYUNLOCKED:
				if (raceId == 0) {
					return;
				}
				msg.addByte(0x06);
				msg.add<uint16_t>(raceId);
				msg.addByte(bestiaryStep);
				break;
			case SCREENSHOT_TYPE_LEVELUP:
				msg.addByte(0x04);
				msg.add<uint16_t>(std::min<uint32_t>(player ? player->getLevel() : 0, 0xFFFF));
				break;
			case SCREENSHOT_TYPE_SKILLUP:
				if (skillId == 0 || skillLevel == 0) {
					return;
				}
				msg.addByte(0x05);
				msg.addByte(skillId);
				msg.add<uint16_t>(skillLevel);
				break;
			case SCREENSHOT_TYPE_BOSSDEFEATED:
				msg.addByte(0x01);
				msg.addByte(0x01);
				break;
			case SCREENSHOT_TYPE_DEATHPVE:
				msg.addByte(0x01);
				msg.addByte(0x02);
				break;
			case SCREENSHOT_TYPE_DEATHPVP:
				msg.addByte(0x01);
				msg.addByte(0x03);
				break;
			case SCREENSHOT_TYPE_PLAYERKILLASSIST:
				msg.addByte(0x01);
				msg.addByte(0x04);
				break;
			case SCREENSHOT_TYPE_PLAYERKILL:
				msg.addByte(0x01);
				msg.addByte(0x05);
				break;
			case SCREENSHOT_TYPE_PLAYERATTACKING:
				msg.addByte(0x01);
				msg.addByte(0x06);
				break;
			case SCREENSHOT_TYPE_TREASUREFOUND:
				msg.addByte(0x01);
				msg.addByte(0x07);
				break;
			case SCREENSHOT_TYPE_GIFTOFLIFE:
				msg.addByte(0x01);
				msg.addByte(0x08);
				break;
			default:
				return;
		}
	} else {
		msg.addByte(screenshotType);
	}

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

void ProtocolGame::sendMonkData(MonkData_t type, uint8_t value) {
	if (!hasProtocolFeature(protocolProfile, ProtocolFeature::CustomMonkPackets) && !hasProtocolFeature(protocolProfile, ProtocolFeature::OfficialVocationSpecificPlayerData)) {
		return;
	}

	NetworkMessage msg;

	msg.addByte(0xC1); // VocationSpecificPlayerData
	msg.addByte(enumToValue(type));

	if (hasProtocolFeature(protocolProfile, ProtocolFeature::OfficialVocationSpecificPlayerData)) {
		switch (type) {
			case MonkData_t::Harmony:
				msg.addByte(value);
				break;
			case MonkData_t::Serenity:
				msg.addByte(value != 0 ? 0x01 : 0x00);
				break;
			case MonkData_t::Virtue: {
				uint16_t spellId = 0;
				switch (static_cast<Virtue_t>(value)) {
					case Virtue_t::Harmony:
						spellId = 274;
						break;
					case Virtue_t::Justice:
						spellId = 275;
						break;
					case Virtue_t::Sustain:
						spellId = 276;
						break;
					case Virtue_t::None:
						break;
				}

				const uint8_t virtueCount = spellId != 0 ? 1 : 0;
				msg.addByte(virtueCount);
				if (virtueCount != 0) {
					msg.add<uint16_t>(spellId);
				}
				break;
			}
		}
	} else {
		msg.addByte(value);
	}

	writeToOutputBuffer(msg);
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
			msg.addString(g_playerRepository().getNameByGuid(guid));
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

	if (shouldSendWeaponProficiencyDetailList(protocolProfile, clientVersionString)) {
		msg.addByte(0x00);
	}

	writeToOutputBuffer(msg);
}
