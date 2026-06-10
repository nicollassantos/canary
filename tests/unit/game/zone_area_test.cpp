/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "game/zones/zone.hpp"

// --- Area::contains ---

TEST(AreaTest, Contains_ReturnsFalse_WhenPositionOutsideBounds) {
	Area area(Position(10, 10, 7), Position(20, 20, 7));
	EXPECT_FALSE(area.contains(Position(5, 15, 7)));
}

TEST(AreaTest, Contains_ReturnsTrue_WhenPositionInsideBounds) {
	Area area(Position(10, 10, 7), Position(20, 20, 7));
	EXPECT_TRUE(area.contains(Position(15, 15, 7)));
}

TEST(AreaTest, Contains_ReturnsTrue_AtCorner) {
	Area area(Position(10, 10, 7), Position(20, 20, 7));
	EXPECT_TRUE(area.contains(Position(10, 10, 7)));
	EXPECT_TRUE(area.contains(Position(20, 20, 7)));
}

TEST(AreaTest, Contains_ReturnsFalse_WhenWrongFloor) {
	Area area(Position(10, 10, 7), Position(20, 20, 7));
	EXPECT_FALSE(area.contains(Position(15, 15, 6)));
}

TEST(AreaTest, Contains_ReturnsTrue_WhenMultiFloor) {
	Area area(Position(10, 10, 5), Position(20, 20, 8));
	EXPECT_TRUE(area.contains(Position(15, 15, 6)));
	EXPECT_TRUE(area.contains(Position(15, 15, 8)));
}

// --- Area::intersects (static) ---

TEST(AreaTest, Intersects_ReturnsFalse_WhenAreasDisjoint) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(15, 15, 7), Position(25, 25, 7));
	EXPECT_FALSE(Area::intersects(a, b));
}

TEST(AreaTest, Intersects_ReturnsTrue_WhenAreasOverlap) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(5, 5, 7), Position(15, 15, 7));
	EXPECT_TRUE(Area::intersects(a, b));
}

TEST(AreaTest, Intersects_ReturnsTrue_WhenAreasShareEdge) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(10, 0, 7), Position(20, 10, 7));
	EXPECT_TRUE(Area::intersects(a, b));
}

TEST(AreaTest, Intersects_ReturnsFalse_WhenDifferentFloors) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(5, 5, 6), Position(15, 15, 6));
	EXPECT_FALSE(Area::intersects(a, b));
}

// --- Area::intersects (instance) ---

TEST(AreaTest, Intersects_Instance_ReturnsTrue_WhenOverlapping) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(5, 5, 7), Position(15, 15, 7));
	EXPECT_TRUE(a.intersects(b));
}

TEST(AreaTest, Intersects_Instance_IsSymmetric) {
	Area a(Position(0, 0, 7), Position(10, 10, 7));
	Area b(Position(5, 5, 7), Position(15, 15, 7));
	EXPECT_EQ(a.intersects(b), b.intersects(a));
}

// --- Area PositionIterator ---

TEST(AreaTest, PositionIterator_CountsCorrectNumberOfPositions) {
	// 3x3 area on single floor = 9 positions
	Area area(Position(0, 0, 7), Position(2, 2, 7));
	int count = 0;
	for (const auto &pos : area) {
		(void)pos;
		++count;
	}
	EXPECT_EQ(9, count);
}

TEST(AreaTest, PositionIterator_IncludesFromPosition) {
	Area area(Position(10, 10, 7), Position(12, 12, 7));
	bool foundFrom = false;
	for (const auto &pos : area) {
		if (pos == Position(10, 10, 7)) {
			foundFrom = true;
		}
	}
	EXPECT_TRUE(foundFrom);
}
