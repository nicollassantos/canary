/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/bags/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "items/functions/item/custom_attribute.hpp"

TEST(CustomAttributeTest, Constructor_Int64_StoresKeyAndValue) {
	CustomAttribute ca("damage", int64_t(42));
	EXPECT_EQ("damage", ca.getStringKey());
	EXPECT_EQ(42, ca.getInteger());
}

TEST(CustomAttributeTest, Constructor_String_StoresKeyAndValue) {
	CustomAttribute ca("name", std::string("sword"));
	EXPECT_EQ("name", ca.getStringKey());
	EXPECT_EQ("sword", ca.getString());
}

TEST(CustomAttributeTest, Constructor_Double_StoresKeyAndValue) {
	CustomAttribute ca("ratio", 1.5);
	EXPECT_EQ("ratio", ca.getStringKey());
	EXPECT_NEAR(1.5, ca.getDouble(), 1e-9);
}

TEST(CustomAttributeTest, Constructor_Bool_StoresKeyAndValue) {
	CustomAttribute ca("flag", true);
	EXPECT_EQ("flag", ca.getStringKey());
	EXPECT_TRUE(ca.getBool());
}

TEST(CustomAttributeTest, HasValue_ReturnsTrue_ForMatchingType) {
	CustomAttribute ca("x", int64_t(1));
	EXPECT_TRUE(ca.hasValue<int64_t>());
}

TEST(CustomAttributeTest, HasValue_ReturnsFalse_ForWrongType) {
	CustomAttribute ca("x", int64_t(1));
	EXPECT_FALSE(ca.hasValue<std::string>());
}

TEST(CustomAttributeTest, GetAttribute_ReturnsInt_WhenInt64) {
	CustomAttribute ca("val", int64_t(99));
	EXPECT_EQ(99, ca.getAttribute<int64_t>());
}

TEST(CustomAttributeTest, GetAttribute_ReturnsString_WhenString) {
	CustomAttribute ca("key", std::string("hello"));
	EXPECT_EQ("hello", ca.getAttribute<std::string>());
}

TEST(CustomAttributeTest, SetValue_UpdatesInt64) {
	CustomAttribute ca("val", int64_t(1));
	ca.setValue(int64_t(100));
	EXPECT_EQ(100, ca.getInteger());
}

TEST(CustomAttributeTest, SetValue_IgnoresWrongType) {
	// setValue(string) on an int64 attribute should be ignored
	CustomAttribute ca("val", int64_t(5));
	ca.setValue(std::string("ignored"));
	EXPECT_EQ(5, ca.getInteger());
}
