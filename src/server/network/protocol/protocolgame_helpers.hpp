/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// Internal helpers shared between protocolgame.cpp and protocolgame_send.cpp.
// Include after all required headers are already in scope.
#pragma once

/*
 * NOTE: This namespace is used so that we can add functions without having to declare them in the ".hpp/.hpp" file
 * Do not use functions only in the .cpp scope without having a namespace, it may conflict with functions in other files of the same name
 */

// This "getIteration" function will allow us to get the total number of iterations that run within a specific map
// Very useful to send the total amount in certain bytes in the ProtocolGame class
namespace {
	constexpr uint64_t PARTY_ANALYZER_THROTTLE_MS = 1000;

	std::string getMarketDetailImbuementEffect(uint16_t itemId) {
		if (const auto* imbuement = g_imbuements().getImbuementByScrollID(itemId); imbuement != nullptr) {
			return imbuement->getDescription();
		}

		return {};
	}

	template <typename T>
	uint16_t getVectorIterationIncreaseCount(T &vector) {
		uint16_t totalIterationCount = 0;
		for ([[maybe_unused]] const auto &vectorIteration : vector) {
			totalIterationCount++;
		}

		return totalIterationCount;
	}

	template <typename LookupFunc>
	void addExivaEntries(NetworkMessage &msg, std::vector<uint32_t> &whitelist, std::vector<std::string> &addedNames, std::unordered_set<uint32_t> &addedGuids, int32_t maxLimit, LookupFunc lookup) {
		const auto size = msg.get<uint16_t>();
		uint32_t lookupAttempts = 0;
		for (uint16_t i = 0; i < size; i++) {
			std::string name = msg.getString();
			if (whitelist.size() >= static_cast<size_t>(maxLimit) || addedGuids.size() >= static_cast<size_t>(maxLimit) || lookupAttempts >= static_cast<uint32_t>(maxLimit)) {
				continue;
			}

			lookupAttempts++;
			uint32_t id = lookup(name);
			if (id != 0 && addedGuids.insert(id).second && std::ranges::find(whitelist, id) == whitelist.end()) {
				whitelist.push_back(id);
				addedNames.push_back(name);
			}
		}
	}

	template <typename LookupFunc>
	void removeExivaEntries(NetworkMessage &msg, std::vector<uint32_t> &whitelist, std::vector<std::string> &removedNames, std::unordered_set<uint32_t> &removedGuids, int32_t maxLimit, LookupFunc lookup) {
		const auto size = msg.get<uint16_t>();
		uint32_t lookupAttempts = 0;
		for (uint16_t i = 0; i < size; i++) {
			std::string name = msg.getString();
			if (removedGuids.size() >= static_cast<size_t>(maxLimit) || lookupAttempts >= static_cast<uint32_t>(maxLimit)) {
				continue;
			}

			lookupAttempts++;
			uint32_t id = lookup(name);
			if (id != 0 && removedGuids.insert(id).second && std::erase(whitelist, id) > 0) {
				removedNames.push_back(name);
			}
		}
	}

	void addOutfitAndMountBytes(NetworkMessage &msg, const std::shared_ptr<Item> &item, const CustomAttribute* attribute, const std::string &head, const std::string &body, const std::string &legs, const std::string &feet, bool addAddon = false, bool addByte = false) {
		auto look = attribute->getAttribute<uint16_t>();
		msg.add<uint16_t>(look);
		if (look != 0) {
			const auto lookHead = item->getCustomAttribute(head);
			const auto lookBody = item->getCustomAttribute(body);
			const auto lookLegs = item->getCustomAttribute(legs);
			const auto lookFeet = item->getCustomAttribute(feet);

			msg.addByte(lookHead ? lookHead->getAttribute<uint8_t>() : 0);
			msg.addByte(lookBody ? lookBody->getAttribute<uint8_t>() : 0);
			msg.addByte(lookLegs ? lookLegs->getAttribute<uint8_t>() : 0);
			msg.addByte(lookFeet ? lookFeet->getAttribute<uint8_t>() : 0);

			if (addAddon) {
				const auto lookAddons = item->getCustomAttribute("LookAddons");
				msg.addByte(lookAddons ? lookAddons->getAttribute<uint8_t>() : 0);
			}
		} else {
			if (addByte) {
				msg.add<uint16_t>(0);
			}
		}
	}

	// Send bytes function for avoid repetitions
	void sendBosstiarySlotsBytes(NetworkMessage &msg, uint8_t bossRace, uint32_t bossKillCount, uint16_t bonusBossSlotOne, uint8_t killBonus, uint8_t isSlotOneInactive, uint32_t removePrice) {
		msg.addByte(bossRace); // Boss Race
		msg.add<uint32_t>(bossKillCount); // Kill Count
		msg.add<uint16_t>(bonusBossSlotOne); // Loot Bonus
		msg.addByte(killBonus); // Kill Bonus
		msg.addByte(bossRace); // Boss Race
		msg.add<uint32_t>(isSlotOneInactive == 1 ? 0 : removePrice); // Remove Price
		msg.addByte(isSlotOneInactive); // Inactive? (Only true if equal to Boosted Boss)
	}

	/**
	 * @brief Handles the imbuement damage for a player and adds it to the network message.
	 * @details This function checks if the player's weapon has any imbuements that provide combat-type damage.
	 * @details If such imbuements are found, the corresponding damage values and combat types are added to the network message.
	 * @details If no imbuement damage is found, default values are added to the message.
	 *
	 * @param msg The network message to which the imbuement damage should be added.
	 * @param player Pointer to the player for whom the imbuement damage should be handled.
	 */
	void handleImbuementDamage(NetworkMessage &msg, const std::shared_ptr<Player> &player) {
		bool imbueDmg = false;
		const auto &weapon = player->getWeapon();
		if (weapon) {
			uint8_t slots = Item::items[weapon->getID()].imbuementSlot;
			if (slots > 0) {
				for (uint8_t i = 0; i < slots; i++) {
					ImbuementInfo imbuementInfo;
					if (!weapon->getImbuementInfo(i, &imbuementInfo)) {
						continue;
					}

					if (imbuementInfo.duration > 0) {
						auto imbuement = *imbuementInfo.imbuement;
						bool hasValidCombat = imbuement.combatType != COMBAT_NONE && imbuement.combatType < COMBAT_COUNT;
						if (hasValidCombat) {
							msg.addDouble(imbuement.elementDamage / 100.);
							msg.addByte(getCipbiaElement(imbuement.combatType));
							imbueDmg = true;
							break;
						}
					}
				}
			}
		}

		if (!imbueDmg) {
			msg.addDouble(0);
			msg.addByte(0);
		}
	}

	/**
	 * @brief Calculates the absorb values for different combat types based on player's equipped items.
	 *
	 * This function calculates the absorb values for each combat type based on the items equipped by the player.
	 * The calculated absorb values are stored in the provided array.
	 *
	 * @param[in] player The pointer to the player whose equipped items are considered.
	 */
	void calculateAbsorbValues(const std::shared_ptr<Player> &player, NetworkMessage &msg, uint8_t &combats, bool fromPlayerSkills = false) {
		alignas(16) uint16_t damageModifiers[COMBAT_COUNT] = { 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000 };

		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			if (!player->isItemAbilityEnabled(static_cast<Slots_t>(slot))) {
				continue;
			}

			const auto item = player->getInventoryItem(static_cast<Slots_t>(slot));
			if (!item) {
				continue;
			}

			const ItemType &itemType = Item::items[item->getID()];
			if (!itemType.abilities) {
				continue;
			}

			for (uint16_t i = 0; i < COMBAT_COUNT; ++i) {
				damageModifiers[i] *= (std::floor(100. - itemType.abilities->absorbPercent[i]) / 100.);
			}

			uint8_t imbuementSlots = itemType.imbuementSlot;
			if (imbuementSlots > 0) {
				for (uint8_t slotId = 0; slotId < imbuementSlots; ++slotId) {
					ImbuementInfo imbuementInfo;
					if (!item->getImbuementInfo(slotId, &imbuementInfo)) {
						continue;
					}

					if (imbuementInfo.duration == 0) {
						continue;
					}

					auto imbuement = *imbuementInfo.imbuement;
					for (uint16_t combat = 0; combat < COMBAT_COUNT; ++combat) {
						const int16_t &imbuementAbsorbPercent = imbuement.absorbPercent[combat];
						if (imbuementAbsorbPercent == 0) {
							continue;
						}

						g_logger().debug("[cyclopedia damage reduction] imbued item {}, reduced {} percent, for element {}", item->getName(), imbuementAbsorbPercent, combatTypeToName(indexToCombatType(combat)));

						damageModifiers[combat] *= (std::floor(100. - imbuementAbsorbPercent) / 100.);
					}
				}
			}
		}

		for (size_t i = 0; i < COMBAT_COUNT; ++i) {
			damageModifiers[i] -= 100 * player->getAbsorbPercent(indexToCombatType(i));
			if (g_configManager().getBoolean(TOGGLE_WHEELSYSTEM)) {
				damageModifiers[i] -= player->wheel().getResistance(indexToCombatType(i));
			}

			if (damageModifiers[i] != 10000) {
				double clientModifier = (10000 - static_cast<int16_t>(damageModifiers[i])) / 10000.;
				g_logger().debug("[{}] CombatType: {}, Damage Modifier: {}, Resulting Client Modifier: {}", __FUNCTION__, i, damageModifiers[i], clientModifier);
				if (!fromPlayerSkills) {
					msg.addByte(0x04);
				}
				msg.addByte(getCipbiaElement(indexToCombatType(i)));
				msg.addDouble(clientModifier);
				++combats;
			}
		}
	}

	/**
	 * @brief Sends the container category to the network message.
	 *
	 * @note The default value is "all", which is the first enum (0). The message must always start with "All".
	 *
	 * @details for example of enum see the ContainerCategory_t
	 *
	 * @param msg The network message to send the category to.
	 */
	template <typename T>
	void sendContainerCategory(NetworkMessage &msg, const std::vector<T> &categories = {}, uint8_t categoryType = 0) {
		msg.addByte(categoryType);
		g_logger().debug("Sendding category type '{}', categories total size '{}'", categoryType, categories.size());
		msg.addByte(categories.size());
		for (auto value : categories) {
			if (value == T::All) {
				continue;
			}

			g_logger().debug("Sending category number '{}', category name '{}'", static_cast<uint8_t>(value), magic_enum::enum_name(value).data());
			msg.addByte(static_cast<uint8_t>(value));
			msg.addString(toStartCaseWithSpace(magic_enum::enum_name(value).data()));
		}
	}

	/**
	 * @brief Calculates and adds the values for different skills based on the player's equipped items and other factors.
	 *
	 * This function calculates the total, equipment-based, imbuement-based, and wheel-based contributions to a specific skill
	 * of the player. These values are then added to the provided `NetworkMessage` object.
	 *
	 * @param[in] player The pointer to the player whose skills and equipment are considered.
	 * @param[in] msg The network message to which the calculated skill values will be added.
	 * @param[in] skill The specific skill to calculate (e.g., Life Leech, Mana Leech, Critical Hit Damage, etc.).
	 */
	void addCyclopediaSkills(std::shared_ptr<Player> &player, NetworkMessage &msg, skills_t skill, const SkillsEquipment &skillEquipment) {
		const auto skillTotal = player->getSkillLevel(skill);

		double skillWheel = 0.0;
		const auto &playerWheel = player->wheel();
		if (skill == SKILL_LIFE_LEECH_AMOUNT) {
			skillWheel = playerWheel.getStat(WheelStat_t::LIFE_LEECH);
		} else if (skill == SKILL_MANA_LEECH_AMOUNT) {
			skillWheel = playerWheel.getStat(WheelStat_t::MANA_LEECH);
		} else if (skill == SKILL_CRITICAL_HIT_DAMAGE) {
			skillWheel = playerWheel.getStat(WheelStat_t::CRITICAL_DAMAGE);
			skillWheel += playerWheel.getMajorStatConditional("Combat Mastery", WheelMajor_t::CRITICAL_DMG_2);
			skillWheel += playerWheel.getMajorStatConditional("Ballistic Mastery", WheelMajor_t::CRITICAL_DMG);
			skillWheel += playerWheel.checkAvatarSkill(WheelAvatarSkill_t::CRITICAL_DAMAGE);
		}

		double skillEquipmentValue = skillEquipment.equipment + (player->weaponProficiency().getSkillBonus(skill) / 10000.);
		double skillEvent = 0.0;

		msg.addDouble(skillTotal / 10000.);
		msg.addDouble(skillEquipmentValue);
		msg.addDouble(skillEquipment.imbuement);
		msg.addDouble(skillWheel / 10000.);
		msg.addDouble(skillEvent);
	}

	void addCyclopediaCriticalSkill(std::shared_ptr<Player> &player, NetworkMessage &msg, skills_t skill, const SkillsEquipment &skillEquipment) {
		const auto &playerBaseCritical = player->getBaseCritical();

		double concoctionCritical = 0.0;
		double baseCritical = skill == SKILL_CRITICAL_HIT_CHANCE ? playerBaseCritical.chance : playerBaseCritical.damage;
		double wheelCritical = player->wheel().getStat(skill == SKILL_CRITICAL_HIT_CHANCE ? WheelStat_t::CRITICAL_CHANCE : WheelStat_t::CRITICAL_DAMAGE) / 10000.0;

		const auto &proficiencyCritical = player->weaponProficiency().getGeneralCritical();
		double proficiencyCriticalValue = skill == SKILL_CRITICAL_HIT_CHANCE ? proficiencyCritical.chance : proficiencyCritical.damage;
		double skillEquipmentValue = skillEquipment.equipment + proficiencyCriticalValue;
		const auto totalCritical = baseCritical + skillEquipmentValue + wheelCritical + skillEquipment.imbuement + concoctionCritical;

		msg.addDouble(totalCritical);
		msg.addDouble(baseCritical);
		msg.addDouble(skillEquipmentValue);
		msg.addDouble(skillEquipment.imbuement);
		msg.addDouble(wheelCritical);
		msg.addDouble(concoctionCritical);
	}
} // namespace
