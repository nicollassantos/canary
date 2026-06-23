/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/game.hpp"
#include "game/game_definitions.hpp"

// --- calculateHighscorePages ---

// entriesPerPage == 0 → 0 (guard)
TEST(HighscoreServiceStaticTest, CalculateHighscorePages_ZeroEntriesPerPage_ReturnsZero) {
	EXPECT_EQ(0, Game::calculateHighscorePages(100, 0));
}

// less than one page → 1
TEST(HighscoreServiceStaticTest, CalculateHighscorePages_FewerThanOnePage_ReturnsOne) {
	EXPECT_EQ(1, Game::calculateHighscorePages(5, 10));
}

// exactly one page (10/10)
TEST(HighscoreServiceStaticTest, CalculateHighscorePages_ExactOnePage_ReturnsOne) {
	EXPECT_EQ(1, Game::calculateHighscorePages(10, 10));
}

// --- getSkillNameById ---

// EXPERIENCE (0) → "experience"
TEST(HighscoreServiceStaticTest, GetSkillNameById_Experience_ReturnsExperience) {
	uint8_t skill = static_cast<uint8_t>(HighscoreCategories_t::EXPERIENCE);
	EXPECT_EQ("experience", Game::getSkillNameById(skill));
}

// FIST_FIGHTING (1) → "skill_fist"
TEST(HighscoreServiceStaticTest, GetSkillNameById_FistFighting_ReturnsSkillFist) {
	uint8_t skill = static_cast<uint8_t>(HighscoreCategories_t::FIST_FIGHTING);
	EXPECT_EQ("skill_fist", Game::getSkillNameById(skill));
}

// MAGIC_LEVEL (8) → "maglevel"
TEST(HighscoreServiceStaticTest, GetSkillNameById_MagicLevel_ReturnsMaglevel) {
	uint8_t skill = static_cast<uint8_t>(HighscoreCategories_t::MAGIC_LEVEL);
	EXPECT_EQ("maglevel", Game::getSkillNameById(skill));
}

// BOSS_POINTS (14) → "boss_points"
TEST(HighscoreServiceStaticTest, GetSkillNameById_BossPoints_ReturnsBossPoints) {
	uint8_t skill = static_cast<uint8_t>(HighscoreCategories_t::BOSS_POINTS);
	EXPECT_EQ("boss_points", Game::getSkillNameById(skill));
}

// Unknown category (e.g., 200) → defaults to EXPERIENCE, resets skill to 0
TEST(HighscoreServiceStaticTest, GetSkillNameById_UnknownCategory_DefaultsToExperience) {
	uint8_t skill = 200;
	const std::string name = Game::getSkillNameById(skill);
	EXPECT_EQ("experience", name);
	EXPECT_EQ(static_cast<uint8_t>(HighscoreCategories_t::EXPERIENCE), skill);
}

