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

class Player;
enum skills_t : int8_t;

class PlayerTrainingComponent {
public:
	PlayerTrainingComponent() = delete;
	explicit PlayerTrainingComponent(Player &player) :
		m_player(player) { }

	bool addOfflineTrainingTries(skills_t skill, uint64_t tries);
	void addOfflineTrainingTime(int32_t addTime);
	void removeOfflineTrainingTime(int32_t removeTime);
	int32_t getOfflineTrainingTime() const;
	int8_t getOfflineTrainingSkill() const;
	void setOfflineTrainingSkill(int8_t skill);

private:
	Player &m_player;
};
