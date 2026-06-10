/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "utils/vectorsort.hpp"

using IntVectorSort = stdext::vector_sort<int>;

// --- empty / size ---

TEST(VectorSortTest, Empty_ReturnsTrue_WhenDefault) {
	IntVectorSort vs;
	EXPECT_TRUE(vs.empty());
	EXPECT_EQ(0u, vs.size());
}

TEST(VectorSortTest, PushBack_IncreasesSizeAndIsNotEmpty) {
	IntVectorSort vs;
	vs.push_back(3);
	EXPECT_FALSE(vs.empty());
	EXPECT_EQ(1u, vs.size());
}

// --- contains ---

TEST(VectorSortTest, Contains_ReturnsFalse_WhenEmpty) {
	IntVectorSort vs;
	EXPECT_FALSE(vs.contains(5));
}

TEST(VectorSortTest, Contains_ReturnsTrue_WhenValuePresent) {
	IntVectorSort vs;
	vs.push_back(10);
	EXPECT_TRUE(vs.contains(10));
}

TEST(VectorSortTest, Contains_ReturnsFalse_WhenValueAbsent) {
	IntVectorSort vs;
	vs.push_back(10);
	EXPECT_FALSE(vs.contains(99));
}

// --- maintains sorted order ---

TEST(VectorSortTest, PushBack_MaintainsSortedOrder) {
	IntVectorSort vs;
	vs.push_back(5);
	vs.push_back(1);
	vs.push_back(3);
	vs.push_back(2);

	EXPECT_EQ(1, vs[0]);
	EXPECT_EQ(2, vs[1]);
	EXPECT_EQ(3, vs[2]);
	EXPECT_EQ(5, vs[3]);
}

// --- erase by value ---

TEST(VectorSortTest, Erase_ReturnsFalse_WhenValueAbsent) {
	IntVectorSort vs;
	vs.push_back(1);
	EXPECT_FALSE(vs.erase(99));
}

TEST(VectorSortTest, Erase_ReturnsTrue_WhenValuePresent) {
	IntVectorSort vs;
	vs.push_back(1);
	EXPECT_TRUE(vs.erase(1));
	EXPECT_TRUE(vs.empty());
}

TEST(VectorSortTest, Erase_RemovesOnlyMatchingValue) {
	IntVectorSort vs;
	vs.push_back(1);
	vs.push_back(2);
	vs.push_back(3);
	vs.erase(2);
	EXPECT_FALSE(vs.contains(2));
	EXPECT_TRUE(vs.contains(1));
	EXPECT_TRUE(vs.contains(3));
}

// --- clear ---

TEST(VectorSortTest, Clear_MakesEmpty) {
	IntVectorSort vs;
	vs.push_back(1);
	vs.push_back(2);
	vs.clear();
	EXPECT_TRUE(vs.empty());
	EXPECT_EQ(0u, vs.size());
}

// --- erase_if ---

TEST(VectorSortTest, EraseIf_RemovesMatchingElements) {
	IntVectorSort vs;
	vs.push_back(1);
	vs.push_back(2);
	vs.push_back(3);
	vs.push_back(4);
	const bool removed = vs.erase_if([](int v) { return v % 2 == 0; });
	EXPECT_TRUE(removed);
	EXPECT_EQ(2u, vs.size());
	EXPECT_FALSE(vs.contains(2));
	EXPECT_FALSE(vs.contains(4));
}
