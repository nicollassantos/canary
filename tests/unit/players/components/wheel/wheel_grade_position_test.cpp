/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/wheel/player_wheel.hpp"
#include "creatures/players/player.hpp"
#include "enums/player_wheel.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {

	constexpr uint8_t Sorcerer = 1;
	constexpr uint8_t Knight = 4;

	uint8_t position(WheelGemBasicModifier_t modifier) {
		return static_cast<uint8_t>(modifier);
	}

	uint8_t position(WheelGemSupremeModifier_t modifier) {
		return static_cast<uint8_t>(modifier);
	}

	class WheelGradePositionTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
			logger = &dynamic_cast<InMemoryLogger &>(DI::get<Logger>());
			logger->reset();
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		bool loggedRejectedPosition() const {
			return std::ranges::any_of(logger->logs, [](const auto &entry) {
				return entry.level == "warning" && entry.message.find("invalid gem grade position") != std::string::npos;
			});
		}

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
		InMemoryLogger* logger = nullptr;
	};

} // namespace

TEST(WheelGradePosition, AcceptsListedLesserPosition) {
	EXPECT_TRUE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Lesser, position(WheelGemBasicModifier_t::General_PhysicalResistance), Knight));
}

TEST(WheelGradePosition, RejectsLesserPositionPastTheGradeTable) {
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Lesser, 49, Knight));
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Lesser, 200, Knight));
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Lesser, 255, Knight));
}

TEST(WheelGradePosition, RejectsLesserPositionThatIsNotAModifierSlot) {
	// 32 is the retired Vocation_Mana_Capacity value: inside the table but never a slot.
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Lesser, 32, Knight));
}

TEST(WheelGradePosition, AcceptsSupremePositionOfThePlayersVocation) {
	EXPECT_TRUE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Greater, position(WheelGemSupremeModifier_t::Knight_AvatarOfSteel_Cooldown), Knight));
}

TEST(WheelGradePosition, RejectsSupremePositionOfAnotherVocation) {
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Greater, position(WheelGemSupremeModifier_t::Knight_AvatarOfSteel_Cooldown), Sorcerer));
}

TEST(WheelGradePosition, RejectsSupremePositionPastTheGradeTable) {
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Greater, 200, Knight));
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Greater, 255, Sorcerer));
}

TEST(WheelGradePosition, RejectsUnknownVocationAndFragmentType) {
	EXPECT_FALSE(PlayerWheel::isGradablePosition(WheelFragmentType_t::Greater, position(WheelGemSupremeModifier_t::General_Dodge), 0));
	EXPECT_FALSE(PlayerWheel::isGradablePosition(static_cast<WheelFragmentType_t>(7), position(WheelGemBasicModifier_t::General_PhysicalResistance), Knight));
}

// Regression: the position byte from the client indexed the grade tables unchecked,
// so a crafted packet read and wrote memory past them.
TEST_F(WheelGradePositionTest, ImproveGemGradeRejectsAnOutOfRangePositionBeforeTouchingTheTables) {
	const auto player = std::make_shared<Player>();

	player->wheel().improveGemGrade(WheelFragmentType_t::Lesser, 200);

	EXPECT_TRUE(loggedRejectedPosition());
}

TEST_F(WheelGradePositionTest, ImproveGemGradeRejectsSupremePositionWithoutVocation) {
	const auto player = std::make_shared<Player>();

	player->wheel().improveGemGrade(WheelFragmentType_t::Greater, position(WheelGemSupremeModifier_t::General_Dodge));

	EXPECT_TRUE(loggedRejectedPosition());
}
