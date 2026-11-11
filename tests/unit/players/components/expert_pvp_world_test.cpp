/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/components/pvp/expert_pvp.hpp"
#include "creatures/players/components/pvp/expert_pvp_world.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"

namespace {
	class FakeExpertPvpWorld final : public ExpertPvpWorld {
	public:
		[[nodiscard]] std::shared_ptr<Player> getPlayerByGUID(uint32_t guid) const override {
			const auto it = players.find(guid);
			return it == players.end() ? nullptr : it->second;
		}

		[[nodiscard]] WorldType_t getWorldType() const override {
			return WORLD_TYPE_PVP;
		}

		[[nodiscard]] std::vector<std::shared_ptr<Player>> getOnlinePlayers() const override {
			std::vector<std::shared_ptr<Player>> result;
			for (const auto &[_, player] : players) {
				result.emplace_back(player);
			}
			return result;
		}

		[[nodiscard]] std::string getConfiguredWorldType() const override {
			return worldType;
		}

		void add(const std::shared_ptr<Player> &player) {
			players[player->getGUID()] = player;
		}

		std::string worldType = "expert-pvp";
		std::map<uint32_t, std::shared_ptr<Player>> players;
	};
}

class ExpertPvpWorldTest : public ::testing::Test {
protected:
	void SetUp() override {
		previousContainer = DI::getTestContainer();
		injector = std::make_unique<di::extension::injector<>>();
		InMemoryLogger::install(*injector);
		injector->install(di::bind<ExpertPvpWorld>.to<FakeExpertPvpWorld>().in(di::singleton));
		DI::setTestContainer(injector.get());
		world = &dynamic_cast<FakeExpertPvpWorld &>(ExpertPvpWorld::getInstance());

		subject = makePlayer(100);
		viewer = makePlayer(200);
		opponent = makePlayer(300);
	}

	void TearDown() override {
		DI::setTestContainer(previousContainer);
		injector.reset();
	}

	static std::shared_ptr<Player> makePlayer(uint32_t guid) {
		auto player = std::make_shared<Player>();
		player->setGroup(std::make_shared<Group>());
		player->setGUID(guid);
		return player;
	}

	std::unique_ptr<di::extension::injector<>> injector;
	di::extension::injector<>* previousContainer = nullptr;
	FakeExpertPvpWorld* world = nullptr;
	std::shared_ptr<Player> subject;
	std::shared_ptr<Player> viewer;
	std::shared_ptr<Player> opponent;
};

TEST_F(ExpertPvpWorldTest, EnabledFollowsConfiguredWorldType) {
	EXPECT_TRUE(ExpertPvp::isEnabled());
	world->worldType = "open-pvp";
	EXPECT_FALSE(ExpertPvp::isEnabled());
	EXPECT_FALSE(ExpertPvp::isRetroPvpWorldType());
	world->worldType = "pvp";
	EXPECT_TRUE(ExpertPvp::isRetroPvpWorldType());
}

TEST_F(ExpertPvpWorldTest, NoMarkWhenExpertPvpDisabled) {
	world->worldType = "retro-pvp";
	subject->addAttacked(viewer);
	EXPECT_EQ(ExpertPvpSituationMark::None, ExpertPvp::getSituationMark(subject, viewer));
}

TEST_F(ExpertPvpWorldTest, YellowWhenSubjectFightsViewer) {
	subject->addAttacked(viewer);
	EXPECT_EQ(ExpertPvpSituationMark::Yellow, ExpertPvp::getSituationMark(subject, viewer));
}

TEST_F(ExpertPvpWorldTest, BrownWhenSubjectFightsOnlinePlayer) {
	world->add(opponent);
	subject->addAttacked(opponent);
	EXPECT_EQ(ExpertPvpSituationMark::Brown, ExpertPvp::getSituationMark(subject, viewer));
}

TEST_F(ExpertPvpWorldTest, IgnoresOpponentsNoLongerOnline) {
	subject->addAttacked(opponent); // opponent never registered in the world
	EXPECT_EQ(ExpertPvpSituationMark::None, ExpertPvp::getSituationMark(subject, viewer));
}

TEST_F(ExpertPvpWorldTest, NoMarkForSelf) {
	subject->addAttacked(viewer);
	EXPECT_EQ(ExpertPvpSituationMark::None, ExpertPvp::getSituationMark(subject, subject));
}
