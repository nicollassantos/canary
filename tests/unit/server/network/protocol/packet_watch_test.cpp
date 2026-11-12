/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "lib/logging/in_memory_logger.hpp"
#include "server/network/protocol/packet_watch.hpp"

namespace {

	using namespace std::chrono_literals;

	constexpr uint32_t Localhost = 0x0100007F;

	class PacketWatchTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		void SetUp() override {
			logger = &dynamic_cast<InMemoryLogger &>(g_logger());
			firstLog = logger->logCount();
		}

		std::vector<std::string> warnings() const {
			std::vector<std::string> found;
			for (size_t index = firstLog; index < logger->logCount(); ++index) {
				const auto [level, message] = logger->getLogEntry(index);
				if (level == "warning" && message.find("[PacketWatch]") != std::string::npos) {
					found.push_back(message);
				}
			}
			return found;
		}

		packet_watch::SlowPacketReporter reporter;
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		InMemoryLogger* logger = nullptr;
		size_t firstLog = 0;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(PacketWatchTest, FastPacketIsNotReported) {
	EXPECT_FALSE(reporter.report(0x82, "Fast", Localhost, packet_watch::SlowThreshold - 1ms, start));
	EXPECT_TRUE(warnings().empty());
}

TEST_F(PacketWatchTest, SlowPacketLogsOpcodePlayerIpAndDuration) {
	EXPECT_TRUE(reporter.report(0x82, "Bed User", Localhost, 80ms, start));

	const auto logged = warnings();
	ASSERT_EQ(1U, logged.size());
	EXPECT_NE(std::string::npos, logged.front().find("0x82"));
	EXPECT_NE(std::string::npos, logged.front().find("'Bed User'"));
	EXPECT_NE(std::string::npos, logged.front().find("127.0.0.1"));
	EXPECT_NE(std::string::npos, logged.front().find("80 ms"));
}

TEST_F(PacketWatchTest, SlowPacketsWithinTheIntervalAreCountedNotLogged) {
	EXPECT_TRUE(reporter.report(0x82, "A", Localhost, 80ms, start));
	EXPECT_FALSE(reporter.report(0x82, "A", Localhost, 80ms, start + 1s));
	EXPECT_FALSE(reporter.report(0xF4, "B", Localhost, 90ms, start + 2s));
	EXPECT_TRUE(reporter.report(0x82, "A", Localhost, 80ms, start + packet_watch::WarningInterval + 1s));

	const auto logged = warnings();
	ASSERT_EQ(2U, logged.size());
	EXPECT_NE(std::string::npos, logged.back().find("2 more slow packets"));
}

TEST_F(PacketWatchTest, ScopeWithoutPlayerIsSafe) {
	{
		const packet_watch::Scope scope(0x1E, nullptr, Localhost);
	}
	SUCCEED();
}
