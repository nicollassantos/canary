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
	#include <cstdint>
	#include <memory>
#endif

class Creature;
class Player;
struct Outfit_t;
struct Position;
enum SoundEffect_t : uint16_t;

/**
 * Notifications that player components send to the world when a creature's visible
 * state changes: speed, health/mana bars, outfit, effects and sounds (driven port).
 *
 * The signatures mirror Game's on purpose: call sites change only the receiver
 * (`g_game()` → `g_creatureFeedback()`), so upstream edits to those lines still merge.
 * Concrete for the same reason as ExpertPvpWorld: the default adapts Game, and
 * tests replace it with `di::bind<CreatureFeedback>.to<Fake>()`. Resolve it per call;
 * do not store it in components.
 */
class CreatureFeedback {
public:
	virtual ~CreatureFeedback() = default;

	static CreatureFeedback &getInstance();

	virtual void changeSpeed(const std::shared_ptr<Creature> &creature, int32_t varSpeedDelta);
	virtual void changePlayerSpeed(const std::shared_ptr<Player> &player, int32_t varSpeedDelta);
	virtual void addCreatureHealth(const std::shared_ptr<Creature> &target);
	virtual void addPlayerMana(const std::shared_ptr<Player> &target);
	virtual void internalCreatureChangeOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit);
	virtual void reloadCreature(const std::shared_ptr<Creature> &creature);
	virtual void addMagicEffect(const Position &pos, uint16_t effect);
	virtual void sendSingleSoundEffect(const Position &pos, SoundEffect_t soundId, const std::shared_ptr<Creature> &actor = nullptr);
};

constexpr auto g_creatureFeedback = CreatureFeedback::getInstance;
