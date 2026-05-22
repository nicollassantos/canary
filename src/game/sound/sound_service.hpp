#pragma once
#include <memory>
#include "game/movement/position.hpp"

class Creature;
enum SoundEffect_t : uint16_t;

class SoundService {
public:
	void sendSingleSoundEffect(const Position &pos, SoundEffect_t soundId, const std::shared_ptr<Creature> &actor = nullptr);
	void sendDoubleSoundEffect(const Position &pos, SoundEffect_t mainSoundEffect, SoundEffect_t secondarySoundEffect, const std::shared_ptr<Creature> &actor = nullptr);
};
