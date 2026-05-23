/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>

class Player;
class Creature;

class PlayerDeathComponent {
public:
	PlayerDeathComponent() = delete;
	explicit PlayerDeathComponent(Player &player) :
		m_player(player) { }

	void death(const std::shared_ptr<Creature> &lastHitCreature);
	void despawn();
	bool dropCorpse(const std::shared_ptr<Creature> &lastHitCreature, const std::shared_ptr<Creature> &mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified);
	void addUnjustifiedDead(const std::shared_ptr<Player> &attacked);
	double getLostPercent() const;

private:
	Player &m_player;
};
