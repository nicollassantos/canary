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
			return { SlotChange::Remove, removal, {} };
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
