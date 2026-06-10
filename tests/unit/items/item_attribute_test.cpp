/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "items/functions/item/attribute.hpp"

class ItemAttributeTest : public ::testing::Test {
protected:
	ItemAttribute attr;
};

// --- hasAttribute ---

TEST_F(ItemAttributeTest, HasAttribute_ReturnsFalse_WhenAttributeNotSet) {
	EXPECT_FALSE(attr.hasAttribute(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, HasAttribute_ReturnsTrue_AfterSetInteger) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(42));
	EXPECT_TRUE(attr.hasAttribute(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, HasAttribute_ReturnsTrue_AfterSetString) {
	attr.setAttribute(ItemAttribute_t::DESCRIPTION, std::string("hello"));
	EXPECT_TRUE(attr.hasAttribute(ItemAttribute_t::DESCRIPTION));
}

// --- getAttributeValue ---

TEST_F(ItemAttributeTest, GetAttributeValue_ReturnsZero_WhenNotSet) {
	// Returns static int64_t emptyInt = 0 when not set
	EXPECT_EQ(0, attr.getAttributeValue(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, GetAttributeValue_ReturnsSetValue) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(55));
	EXPECT_EQ(55, attr.getAttributeValue(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, GetAttributeValue_OverwritesOnSecondSet) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(10));
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(20));
	EXPECT_EQ(20, attr.getAttributeValue(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, GetAttributeValue_IndependentAttributes) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(10));
	attr.setAttribute(ItemAttribute_t::DEFENSE, int64_t(5));
	EXPECT_EQ(10, attr.getAttributeValue(ItemAttribute_t::ATTACK));
	EXPECT_EQ(5, attr.getAttributeValue(ItemAttribute_t::DEFENSE));
}

// --- getAttributeString ---

TEST_F(ItemAttributeTest, GetAttributeString_ReturnsEmpty_WhenNotSet) {
	EXPECT_TRUE(attr.getAttributeString(ItemAttribute_t::DESCRIPTION).empty());
}

TEST_F(ItemAttributeTest, GetAttributeString_ReturnsSetValue) {
	attr.setAttribute(ItemAttribute_t::DESCRIPTION, std::string("test item"));
	EXPECT_EQ("test item", attr.getAttributeString(ItemAttribute_t::DESCRIPTION));
}

TEST_F(ItemAttributeTest, GetAttributeString_ReturnsEmpty_ForIntegerTypeKey) {
	// ATTACK is an integer attribute → getString returns empty even after integer set
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(5));
	EXPECT_TRUE(attr.getAttributeString(ItemAttribute_t::ATTACK).empty());
}

// --- removeAttribute ---

TEST_F(ItemAttributeTest, RemoveAttribute_ReturnsFalse_WhenNotPresent) {
	EXPECT_FALSE(attr.removeAttribute(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, RemoveAttribute_ReturnsTrue_WhenPresent) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(5));
	EXPECT_TRUE(attr.removeAttribute(ItemAttribute_t::ATTACK));
}

TEST_F(ItemAttributeTest, RemoveAttribute_MakesHasAttributeFalse) {
	attr.setAttribute(ItemAttribute_t::ATTACK, int64_t(5));
	attr.removeAttribute(ItemAttribute_t::ATTACK);
	EXPECT_FALSE(attr.hasAttribute(ItemAttribute_t::ATTACK));
}
