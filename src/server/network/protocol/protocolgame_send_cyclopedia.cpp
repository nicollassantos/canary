/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// ProtocolGame send methods: Cyclopedia, bestiary/bosstiary, highscore, achievement and house packets.
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
	const auto levelPercent = std::min<uint16_t>(static_cast<uint16_t>(player->getLevelPercent() * 100), 10000);
	msg.add<uint16_t>(levelPercent);
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
	msg.addByte(preySlotsUnlocked);

	msg.addByte(cyclopediaSummary.m_preyWildcards); // getPreyCardsObtained
	const auto weeklyExpansion = player->kv()->scoped("task-board")->scoped("general")->get("weekly-expansion-unlocked");
	msg.addByte(weeklyExpansion && weeklyExpansion->get<bool>() ? 0x01 : 0x00);
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
		auto titleName = PlayerTitle::getNameBySex(player->getSex(), title.m_maleName, title.m_femaleName);
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

	msg.addDouble(0.0); // Full hit points extra damage
	msg.addDouble(0.0); // Low hit points extra damage
	msg.addDouble(0.0); // Armor penetration
	msg.addByte(0x00); // Elemental pierces count

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

	const auto wheelMultiplier = player->wheel().getMitigationMultiplier();
	msg.addDouble(player->getMitigation() / 100.);
	msg.addDouble(0.0);
	msg.addDouble(player->getDefenseEquipment() / 10000.);
	msg.addDouble(player->getSkillLevel(SKILL_SHIELD) * player->getVocation()->mitigationFactor / 10000.);
	msg.addDouble(wheelMultiplier / 100.);

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
	msg.addDouble(getForgeSkillStat(CONST_SLOT_LEGS, false));
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

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, const std::string &text) {
	NetworkMessage msg;
	msg.addByte(0x97);
	msg.addByte(0x00);
	msg.add<uint32_t>(windowTextId);
	msg.addString(text);
	writeToOutputBuffer(msg);
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
			auto ownerName = g_playerRepository().getNameByGuid(houseData->getOwner());
			msg.addString(ownerName);
			msg.add<uint32_t>(houseData->getPaidUntil());

			bool rented = ownerName.compare(player->getName()) == 0;
			msg.addByte(rented);
			if (rented) {
				msg.addByte(0);
				msg.addByte(0);
			}
		} else if (houseState == CyclopediaHouseState::Transfer) {
			auto ownerName = g_playerRepository().getNameByGuid(houseData->getOwner());
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
			auto ownerName = g_playerRepository().getNameByGuid(houseData->getOwner());
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
