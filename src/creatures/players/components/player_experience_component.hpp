/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <memory>

class Player;
class Creature;
enum skills_t : int8_t;

class PlayerExperienceComponent {
public:
	PlayerExperienceComponent() = delete;
	explicit PlayerExperienceComponent(Player &player) :
		m_player(player) { }

	uint16_t getBaseSkill(uint8_t skill) const;
	double_t getSkillPercent(skills_t skill) const;
	void addSkillAdvance(skills_t skill, uint64_t count);
	void addManaSpent(uint64_t amount);
	void addExperience(const std::shared_ptr<Creature> &target, uint64_t exp, bool sendText = false);
	void removeExperience(uint64_t exp, bool sendText = false);
	void gainExperience(uint64_t gainExp, const std::shared_ptr<Creature> &target);
	uint16_t getSkillLevel(skills_t skill) const;

private:
	Player &m_player;
};
