/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "creatures/creatures_definitions.hpp"
#include "creatures/players/components/creature_feedback.hpp"
#include "lib/di/container.hpp"

#include <memory>
#include <vector>

/// Records the world notifications player components send, instead of reaching Game.
class RecordingCreatureFeedback final : public CreatureFeedback {
public:
	struct SpeedChange {
		std::shared_ptr<Creature> creature;
		int32_t delta = 0;
	};

	static di::extension::injector<> &install(di::extension::injector<> &injector) {
		injector.install(di::bind<CreatureFeedback>.to<RecordingCreatureFeedback>().in(di::singleton));
		return injector;
	}

	static RecordingCreatureFeedback &installed() {
		return dynamic_cast<RecordingCreatureFeedback &>(CreatureFeedback::getInstance());
	}

	void changeSpeed(const std::shared_ptr<Creature> &creature, int32_t varSpeedDelta) override {
		speedChanges.push_back({ creature, varSpeedDelta });
	}

	void changePlayerSpeed(const std::shared_ptr<Player> &player, int32_t varSpeedDelta) override {
		playerSpeedChanges.push_back({ std::static_pointer_cast<Creature>(player), varSpeedDelta });
	}

	void addCreatureHealth(const std::shared_ptr<Creature> &) override {
		++healthUpdates;
	}

	void addPlayerMana(const std::shared_ptr<Player> &) override {
		++manaUpdates;
	}

	void internalCreatureChangeOutfit(const std::shared_ptr<Creature> &, const Outfit_t &) override {
		++outfitChanges;
	}

	void reloadCreature(const std::shared_ptr<Creature> &) override {
		++reloads;
	}

	void addMagicEffect(const Position &, uint16_t effect) override {
		effects.push_back(effect);
	}

	void sendSingleSoundEffect(const Position &, SoundEffect_t soundId, const std::shared_ptr<Creature> &) override {
		sounds.push_back(soundId);
	}

	std::vector<SpeedChange> speedChanges;
	std::vector<SpeedChange> playerSpeedChanges;
	uint32_t healthUpdates = 0;
	uint32_t manaUpdates = 0;
	uint32_t outfitChanges = 0;
	uint32_t reloads = 0;
	std::vector<uint16_t> effects;
	std::vector<SoundEffect_t> sounds;
};
