/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#ifndef USE_PRECOMPILED_HEADERS
	#include <cstdint>
	#include <map>
#endif

/// Who may change a Team Finder listing's member list, and when the other side is told. The list is
/// TeamFinder::membersMap of the leader's listing (player guid -> status); the client reads each player's
/// status from it (ProtocolGame::sendTeamFinderList). Every guid comes from the client.
namespace team_finder_rules {
	enum class MemberStatus : uint8_t {
		Requested = 1,
		Invited = 2,
		Accepted = 3,
		Denied = 4,
	};

	/// A join request is kept in the leader's listing. True when it is new, so the leader is told once and
	/// not on every repeated packet; the leader cannot join their own listing.
	inline bool requestToJoin(std::map<uint32_t, uint8_t> &members, uint32_t leaderGuid, uint32_t playerGuid) {
		if (playerGuid == leaderGuid) {
			return false;
		}
		return members.try_emplace(playerGuid, static_cast<uint8_t>(MemberStatus::Requested)).second;
	}

	/// Withdraws the player's request; true when there was one.
	inline bool cancelRequest(std::map<uint32_t, uint8_t> &members, uint32_t playerGuid) {
		return members.erase(playerGuid) > 0;
	}

	/// The leader answers a player of the listing. True when the status changed, so the player is told once;
	/// a guid that is not in the listing changes nothing and is not told anything.
	inline bool setMemberStatus(std::map<uint32_t, uint8_t> &members, uint32_t memberGuid, uint8_t status) {
		const auto it = members.find(memberGuid);
		if (it == members.end() || it->second == status) {
			return false;
		}
		it->second = status;
		return true;
	}
}
