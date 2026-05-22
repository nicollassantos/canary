#include "game/sound/sound_service.hpp"
#include "creatures/creature.hpp"
#include "creatures/creatures_definitions.hpp"
#include "creatures/players/player.hpp"
#include "map/spectators.hpp"

void SoundService::sendSingleSoundEffect(const Position &pos, SoundEffect_t soundId, const std::shared_ptr<Creature> &actor) {
	if (soundId == SoundEffect_t::SILENCE) {
		return;
	}

	using enum SourceEffect_t;
	for (const auto &spectator : Spectators().find<Player>(pos)) {
		SourceEffect_t source = CREATURES;
		if (!actor || actor->getNpc()) {
			source = GLOBAL;
		} else if (actor == spectator) {
			source = OWN;
		} else if (actor->getPlayer()) {
			source = OTHERS;
		}

		spectator->getPlayer()->sendSingleSoundEffect(pos, soundId, source);
	}
}

void SoundService::sendDoubleSoundEffect(const Position &pos, SoundEffect_t mainSoundEffect, SoundEffect_t secondarySoundEffect, const std::shared_ptr<Creature> &actor) {
	if (secondarySoundEffect == SoundEffect_t::SILENCE) {
		sendSingleSoundEffect(pos, mainSoundEffect, actor);
		return;
	}

	using enum SourceEffect_t;
	for (const auto &spectator : Spectators().find<Player>(pos)) {
		SourceEffect_t source = CREATURES;
		if (!actor || actor->getNpc()) {
			source = GLOBAL;
		} else if (actor == spectator) {
			source = OWN;
		} else if (actor->getPlayer()) {
			source = OTHERS;
		}

		spectator->getPlayer()->sendDoubleSoundEffect(pos, mainSoundEffect, source, secondarySoundEffect, source);
	}
}
