/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/bosstiary/bosstiary_slot_rules.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <algorithm>
#endif

namespace bosstiary_slot_rules {

	bool isSlotUnlocked(uint8_t slotId, size_t unlockedBosses, uint32_t bossPoints) {
		if (slotId == 1) {
			return unlockedBosses > 0;
		}
		if (slotId == 2) {
			return bossPoints >= SecondSlotPoints;
		}
		return false;
	}

	std::vector<uint16_t> selectableBosses(const SlotState &state) {
		std::vector<uint16_t> bosses;
		for (const auto boss : state.unlockedBosses) {
			if (boss != state.boostedBossId && boss != state.slotOne && boss != state.slotTwo) {
				bosses.emplace_back(boss);
			}
		}
		return bosses;
	}

	uint32_t removalPrice(uint8_t removeTimes) {
		if (removeTimes < 2) {
			return 0;
		}
		return 300'000U * removeTimes - 500'000U;
	}

	PlayerSlots startDay(PlayerSlots slots, uint32_t boostedBossId) {
		if (slots.slotOne == boostedBossId) {
			slots.slotOne = 0;
		}
		if (slots.slotTwo == boostedBossId) {
			slots.slotTwo = 0;
		}
		slots.removeTimes = 1;
		return slots;
	}

	uint16_t slotLootBonus(uint16_t pointsBonus, uint8_t bossLevel) {
		return pointsBonus + (bossLevel == MasteryLevel ? MasteryLootBonus : 0);
	}

	SlotDecision decide(const SlotState &state, uint8_t slotId, uint32_t bossId) {
		if (slotId != 1 && slotId != 2) {
			return { SlotChange::Refused, 0, "invalid slot" };
		}
		if (!isSlotUnlocked(slotId, state.unlockedBosses.size(), state.bossPoints)) {
			return { SlotChange::Refused, 0, "slot locked" };
		}

		const uint32_t current = slotId == 1 ? state.slotOne : state.slotTwo;
		const uint32_t other = slotId == 1 ? state.slotTwo : state.slotOne;
		const uint32_t removal = current != 0 && current != state.boostedBossId ? state.removePrice : 0;

		if (bossId == 0) {
			if (current == 0) {
				return { SlotChange::Refused, 0, "slot already empty" };
			}
			return { SlotChange::Remove, removal, {}, current != state.boostedBossId };
		}

		if (current != 0) {
			return { SlotChange::Refused, 0, "slot occupied" };
		}
		if (bossId == state.boostedBossId) {
			return { SlotChange::Refused, 0, "boosted boss" };
		}
		if (bossId == other) {
			return { SlotChange::Refused, 0, "in the other slot" };
		}
		if (bossId > UINT16_MAX || std::ranges::find(state.unlockedBosses, static_cast<uint16_t>(bossId)) == state.unlockedBosses.end()) {
			return { SlotChange::Refused, 0, "boss not unlocked" };
		}
		return { SlotChange::Assign, 0, {} };
	}

} // namespace bosstiary_slot_rules
