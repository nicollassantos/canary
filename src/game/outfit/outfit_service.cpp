/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/outfit/outfit_service.hpp"

#include "config/configmanager.hpp"
#include "creatures/appearance/attached_effects/attached_effects.hpp"
#include "creatures/appearance/mounts/mounts.hpp"
#include "creatures/appearance/outfit/outfit.hpp"
#include "creatures/creature.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "items/item.hpp"
#include "map/spectators.hpp"

void OutfitService::playerChangeOutfit(uint32_t playerId, Outfit_t outfit, bool setMount, uint8_t isMountRandomized) {
	if (!config_.getBoolean(ALLOW_CHANGEOUTFIT)) {
		return;
	}

	const auto &player = game_.getPlayerByID(playerId);
	playerChangeOutfit(player, outfit, setMount, isMountRandomized);
}

void OutfitService::playerChangeOutfit(const std::shared_ptr<Player> &player, Outfit_t outfit, bool setMount, uint8_t isMountRandomized) {
	if (!player) {
		return;
	}

	if (player->isWearingSupportOutfit()) {
		outfit.lookMount = 0;
		isMountRandomized = 0;
	}

	player->setRandomMount(isMountRandomized);

	if (isMountRandomized && setMount && player->hasAnyMount()) {
		outfit.lookMount = Game::resolveRandomMountClientId(*game_.mounts, player->getRandomMountId());
		if (outfit.lookMount == 0) {
			isMountRandomized = 0;
			player->setRandomMount(0);
		}
	}

	const auto playerOutfit = Outfits::getInstance().getOutfitByLookType(player, outfit.lookType);
	if (!playerOutfit || !setMount) {
		outfit.lookMount = 0;
		isMountRandomized = 0;
		player->setRandomMount(0);
	}

	if (outfit.lookMount != 0 && !game_.outfitSupportsMount(outfit.lookType)) {
		outfit.lookMount = 0;
		isMountRandomized = 0;
		player->setRandomMount(0);
	}

	if (outfit.lookMount != 0) {
		const auto mount = game_.mounts->getMountByClientID(outfit.lookMount);
		if (!mount) {
			return;
		}

		if (!player->hasMount(mount)) {
			return;
		}

		std::shared_ptr<Tile> playerTile = player->getTile();
		if (!playerTile) {
			return;
		}

		const bool blockedInProtectionZone = !config_.getBoolean(TOGGLE_MOUNT_IN_PZ) && playerTile->hasFlag(TILESTATE_PROTECTIONZONE);
		if (blockedInProtectionZone) {
			outfit.lookMount = 0;
		} else {
			auto deltaSpeedChange = mount->speed;
			const auto prevMount = player->isMounted() ? game_.mounts->getMountByID(player->getCurrentMount()) : nullptr;

			if (prevMount) {
				deltaSpeedChange -= prevMount->speed;
			}
			game_.changeSpeed(player, deltaSpeedChange);
		}

		player->setCurrentMount(mount->id);

	} else if (player->isMounted()) {
		player->dismount();
	}

	if (player->canWear(outfit.lookType, outfit.lookAddons)) {
		player->defaultOutfit = outfit;

		if (player->hasCondition(CONDITION_OUTFIT)) {
			return;
		}

		internalCreatureChangeOutfit(player, outfit);
	}

	auto &playerAttachedEffects = player->attachedEffects();
	// Wings
	if (outfit.lookWing != 0) {
		const auto &wing = game_.getAttachedEffects()->getWingByID(outfit.lookWing);
		if (!wing) {
			return;
		}

		player->detachEffectById(playerAttachedEffects.getCurrentWing());
		playerAttachedEffects.setCurrentWing(wing->id);
		player->attachEffectById(wing->id);
	} else {
		if (playerAttachedEffects.isWinged()) {
			playerAttachedEffects.diswing();
		}
		player->detachEffectById(playerAttachedEffects.getCurrentWing());
		playerAttachedEffects.setWasWinged(false);
	}
	// Effect
	if (outfit.lookEffect != 0) {
		const auto &effect = game_.getAttachedEffects()->getEffectByID(outfit.lookEffect);
		if (!effect) {
			return;
		}

		player->detachEffectById(playerAttachedEffects.getCurrentEffect());
		playerAttachedEffects.setCurrentEffect(effect->id);
		player->attachEffectById(effect->id);
	} else {
		if (playerAttachedEffects.isEffected()) {
			playerAttachedEffects.diseffect();
		}
		player->detachEffectById(playerAttachedEffects.getCurrentEffect());
		playerAttachedEffects.setWasEffected(false);
	}

	// Aura
	if (outfit.lookAura != 0) {
		const auto &aura = game_.getAttachedEffects()->getAuraByID(outfit.lookAura);
		if (!aura) {
			return;
		}

		player->detachEffectById(playerAttachedEffects.getCurrentAura());
		playerAttachedEffects.setCurrentAura(aura->id);
		player->attachEffectById(aura->id);
	} else {
		if (playerAttachedEffects.isAuraed()) {
			playerAttachedEffects.disaura();
		}
		player->detachEffectById(playerAttachedEffects.getCurrentAura());
		playerAttachedEffects.setWasAuraed(false);
	}
	// Shaders
	if (outfit.lookShader != 0) {
		const auto &shaderPtr = game_.getAttachedEffects()->getShaderByID(outfit.lookShader);
		if (!shaderPtr) {
			return;
		}
		Shader* shader = shaderPtr.get();

		if (!playerAttachedEffects.hasShader(shader)) {
			return;
		}

		playerAttachedEffects.setCurrentShader(shader->id);
		playerAttachedEffects.sendShader(player, shader->name);

	} else {
		if (playerAttachedEffects.isShadered()) {
			playerAttachedEffects.disshader();
		}
		playerAttachedEffects.sendShader(player, "Outfit - Default");
		playerAttachedEffects.setWasShadered(false);
	}
}

void OutfitService::internalCreatureChangeOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit) {
	if (!g_events().eventCreatureOnChangeOutfit(creature, outfit)) {
		return;
	}

	if (!g_callbacks().checkCallback(EventCallback_t::creatureOnChangeOutfit, creature, outfit)) {
		return;
	}

	creature->setCurrentOutfit(outfit);

	if (creature->isInvisible()) {
		return;
	}

	// Send to clients
	for (const auto &spectator : Spectators().find<Player>(creature->getPosition(), true)) {
		spectator->getPlayer()->sendCreatureChangeOutfit(creature, outfit);
	}
}

void OutfitService::playerSetMonsterPodium(uint32_t playerId, uint32_t monsterRaceId, const Position &pos, uint8_t stackPos, uint16_t itemId, uint8_t direction, const std::pair<uint8_t, uint8_t> &podiumAndMonsterVisible) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player || pos.x == 0xFFFF) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || !item->isPodium() || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	const auto &tile = item->getParent() ? item->getParent()->getTile() : nullptr;
	if (!tile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (!Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		if (std::vector<Direction> listDir;
		    player->getPathTo(pos, listDir, 0, 1, true, false)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos] {
					game_.playerBrowseField(playerId, pos);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (monsterRaceId != 0) {
		item->setCustomAttribute("PodiumMonsterRaceId", static_cast<int64_t>(monsterRaceId));
	} else if (auto podiumMonsterRace = item->getCustomAttribute("PodiumMonsterRaceId")) {
		monsterRaceId = static_cast<uint32_t>(podiumMonsterRace->getInteger());
	}

	const auto mType = g_monsters().getMonsterTypeByRaceId(static_cast<uint16_t>(monsterRaceId), itemId == ITEM_PODIUM_OF_VIGOUR);
	if (!mType) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		g_logger().debug("[{}] player {} is trying to add invalid monster to podium {}", __FUNCTION__, player->getName(), item->getName());
		return;
	}

	const auto [podiumVisible, monsterVisible] = podiumAndMonsterVisible;
	bool changeTentuglyName = false;
	if (auto monsterOutfit = mType->info.outfit;
	    (monsterOutfit.lookType != 0 || monsterOutfit.lookTypeEx != 0) && monsterVisible) {
		// "Tantugly's Head" boss have to send other looktype to the podium
		if (monsterOutfit.lookTypeEx == 35105) {
			monsterOutfit.lookTypeEx = 39003;
			changeTentuglyName = true;
		}
		item->setCustomAttribute("LookTypeEx", static_cast<int64_t>(monsterOutfit.lookTypeEx));
		item->setCustomAttribute("LookType", static_cast<int64_t>(monsterOutfit.lookType));
		item->setCustomAttribute("LookHead", static_cast<int64_t>(monsterOutfit.lookHead));
		item->setCustomAttribute("LookBody", static_cast<int64_t>(monsterOutfit.lookBody));
		item->setCustomAttribute("LookLegs", static_cast<int64_t>(monsterOutfit.lookLegs));
		item->setCustomAttribute("LookFeet", static_cast<int64_t>(monsterOutfit.lookFeet));
		item->setCustomAttribute("LookAddons", static_cast<int64_t>(monsterOutfit.lookAddons));
	} else {
		item->removeCustomAttribute("LookType");
	}

	item->setCustomAttribute("PodiumVisible", static_cast<int64_t>(podiumVisible));
	item->setCustomAttribute("LookDirection", static_cast<int64_t>(direction));
	item->setCustomAttribute("MonsterVisible", static_cast<int64_t>(monsterVisible));

	// Change Podium name
	if (monsterVisible) {
		std::ostringstream name;
		item->removeAttribute(ItemAttribute_t::NAME);
		name << item->getName() << " displaying ";
		if (changeTentuglyName) {
			name << "Tentugly";
		} else {
			name << mType->name;
		}
		item->setAttribute(ItemAttribute_t::NAME, name.str());
	} else {
		item->removeAttribute(ItemAttribute_t::NAME);
	}

	for (const auto &spectator : Spectators().find<Player>(pos, true)) {
		spectator->getPlayer()->sendUpdateTileItem(tile, pos, item);
	}

	player->updateUIExhausted();
}

void OutfitService::playerRotatePodium(uint32_t playerId, const Position &pos, uint8_t stackPos, uint16_t itemId) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	const std::shared_ptr<Thing> &thing = game_.internalGetThing(player, pos, stackPos, itemId, STACKPOS_TOPDOWN_ITEM);
	if (!thing) {
		return;
	}

	const auto &item = thing->getItem();
	if (!item || item->getID() != itemId || item->hasAttribute(ItemAttribute_t::UNIQUEID)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	if (pos.x != 0xFFFF && !Position::areInRange<1, 1, 0>(pos, player->getPosition())) {
		if (std::vector<Direction> listDir;
		    player->getPathTo(pos, listDir, 0, 1, true, true)) {
			g_dispatcher().addEvent([this, playerId = player->getID(), listDir] { game_.playerAutoWalk(playerId, listDir); }, __FUNCTION__);
			const auto &task = game_.createPlayerTask(
				400,
				[this, playerId, pos, stackPos, itemId] {
					playerRotatePodium(playerId, pos, stackPos, itemId);
				},
				__FUNCTION__
			);
			player->setNextWalkActionTask(task);
		} else {
			player->sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		}
		return;
	}

	if (config_.getBoolean(ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS) && !InternalGame::playerCanUseItemOnHouseTile(player, item)) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	auto podiumRaceIdAttribute = item->getCustomAttribute("PodiumMonsterRaceId");
	auto lookDirection = item->getCustomAttribute("LookDirection");
	auto podiumVisible = item->getCustomAttribute("PodiumVisible");
	auto monsterVisible = item->getCustomAttribute("MonsterVisible");

	auto podiumRaceId = podiumRaceIdAttribute ? static_cast<uint16_t>(podiumRaceIdAttribute->getInteger()) : 0;
	uint8_t directionValue;
	if (lookDirection) {
		directionValue = static_cast<uint8_t>(lookDirection->getInteger() >= 3 ? 0 : lookDirection->getInteger() + 1);
	} else {
		directionValue = 2;
	}
	auto isPodiumVisible = podiumVisible ? static_cast<bool>(podiumVisible->getInteger()) : false;
	bool isMonsterVisible = monsterVisible ? static_cast<bool>(monsterVisible->getInteger()) : false;

	// Rotate monster podium (bestiary or bosstiary) to the new direction
	bool isPodiumOfRenown = itemId == ITEM_PODIUM_OF_RENOWN1 || itemId == ITEM_PODIUM_OF_RENOWN2;
	if (!isPodiumOfRenown) {
		auto lookTypeExAttribute = item->getCustomAttribute("LookTypeEx");
		if (!isMonsterVisible || podiumRaceId == 0 || (lookTypeExAttribute && lookTypeExAttribute->getInteger() == 39003)) {
			player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return;
		}

		playerSetMonsterPodium(playerId, podiumRaceId, pos, stackPos, itemId, directionValue, std::make_pair(isPodiumVisible, isMonsterVisible));
		return;
	}

	// We retrieve the outfit information to be able to rotate the podium of renown in the new direction
	Outfit_t newOutfit;
	newOutfit.lookType = InternalGame::getCustomAttributeValue<uint16_t>(item, "LookType");
	newOutfit.lookAddons = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookAddons");
	newOutfit.lookHead = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookHead");
	newOutfit.lookBody = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookBody");
	newOutfit.lookLegs = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookLegs");
	newOutfit.lookFeet = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookFeet");

	newOutfit.lookMount = InternalGame::getCustomAttributeValue<uint16_t>(item, "LookMount");
	newOutfit.lookMountHead = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookMountHead");
	newOutfit.lookMountBody = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookMountBody");
	newOutfit.lookMountLegs = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookMountLegs");
	newOutfit.lookMountFeet = InternalGame::getCustomAttributeValue<uint8_t>(item, "LookMountFeet");
	if (newOutfit.lookType == 0 && newOutfit.lookMount == 0) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	game_.playerSetShowOffSocket(player->getID(), newOutfit, pos, stackPos, itemId, isPodiumVisible, directionValue);
}
