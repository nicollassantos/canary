#include "server/network/commands/game_command_dispatcher.hpp"

#include "game/game.hpp"

void GameCommandDispatcher::dispatchMove(MoveCommand cmd) {
	g_game().playerAutoWalk(m_playerId, cmd.path);
}

void GameCommandDispatcher::dispatchAttack(AttackCommand cmd) {
	g_game().playerSetAttackedCreature(m_playerId, cmd.creatureId);
}

void GameCommandDispatcher::dispatchFollow(FollowCommand cmd) {
	g_game().playerFollowCreature(m_playerId, cmd.creatureId);
}
