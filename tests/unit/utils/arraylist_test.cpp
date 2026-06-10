/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/bags/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "utils/arraylist.hpp"

using IntArrayList = stdext::arraylist<int>;

// --- empty / size ---

TEST(ArrayListTest, Empty_ReturnsTrue_WhenDefault) {
	IntArrayList al;
	EXPECT_TRUE(al.empty());
	EXPECT_EQ(0u, al.size());
}

TEST(ArrayListTest, PushBack_IncreasesSizeAndIsNotEmpty) {
	IntArrayList al;
	al.push_back(1);
	EXPECT_FALSE(al.empty());
	EXPECT_EQ(1u, al.size());
}

// --- push_back and push_front ordering ---

TEST(ArrayListTest, PushFront_PrependsBefore_PushBack) {
	IntArrayList al;
	al.push_back(2);
	al.push_back(3);
	al.push_front(1);
	// After merge: [1, 2, 3]
	EXPECT_EQ(1, al[0]);
	EXPECT_EQ(2, al[1]);
	EXPECT_EQ(3, al[2]);
}

TEST(ArrayListTest, MultiplePushFronts_ReverseOrder) {
	IntArrayList al;
	al.push_front(3);
	al.push_front(2);
	al.push_front(1);
	// push_front reverses: [1, 2, 3]
	EXPECT_EQ(1, al[0]);
	EXPECT_EQ(2, al[1]);
	EXPECT_EQ(3, al[2]);
}

// --- contains ---

TEST(ArrayListTest, Contains_ReturnsFalse_WhenEmpty) {
	IntArrayList al;
	EXPECT_FALSE(al.contains(5));
}

TEST(ArrayListTest, Contains_ReturnsTrue_WhenValuePresent) {
	IntArrayList al;
	al.push_back(10);
	EXPECT_TRUE(al.contains(10));
}

TEST(ArrayListTest, Contains_ReturnsTrue_ForPushFrontValue) {
	IntArrayList al;
	al.push_back(5);
	al.push_front(3);
	EXPECT_TRUE(al.contains(3));
}

// --- erase by value ---

TEST(ArrayListTest, Erase_ReturnsFalse_WhenValueAbsent) {
	IntArrayList al;
	al.push_back(1);
	EXPECT_FALSE(al.erase(99));
}

TEST(ArrayListTest, Erase_ReturnsTrue_WhenValuePresent) {
	IntArrayList al;
	al.push_back(1);
	EXPECT_TRUE(al.erase(1));
	EXPECT_TRUE(al.empty());
}

// --- clear ---

TEST(ArrayListTest, Clear_MakesEmpty) {
	IntArrayList al;
	al.push_back(1);
	al.push_front(2);
	al.clear();
	EXPECT_TRUE(al.empty());
	EXPECT_EQ(0u, al.size());
}

// --- erase_if ---

TEST(ArrayListTest, EraseIf_RemovesMatchingElements) {
	IntArrayList al;
	al.push_back(1);
	al.push_back(2);
	al.push_back(3);
	al.push_back(4);
	const bool removed = al.erase_if([](int v) { return v > 2; });
	EXPECT_TRUE(removed);
	EXPECT_EQ(2u, al.size());
}
