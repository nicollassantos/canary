/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2024 OpenTibiaBR
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#define private public
#include "creatures/players/grouping/party.hpp"
#undef private

#include "creatures/players/player.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"

class PartyTest : public ::testing::Test {
protected:
	static void SetUpTestSuite() {
		InMemoryLogger::install(injector_);
		DI::setTestContainer(&injector_);
	}

	static void TearDownTestSuite() {
		if (DI::getTestContainer() == &injector_) {
			DI::setTestContainer(nullptr);
		}
	}

private:
	inline static di::extension::injector<> injector_ {};
};

TEST_F(PartyTest, GetPlayersAndDisbandHandleNullEntries) {
	auto party = std::make_shared<Party>();
	auto leader = std::make_shared<Player>();
	auto member = std::make_shared<Player>();
	auto invitee = std::make_shared<Player>();

	party->m_leader = leader;
	leader->setParty(party);
	member->setParty(party);

	party->memberList.push_back(nullptr);
	party->memberList.push_back(member);
	party->inviteList.push_back(nullptr);
	party->inviteList.push_back(invitee);

	const auto players = party->getPlayers();
	ASSERT_EQ(players.size(), 2U);
	EXPECT_EQ(players[0], member);
	EXPECT_EQ(players[1], leader);

	EXPECT_NO_THROW(party->disband());
	EXPECT_EQ(party->getLeader(), nullptr);
	EXPECT_EQ(party->getMemberCount(), 0U);
	EXPECT_EQ(party->getInvitationCount(), 0U);
	EXPECT_EQ(leader->getParty(), nullptr);
	EXPECT_EQ(member->getParty(), nullptr);
}

// The invited character has to be on the leader's screen; a creature id from anywhere could only be forged.
TEST_F(PartyTest, AnInviteReachesOnlyACharacterOnTheLeadersScreen) {
	const Position leader { 1000, 1000, 7 };
	EXPECT_TRUE(Party::isOnInviterScreen(leader, { 1005, 1003, 7 }));
	EXPECT_TRUE(Party::isOnInviterScreen(leader, { 1009, 1007, 7 })); // the edge of the view
	EXPECT_FALSE(Party::isOnInviterScreen(leader, { 1010, 1000, 7 }));
	EXPECT_FALSE(Party::isOnInviterScreen(leader, { 1000, 1008, 7 }));
	EXPECT_FALSE(Party::isOnInviterScreen(leader, { 1500, 1500, 7 })); // another town
	EXPECT_FALSE(Party::isOnInviterScreen(leader, { 1000, 1000, 8 })); // underground, from the surface
}
