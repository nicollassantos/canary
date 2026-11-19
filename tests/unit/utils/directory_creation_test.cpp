/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "utils/directory_creation.hpp"

#ifndef USE_PRECOMPILED_HEADERS
	#include <filesystem>
	#include <fstream>
	#include <random>
	#include <string>
#endif

namespace {
	// FS.mkdir/FS.mkdir_p (upstream PR #4102): directories are created without a shell.
	class DirectoryCreationTest : public ::testing::Test {
	protected:
		void SetUp() override {
			std::random_device device;
			root = std::filesystem::temp_directory_path() / ("canary_directory_creation_" + std::to_string(device()));
			std::filesystem::create_directories(root);
		}

		void TearDown() override {
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}

		std::filesystem::path root;
	};
}

TEST_F(DirectoryCreationTest, CreatesEveryMissingParent) {
	const auto target = root / "reports" / "bugs" / "Some_Player";

	EXPECT_FALSE(directory_creation::createDirectories(target.string()).has_value());
	EXPECT_TRUE(std::filesystem::is_directory(target));
}

TEST_F(DirectoryCreationTest, AnExistingDirectoryIsASuccess) {
	EXPECT_FALSE(directory_creation::createDirectories(root.string()).has_value());
	EXPECT_FALSE(directory_creation::createDirectories(root.string()).has_value());
}

// The old FS.mkdir ran `mkdir "<path>"` in a shell: these names broke out of the quotes.
TEST_F(DirectoryCreationTest, ShellMetacharactersAreJustPartOfTheName) {
	const auto canary = root / "injected";
	for (const std::string name : { "a\" ; mkdir \"injected", "b`touch injected`", "c$(touch injected)", "d&e|f;g", "it's (fine) 100%" }) {
		SCOPED_TRACE(name);
		const auto target = root / name;
		EXPECT_FALSE(directory_creation::createDirectories(target.string()).has_value());
		EXPECT_TRUE(std::filesystem::is_directory(target));
	}
	EXPECT_FALSE(std::filesystem::exists(canary));
}

TEST_F(DirectoryCreationTest, AFileInTheWayIsReportedAsAnError) {
	const auto file = root / "taken";
	std::ofstream(file) << "x";

	const auto error = directory_creation::createDirectories((file / "child").string());
	ASSERT_TRUE(error.has_value());
	EXPECT_FALSE(error->empty());
}

TEST_F(DirectoryCreationTest, AnEmptyPathIsAnError) {
	const auto error = directory_creation::createDirectories("");
	ASSERT_TRUE(error.has_value());
	EXPECT_FALSE(error->empty());
}
