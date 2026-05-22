/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "game/forge/forge_service.hpp"
#include "config/configmanager.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/players/player.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "items/item.hpp"
#include "items/containers/container.hpp"
#include "items/items.hpp"
#include "kv/kv.hpp"
#include "lib/metrics/metrics.hpp"
#include "utils/tools.hpp"

void ForgeService::playerForgeFuseItems(uint32_t playerId, ForgeAction_t actionType, uint16_t firstItemId, uint8_t tier, uint16_t secondItemId, bool usedCore, bool reduceTierLoss, bool convergence) {
	metrics::method_latency measure(__METRICS_METHOD_NAME__);
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();

	uint8_t coreCount = (usedCore ? 1 : 0) + (reduceTierLoss ? 1 : 0);
	auto baseSuccess = static_cast<uint8_t>(g_configManager().getNumber(FORGE_BASE_SUCCESS_RATE));
	if (const auto scopedForgeChance = g_kv().scoped("eventscheduler")->get("forge-chance")) {
		auto forgeChance = static_cast<int>(scopedForgeChance->getNumber());
		int adjustedSuccess = baseSuccess + forgeChance - 100;
		baseSuccess = static_cast<uint8_t>(std::clamp(adjustedSuccess, 0, 100));
	}
	auto coreSuccess = usedCore ? g_configManager().getNumber(FORGE_BONUS_SUCCESS_RATE) : 0;
	auto finalRate = baseSuccess + coreSuccess;
	auto roll = static_cast<uint8_t>(uniform_random(1, 100)) <= finalRate;

	bool success = roll ? true : false;

	auto chance = uniform_random(0, 10000);
	uint8_t bonus = convergence ? 0 : forgeBonus(chance);

	player->forgeFuseItems(actionType, firstItemId, tier, secondItemId, success, reduceTierLoss, convergence, bonus, coreCount);
}

void ForgeService::playerForgeTransferItemTier(uint32_t playerId, ForgeAction_t actionType, uint16_t donorItemId, uint8_t tier, uint16_t receiveItemId, bool convergence) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();
	player->forgeTransferItemTier(actionType, donorItemId, tier, receiveItemId, convergence);
}

void ForgeService::playerForgeResourceConversion(uint32_t playerId, ForgeAction_t actionType) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();
	player->forgeResourceConversion(actionType);
}

void ForgeService::playerBrowseForgeHistory(uint32_t playerId, uint8_t page) {
	const auto &player = game_.getPlayerByID(playerId);
	if (!player) {
		return;
	}

	if (player->isUIExhausted()) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return;
	}

	player->updateUIExhausted();
	player->forgeHistory(page);
}

uint32_t ForgeService::makeInfluencedMonster() {
	if (auto influencedLimit = g_configManager().getNumber(FORGE_INFLUENCED_CREATURES_LIMIT);
	    game_.forgeableMonsters.empty() || game_.influencedMonsters.size() >= influencedLimit) {
		return 0;
	}

	auto maxTries = game_.forgeableMonsters.size();
	uint16_t tries = 0;
	std::shared_ptr<Monster> monster = nullptr;
	while (true) {
		if (tries == maxTries) {
			return 0;
		}

		tries++;

		auto random = static_cast<uint32_t>(normal_random(0, static_cast<int32_t>(game_.forgeableMonsters.size() - 1)));
		auto monsterId = game_.forgeableMonsters.at(random);
		monster = game_.getMonsterByID(monsterId);
		if (monster == nullptr) {
			continue;
		}

		if (monster->getForgeStack() == 0) {
			auto it = std::ranges::find(game_.forgeableMonsters.begin(), game_.forgeableMonsters.end(), monsterId);
			if (it == game_.forgeableMonsters.end()) {
				monster = nullptr;
				continue;
			}
			game_.forgeableMonsters.erase(it);
			break;
		}
	}

	if (monster && monster->canBeForgeMonster()) {
		monster->setMonsterForgeClassification(ForgeClassifications_t::FORGE_INFLUENCED_MONSTER);
		monster->configureForgeSystem();
		game_.influencedMonsters.emplace(monster->getID());
		return monster->getID();
	}

	return 0;
}

uint32_t ForgeService::makeFiendishMonster(uint32_t forgeableMonsterId /* = 0*/, bool createForgeableMonsters /* = false*/) {
	if (createForgeableMonsters) {
		game_.forgeableMonsters.clear();
		for (const auto &monster : game_.getMonsters()) {
			auto monsterTile = monster->getTile();
			if (!monster || !monsterTile) {
				continue;
			}

			if (monster->canBeForgeMonster() && !monsterTile->hasFlag(TILESTATE_NOLOGOUT)) {
				game_.forgeableMonsters.push_back(monster->getID());
			}
		}
		for (const auto monsterId : game_.getFiendishMonsters()) {
			auto monster = game_.getMonsterByID(monsterId);
			if (!monster) {
				game_.removeFiendishMonster(monsterId);
				continue;
			}

			if (auto fiendishLimit = g_configManager().getNumber(FORGE_FIENDISH_CREATURES_LIMIT);
			    game_.getFiendishMonsters().size() >= fiendishLimit) {
				monster->clearFiendishStatus();
				game_.removeFiendishMonster(monsterId);
				break;
			}
		}
	}

	if (auto fiendishLimit = g_configManager().getNumber(FORGE_FIENDISH_CREATURES_LIMIT);
	    game_.forgeableMonsters.empty() || game_.fiendishMonsters.size() >= fiendishLimit) {
		return 0;
	}

	auto maxTries = game_.forgeableMonsters.size();
	uint16_t tries = 0;
	std::shared_ptr<Monster> monster = nullptr;
	while (true) {
		if (tries == maxTries) {
			return 0;
		}

		tries++;

		auto random = static_cast<uint32_t>(uniform_random(0, static_cast<int32_t>(game_.forgeableMonsters.size() - 1)));
		uint32_t fiendishMonsterId = forgeableMonsterId;
		if (fiendishMonsterId == 0) {
			fiendishMonsterId = game_.forgeableMonsters.at(random);
		}
		monster = game_.getMonsterByID(fiendishMonsterId);
		if (monster == nullptr) {
			continue;
		}

		if (monster->getForgeStack() == 0) {
			auto it = std::find(game_.forgeableMonsters.begin(), game_.forgeableMonsters.end(), fiendishMonsterId);
			if (it == game_.forgeableMonsters.end()) {
				monster = nullptr;
				continue;
			}
			game_.forgeableMonsters.erase(it);
			break;
		}
	}

	std::string saveIntervalType = g_configManager().getString(FORGE_FIENDISH_INTERVAL_TYPE);
	auto saveIntervalConfigTime = std::atoi(g_configManager().getString(FORGE_FIENDISH_INTERVAL_TIME).c_str());
	int intervalTime = 0;
	time_t timeToChangeFiendish;
	if (saveIntervalType == "second") {
		intervalTime = 1000;
		timeToChangeFiendish = 1;
	} else if (saveIntervalType == "minute") {
		intervalTime = 60 * 1000;
		timeToChangeFiendish = 60;
	} else if (saveIntervalType == "hour") {
		intervalTime = 60 * 60 * 1000;
		timeToChangeFiendish = 3600;
	} else {
		timeToChangeFiendish = 3600;
	}

	uint32_t finalTime = 0;
	if (intervalTime == 0) {
		g_logger().warn("Fiendish interval type is wrong, setting default time to 1h");
		finalTime = 3600 * 1000;
	} else {
		finalTime = static_cast<uint32_t>(saveIntervalConfigTime * intervalTime);
	}

	if (monster && monster->canBeForgeMonster()) {
		monster->setMonsterForgeClassification(ForgeClassifications_t::FORGE_FIENDISH_MONSTER);
		monster->configureForgeSystem();
		monster->setTimeToChangeFiendish(timeToChangeFiendish + getTimeNow());
		game_.fiendishMonsters.emplace(monster->getID());

		auto schedulerTask = game_.createPlayerTask(
			finalTime,
			[this, monster] { updateFiendishMonsterStatus(monster->getID(), monster->getName()); },
			__FUNCTION__
		);
		game_.forgeMonsterEventIds[monster->getID()] = g_dispatcher().scheduleEvent(schedulerTask);
		return monster->getID();
	}

	return 0;
}

void ForgeService::updateFiendishMonsterStatus(uint32_t monsterId, const std::string &monsterName) {
	const auto &monster = game_.getMonsterByID(monsterId);
	if (!monster) {
		g_logger().warn("[{}] Failed to update monster with id {} and name {}, monster not found", __FUNCTION__, monsterId, monsterName);
		return;
	}

	monster->clearFiendishStatus();
	game_.removeFiendishMonster(monsterId, false);
	makeFiendishMonster();
}

bool ForgeService::removeForgeMonster(uint32_t id, ForgeClassifications_t monsterForgeClassification, bool create) {
	if (monsterForgeClassification == ForgeClassifications_t::FORGE_FIENDISH_MONSTER) {
		game_.removeFiendishMonster(id, create);
	} else if (monsterForgeClassification == ForgeClassifications_t::FORGE_INFLUENCED_MONSTER) {
		game_.removeInfluencedMonster(id, create);
	}

	return true;
}

void ForgeService::updateForgeableMonsters() {
	game_.forgeableMonsters.clear();
	for (const auto &monster : game_.getMonsters()) {
		const auto &monsterTile = monster->getTile();
		if (!monsterTile) {
			continue;
		}

		if (monster->canBeForgeMonster() && !monsterTile->hasFlag(TILESTATE_NOLOGOUT)) {
			game_.forgeableMonsters.emplace_back(monster->getID());
		}
	}

	for (const auto &monsterId : game_.getFiendishMonsters()) {
		if (!game_.getMonsterByID(monsterId)) {
			game_.removeFiendishMonster(monsterId);
		}
	}

	for (const auto &monsterId : game_.getInfluencedMonsters()) {
		if (!game_.getMonsterByID(monsterId)) {
			game_.removeInfluencedMonster(monsterId, false);
		}
	}

	uint32_t fiendishLimit = g_configManager().getNumber(FORGE_FIENDISH_CREATURES_LIMIT);
	if (game_.fiendishMonsters.size() < fiendishLimit) {
		createFiendishMonsters();
	}

	uint32_t influencedLimit = g_configManager().getNumber(FORGE_INFLUENCED_CREATURES_LIMIT);
	if (game_.influencedMonsters.size() < influencedLimit) {
		createInfluencedMonsters();
	}
}

void ForgeService::createFiendishMonsters() {
	uint32_t fiendishLimit = g_configManager().getNumber(FORGE_FIENDISH_CREATURES_LIMIT);
	if (game_.fiendishMonsters.size() >= fiendishLimit) {
		return;
	}

	uint32_t noProgressAttempts = 0;
	uint32_t remaining = fiendishLimit - static_cast<uint32_t>(game_.fiendishMonsters.size());
	uint32_t maxAttemptsWithoutProgress = remaining * 4;
	if (maxAttemptsWithoutProgress < 25) {
		maxAttemptsWithoutProgress = 25;
	}

	while (game_.fiendishMonsters.size() < fiendishLimit) {
		const auto previousSize = game_.fiendishMonsters.size();

		makeFiendishMonster();

		if (game_.fiendishMonsters.size() > previousSize) {
			noProgressAttempts = 0;
			continue;
		}

		noProgressAttempts++;
		if (noProgressAttempts >= maxAttemptsWithoutProgress) {
			g_logger().warn("[{}] - Aborting fiendish creation due to no progress. Size: {}, max: {}, attempts: {}.", __FUNCTION__, game_.fiendishMonsters.size(), fiendishLimit, noProgressAttempts);
			return;
		}
	}
}

void ForgeService::createInfluencedMonsters() {
	uint32_t influencedLimit = g_configManager().getNumber(FORGE_INFLUENCED_CREATURES_LIMIT);
	if (game_.influencedMonsters.size() >= influencedLimit) {
		return;
	}

	uint32_t noProgressAttempts = 0;
	uint32_t remaining = influencedLimit - static_cast<uint32_t>(game_.influencedMonsters.size());
	uint32_t maxAttemptsWithoutProgress = remaining * 4;
	if (maxAttemptsWithoutProgress < 25) {
		maxAttemptsWithoutProgress = 25;
	}

	while (game_.influencedMonsters.size() < influencedLimit) {
		const auto previousSize = game_.influencedMonsters.size();

		makeInfluencedMonster();

		if (game_.influencedMonsters.size() > previousSize) {
			noProgressAttempts = 0;
			continue;
		}

		noProgressAttempts++;
		if (noProgressAttempts >= maxAttemptsWithoutProgress) {
			g_logger().warn("[{}] - Aborting influenced creation due to no progress. Size: {}, max: {}, attempts: {}.", __FUNCTION__, game_.influencedMonsters.size(), influencedLimit, noProgressAttempts);
			return;
		}
	}
}

void ForgeService::checkForgeEventId(uint32_t monsterId) {
	auto find = game_.forgeMonsterEventIds.find(monsterId);
	if (find != game_.forgeMonsterEventIds.end()) {
		g_dispatcher().stopEvent(find->second);
		game_.forgeMonsterEventIds.erase(find);
	}
}

bool ForgeService::addInfluencedMonster(const std::shared_ptr<Monster> &monster, uint16_t stack /* = 0 */) {
	if (monster && monster->canBeForgeMonster()) {
		std::erase_if(game_.influencedMonsters, [this](const auto monsterId) {
			return game_.getMonsterByID(monsterId) == nullptr;
		});

		const auto monsterId = monster->getID();
		const bool alreadyInfluenced = game_.influencedMonsters.contains(monsterId);
		if (auto maxInfluencedMonsters = static_cast<uint32_t>(g_configManager().getNumber(FORGE_INFLUENCED_CREATURES_LIMIT));
		    !alreadyInfluenced && (game_.influencedMonsters.size() + 1) > maxInfluencedMonsters) {
			return false;
		}

		monster->setMonsterForgeClassification(ForgeClassifications_t::FORGE_INFLUENCED_MONSTER);
		monster->configureForgeSystem(stack);
		game_.influencedMonsters.emplace(monsterId);
		return true;
	}
	return false;
}

bool ForgeService::addItemStoreInbox(const std::shared_ptr<Player> &player, uint32_t itemId) {
	const auto &decoKit = Item::CreateItem(ITEM_DECORATION_KIT, 1);
	if (!decoKit) {
		return false;
	}
	const ItemType &itemType = Item::items[itemId];
	std::string description = fmt::format("You bought this item in the Store.\nUnwrap it in your own house to create a <{}>.", itemType.name);
	decoKit->setAttribute(ItemAttribute_t::DESCRIPTION, description);
	decoKit->setCustomAttribute("unWrapId", static_cast<int64_t>(itemId));

	const auto &inboxItem = player->getInventoryItem(CONST_SLOT_STORE_INBOX);
	if (!inboxItem) {
		return false;
	}

	const auto &inboxContainer = inboxItem->getContainer();
	if (!inboxContainer) {
		return false;
	}

	if (game_.internalAddItem(inboxContainer, decoKit) != RETURNVALUE_NOERROR) {
		inboxContainer->internalAddThing(decoKit);
	}

	return true;
}
