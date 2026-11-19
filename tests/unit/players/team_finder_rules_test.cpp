/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/grouping/team_finder_rules.hpp"

namespace {
	constexpr uint32_t kLeader = 10;
	constexpr uint32_t kPlayer = 20;
	constexpr uint32_t kStranger = 30;
	constexpr auto kRequested = static_cast<uint8_t>(team_finder_rules::MemberStatus::Requested);
	constexpr auto kInvited = static_cast<uint8_t>(team_finder_rules::MemberStatus::Invited);
	constexpr auto kAccepted = static_cast<uint8_t>(team_finder_rules::MemberStatus::Accepted);
}

// The join request is kept in the leader's listing; the leader is told only when it is new.
TEST(TeamFinderRulesTest, AJoinRequestIsKeptOnceAndTheLeaderCannotJoinTheirOwnListing) {
	std::map<uint32_t, uint8_t> members;
	EXPECT_TRUE(team_finder_rules::requestToJoin(members, kLeader, kPlayer));
	EXPECT_FALSE(team_finder_rules::requestToJoin(members, kLeader, kPlayer)); // repeated packet: no new message
	EXPECT_FALSE(team_finder_rules::requestToJoin(members, kLeader, kLeader));
	ASSERT_EQ(1u, members.size());
	EXPECT_EQ(kRequested, members.at(kPlayer));
}

TEST(TeamFinderRulesTest, CancellingRemovesOnlyThePlayersOwnRequest) {
	std::map<uint32_t, uint8_t> members { { kPlayer, kRequested }, { kStranger, kAccepted } };
	EXPECT_TRUE(team_finder_rules::cancelRequest(members, kPlayer));
	EXPECT_FALSE(team_finder_rules::cancelRequest(members, kPlayer));
	EXPECT_EQ(1u, members.count(kStranger));
}

// A guid that is not in the listing is neither changed nor told anything; an unchanged answer is not repeated.
TEST(TeamFinderRulesTest, TheLeaderAnswersOnlyPlayersOfTheListingAndOnlyOnChange) {
	std::map<uint32_t, uint8_t> members { { kPlayer, kRequested } };
	EXPECT_FALSE(team_finder_rules::setMemberStatus(members, kStranger, kInvited));
	EXPECT_EQ(0u, members.count(kStranger));
	EXPECT_TRUE(team_finder_rules::setMemberStatus(members, kPlayer, kAccepted));
	EXPECT_FALSE(team_finder_rules::setMemberStatus(members, kPlayer, kAccepted));
	EXPECT_EQ(kAccepted, members.at(kPlayer));
}
