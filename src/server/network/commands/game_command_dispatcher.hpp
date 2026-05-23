#pragma once

#include "server/network/network_command_port.hpp"

class GameCommandDispatcher final : public INetworkCommandPort {
public:
	explicit GameCommandDispatcher(uint32_t playerId) :
		m_playerId(playerId) { }

	void dispatchMove(MoveCommand cmd) override;
	void dispatchAttack(AttackCommand cmd) override;
	void dispatchFollow(FollowCommand cmd) override;

private:
	uint32_t m_playerId;
};
