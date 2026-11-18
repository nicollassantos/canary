/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

class Player;
class KV;
struct RecentDeathRow;
struct PvpKillRow;

struct Summary {
	uint16_t m_preyWildcards = 0;
	uint16_t m_instantRewards = 0;
	uint16_t m_hirelings = 0;

	[[maybe_unused]] Summary(uint16_t mPreyWildcards, uint16_t mInstantRewards, uint16_t mHirelings) :
		m_preyWildcards(mPreyWildcards), m_instantRewards(mInstantRewards), m_hirelings(mHirelings) { }
};

class PlayerCyclopedia {
public:
	explicit PlayerCyclopedia(Player &player);

	Summary getSummary() const;

	void loadSummaryData() const;
	void loadDeathHistory(uint16_t page, uint16_t entriesPerPage) const;
	void loadRecentKills(uint16_t page, uint16_t entriesPerPage) const;

	/// The Cyclopedia line of a death: who killed, who did most damage, the other participants.
	static std::string describeDeath(const RecentDeathRow &death);
	/// Whether a kill shows as unjustified for `killerName`.
	static uint8_t pvpKillStatus(const PvpKillRow &kill, const std::string &killerName);

	void updateStoreSummary(uint8_t type, uint16_t amount = 1, const std::string &id = "") const;
	uint16_t getAmount(uint8_t type) const;
	void updateAmount(uint8_t type, uint16_t amount = 1) const;

	[[nodiscard]] std::map<uint16_t, uint16_t> getResult(uint8_t type) const;
	void insertValue(uint8_t type, uint16_t amount = 1, const std::string &id = "") const;

private:
	Player &m_player;
};
