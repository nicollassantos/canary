/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstddef>
	#include <cstdint>
	#include <string_view>
	#include <vector>
#endif

/// The two Bosstiary slots: a boss in a slot gives extra loot rolls in its reward chest, so which boss
/// may go where is decided here, from facts the caller gathers (no Player, no global state), and both
/// the selection (Game::playerBosstiarySlot) and the window (ProtocolGame) use it.
namespace bosstiary_slot_rules {
	/// Boss points that open the second slot.
	inline constexpr uint32_t SecondSlotPoints = 1500;

	/// Tibia's rule (TibiaWiki BR, Bosstiário): the first slot opens with the first Prowess unlocked on any
	/// boss, the second at SecondSlotPoints. Slots other than 1 and 2 never.
	bool isSlotUnlocked(uint8_t slotId, size_t unlockedBosses, uint32_t bossPoints);

	struct SlotState {
		uint32_t slotOne = 0;
		uint32_t slotTwo = 0;
		uint32_t boostedBossId = 0;
		/// Bosses the player unlocked (IOBosstiary::getBosstiaryFinished).
		std::vector<uint16_t> unlockedBosses;
		uint32_t bossPoints = 0;
		/// What removing a boss from a slot costs now (removalPrice).
		uint32_t removePrice = 0;
	};

	enum class SlotChange : uint8_t {
		Refused,
		Assign,
		Remove,
	};

	struct SlotDecision {
		SlotChange change = SlotChange::Refused;
		/// Gold to pay first: removing a boss, except one that became today's boosted boss.
		uint32_t price = 0;
		std::string_view reason;
		/// The removal counts as one of today's changes (Player::addRemoveTime): every removal but that of a
		/// boss that became today's boosted one.
		bool countsAsChange = false;
	};

	/// Tibia's manual (Bosstiary): "You can change the boss in a boss slot for free once after each server
	/// save. All further exchanges require a certain amount of gold, which will be raised with each change
	/// on the same day." The manual gives no amounts: upstream's are kept (100k, 400k, 700k, ...).
	/// `removeTimes` is player_bosstiary.removeTimes: 1 + the changes since the boss of the day was drawn
	/// (IServerConfigRepository::storeBoostedBoss sets it back to 1).
	uint32_t removalPrice(uint8_t removeTimes);

	/// Bosses the slots window offers: unlocked, not today's boosted one (it has its own slot) and not in a
	/// slot already, in the order given. Exactly the bosses `decide` assigns to an empty open slot.
	std::vector<uint16_t> selectableBosses(const SlotState &state);

	/// Kill level at which a boss is mastered (IOBosstiary::levelInfos has three levels).
	inline constexpr uint8_t MasteryLevel = 3;
	/// Tibia (TibiaWiki BR, Bosstiário): "Mastery concede a você um bônus adicional de 25% de saque de
	/// equipamento quando este boss é designado a um boss slot".
	inline constexpr uint16_t MasteryLootBonus = 25;

	/// Equipment loot bonus of a slot boss: the bonus of the player's boss points, plus MasteryLootBonus for a
	/// mastered boss. Shown in the slots window and used by the reward chest (Player:getBossBonus).
	uint16_t slotLootBonus(uint16_t pointsBonus, uint8_t bossLevel);

	/// What selecting `bossId` for `slotId` does; `bossId` 0 removes the slot's boss. As in the Tibia client,
	/// a boss goes only into an empty open slot (an occupied one offers only "Remove", at its price), and
	/// must be unlocked, not today's boosted one (it has its own slot) and not in the other slot.
	SlotDecision decide(const SlotState &state, uint8_t slotId, uint32_t bossId);
}
