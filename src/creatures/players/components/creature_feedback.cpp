/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/creature_feedback.hpp"

#include "creatures/creatures_definitions.hpp"
#include "game/game.hpp"
#include "game/movement/position.hpp"
#include "lib/di/container.hpp"

CreatureFeedback &CreatureFeedback::getInstance() {
	return DI::port<CreatureFeedback>();
}

void CreatureFeedback::changeSpeed(const std::shared_ptr<Creature> &creature, int32_t varSpeedDelta) {
	g_game().changeSpeed(creature, varSpeedDelta);
}

void CreatureFeedback::changePlayerSpeed(const std::shared_ptr<Player> &player, int32_t varSpeedDelta) {
	g_game().changePlayerSpeed(player, varSpeedDelta);
}

void CreatureFeedback::addCreatureHealth(const std::shared_ptr<Creature> &target) {
	g_game().addCreatureHealth(target);
}

void CreatureFeedback::addPlayerMana(const std::shared_ptr<Player> &target) {
	g_game().addPlayerMana(target);
}

void CreatureFeedback::internalCreatureChangeOutfit(const std::shared_ptr<Creature> &creature, const Outfit_t &outfit) {
	g_game().internalCreatureChangeOutfit(creature, outfit);
}

void CreatureFeedback::reloadCreature(const std::shared_ptr<Creature> &creature) {
	g_game().reloadCreature(creature);
}

void CreatureFeedback::addMagicEffect(const Position &pos, uint16_t effect) {
	g_game().addMagicEffect(pos, effect);
}

void CreatureFeedback::sendSingleSoundEffect(const Position &pos, SoundEffect_t soundId, const std::shared_ptr<Creature> &actor) {
	g_game().sendSingleSoundEffect(pos, soundId, actor);
}
