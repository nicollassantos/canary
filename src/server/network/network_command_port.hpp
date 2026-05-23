#pragma once

#include <cstdint>
#include <memory>
#include <vector>

enum Direction : uint8_t;

// ---------------------------------------------------------------------------
// Command value objects — pure data, no logic
// ---------------------------------------------------------------------------

struct NetworkCommand {
	virtual ~NetworkCommand() = default;
};

struct MoveCommand final : NetworkCommand {
	std::vector<Direction> path;
};

struct AttackCommand final : NetworkCommand {
	uint32_t creatureId = 0;
};

struct FollowCommand final : NetworkCommand {
	uint32_t creatureId = 0;
};

struct UseItemCommand final : NetworkCommand {
	uint16_t itemId = 0;
	uint8_t stackPos = 0;
	uint8_t index = 0;
};

struct SayCommand final : NetworkCommand {
	uint8_t talkType = 0;
	std::string text;
	uint16_t channelId = 0;
};

// ---------------------------------------------------------------------------
// Driving port — inbound network commands enter the domain through here
// ---------------------------------------------------------------------------

class INetworkCommandPort {
public:
	virtual ~INetworkCommandPort() = default;
	virtual void dispatchMove(MoveCommand cmd) = 0;
	virtual void dispatchAttack(AttackCommand cmd) = 0;
	virtual void dispatchFollow(FollowCommand cmd) = 0;
};
