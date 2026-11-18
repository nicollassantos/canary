/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

// Player.hpp already includes the cylopedia
#include "creatures/players/player.hpp"

#include "creatures/players/player_progress_repository.hpp"
#include "enums/player_blessings.hpp"
#include "enums/player_cyclopedia.hpp"
#include "game/game.hpp"
#include "kv/kv.hpp"

PlayerCyclopedia::PlayerCyclopedia(Player &player) :
	m_player(player) { }

Summary PlayerCyclopedia::getSummary() const {
	return { getAmount(Summary_t::PREY_CARDS),
		     getAmount(Summary_t::INSTANT_REWARDS),
		     getAmount(Summary_t::HIRELINGS) };
}

void PlayerCyclopedia::loadSummaryData() const {
	const auto hirelings = g_playerProgressRepository().countHirelings(m_player.getGUID());
	const auto kvScoped = m_player.kv()->scoped("summary")->scoped(g_game().getSummaryKeyByType(static_cast<uint8_t>(Summary_t::HIRELINGS)));
	if (hirelings && !kvScoped->get("amount").has_value()) {
		kvScoped->set("amount", static_cast<int16_t>(*hirelings));
	}
}

std::string PlayerCyclopedia::describeDeath(const RecentDeathRow &death) {
	const auto &killed_by = death.killedBy;
	const auto &mostdamage_by = death.mostDamageBy;
	const auto &participants = death.participants;
	// Debug: Log the raw participants string
	g_logger().debug("Participants string: {}", participants);

	std::string cause = fmt::format("Died at level {}", death.level);
	if (!killed_by.empty()) {
		cause.append(fmt::format(" by {}", killed_by));
	}
	if (!mostdamage_by.empty()) {
		if (killed_by.empty()) {
			cause.append(fmt::format(" by {}", mostdamage_by));
		} else if (mostdamage_by != killed_by) {
			cause.append(fmt::format(" and by {}", mostdamage_by));
		} else {
			cause.append(" <b>(soloed)</b>");
		}
	}

	if (!participants.empty()) {
		std::map<std::string, std::pair<int, std::string>> participantCount;
		std::set<std::string> skipped; // Track skipped names like PHP
		std::istringstream iss(participants);
		std::string line;
		while (std::getline(iss, line)) {
			if (line.find("Name: ") == 0) {
				auto trim_cr = [](std::string &s) {
					if (!s.empty() && s.back() == '\r') {
						s.pop_back();
					}
				};
				std::string name = line.substr(6);
				trim_cr(name);
				const auto pos = iss.tellg();
				std::string typeLine;
				if (std::getline(iss, typeLine) && typeLine.find("Type: ") == 0) {
					std::string type = typeLine.substr(6);
					trim_cr(type);
					// Skip only if not already skipped (mimic PHP logic)
					if ((name == killed_by || name == mostdamage_by) && !skipped.contains(name)) {
						skipped.insert(name);
						continue;
					}
					participantCount[name].first++; // Increment count
					participantCount[name].second = type; // Store type
				} else {
					g_logger().warn("Malformed participant entry for name '{}': expected 'Type: ' line", name);
					if (pos != std::istringstream::pos_type(-1)) {
						iss.seekg(pos);
					}
				}
			}
		}

		std::vector<std::string> players;
		std::vector<std::string> monsters;
		for (const auto &[name, data] : participantCount) {
			int count = data.first;
			std::string type = data.second;
			std::string entry = count > 1 ? fmt::format("{}x {}", count, name) : name;
			if (type == "player") {
				players.push_back(entry);
			} else if (type == "monster") {
				monsters.push_back(entry);
			} else {
				g_logger().warn("Unrecognized participant type '{}' for name '{}'", type, name);
			}
		}

		if (!players.empty() || !monsters.empty()) {
			cause.append(", ");
			std::vector<std::string> allParticipants;
			allParticipants.insert(allParticipants.end(), players.begin(), players.end());
			allParticipants.insert(allParticipants.end(), monsters.begin(), monsters.end());
			cause.append(fmt::format("{}", fmt::join(allParticipants, ", ")));
		}
		// Debug: Log the final cause string
		g_logger().debug("Cause string: {}", cause);
	}
	return cause;
}

uint8_t PlayerCyclopedia::pvpKillStatus(const PvpKillRow &kill, const std::string &killerName) {
	if (killerName == kill.killedBy) {
		return kill.unjustified ? CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_UNJUSTIFIED : CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_JUSTIFIED;
	}
	if (killerName == kill.mostDamageBy && kill.mostDamageUnjustified) {
		return CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_UNJUSTIFIED;
	}
	return CYCLOPEDIA_CHARACTERINFO_RECENTKILLSTATUS_JUSTIFIED;
}

void PlayerCyclopedia::loadDeathHistory(uint16_t page, uint16_t entriesPerPage) const {
	Benchmark bm_check;
	const uint32_t offset = static_cast<uint32_t>(page - 1) * entriesPerPage;
	const uint32_t playerID = m_player.getID();
	g_playerProgressRepository().loadRecentDeaths(m_player.getGUID(), offset, entriesPerPage, [playerID, page, entriesPerPage](std::optional<RecentDeathsPage> deaths) {
		const auto &player = g_game().getPlayerByID(playerID);
		if (!player) {
			return;
		}

		player->resetAsyncOngoingTask(PlayerAsyncTask_RecentDeaths);
		if (!deaths) {
			player->sendCyclopediaCharacterRecentDeaths(0, 0, {});
			return;
		}

		const auto pages = (deaths->totalEntries + entriesPerPage - 1) / entriesPerPage;
		std::vector<RecentDeathEntry> entries;
		entries.reserve(deaths->rows.size());
		for (const auto &death : deaths->rows) {
			entries.emplace_back(describeDeath(death), death.time);
		}
		player->sendCyclopediaCharacterRecentDeaths(page, static_cast<uint16_t>(pages), entries);
	});
	m_player.addAsyncOngoingTask(PlayerAsyncTask_RecentDeaths);

	g_logger().debug("Loading death history from the player {} took {} milliseconds.", m_player.getName(), bm_check.duration());
}

void PlayerCyclopedia::loadRecentKills(uint16_t page, uint16_t entriesPerPage) const {
	Benchmark bm_check;
	const uint32_t offset = static_cast<uint32_t>(page - 1) * entriesPerPage;
	const uint32_t playerID = m_player.getID();
	g_playerProgressRepository().loadRecentPvpKills(m_player.getName(), offset, entriesPerPage, [playerID, page, entriesPerPage](std::optional<PvpKillsPage> kills) {
		const auto &player = g_game().getPlayerByID(playerID);
		if (!player) {
			return;
		}

		player->resetAsyncOngoingTask(PlayerAsyncTask_RecentPvPKills);
		if (!kills) {
			player->sendCyclopediaCharacterRecentPvPKills(0, 0, {});
			return;
		}

		const auto pages = (kills->totalEntries + entriesPerPage - 1) / entriesPerPage;
		std::vector<RecentPvPKillEntry> entries;
		entries.reserve(kills->rows.size());
		for (const auto &kill : kills->rows) {
			entries.emplace_back(fmt::format("Killed {}.", kill.victim), kill.time, pvpKillStatus(kill, player->getName()));
		}
		player->sendCyclopediaCharacterRecentPvPKills(page, static_cast<uint16_t>(pages), entries);
	});
	m_player.addAsyncOngoingTask(PlayerAsyncTask_RecentPvPKills);

	g_logger().debug("Loading recent kills from the player {} took {} milliseconds.", m_player.getName(), bm_check.duration());
}

void PlayerCyclopedia::updateStoreSummary(uint8_t type, uint16_t amount, const std::string &id) const {
	switch (type) {
		case Summary_t::HOUSE_ITEMS:
		case Summary_t::BLESSINGS:
			insertValue(type, amount, id);
			break;
		case Summary_t::ALL_BLESSINGS:
			for (auto blessIt : magic_enum::enum_values<Blessings>()) {
				insertValue(static_cast<uint8_t>(Summary_t::BLESSINGS), amount, fmt::format("{}", blessIt));
			}
			break;
		default:
			updateAmount(type, amount);
			break;
	}
}

uint16_t PlayerCyclopedia::getAmount(uint8_t type) const {
	const auto kvScope = m_player.kv()->scoped("summary")->scoped(g_game().getSummaryKeyByType(type))->get("amount");
	return static_cast<uint16_t>(kvScope ? kvScope->getNumber() : 0);
}

void PlayerCyclopedia::updateAmount(uint8_t type, uint16_t amount) const {
	const auto oldAmount = getAmount(type);
	m_player.kv()->scoped("summary")->scoped(g_game().getSummaryKeyByType(type))->set("amount", oldAmount + amount);
}

std::map<uint16_t, uint16_t> PlayerCyclopedia::getResult(uint8_t type) const {
	const auto kvScope = m_player.kv()->scoped("summary")->scoped(g_game().getSummaryKeyByType(type));
	std::map<uint16_t, uint16_t> result; // ID, amount
	for (const auto &scope : kvScope->keys()) {
		const size_t &pos = scope.find('.');
		if (pos == std::string::npos) {
			g_logger().error("[{}] Invalid key format: {}", __FUNCTION__, scope);
			continue;
		}
		std::string id = scope.substr(0, pos);
		const auto amount = kvScope->scoped(id)->get("amount");
		result.emplace(std::stoll(id), static_cast<uint16_t>(amount ? amount->getNumber() : 0));
	}
	return result;
}

void PlayerCyclopedia::insertValue(uint8_t type, uint16_t amount, const std::string &id) const {
	auto result = getResult(type);
	const auto it = result.find(std::stoll(id));
	auto oldAmount = (it != result.end() ? it->second : 0);
	auto newAmount = oldAmount + amount;
	m_player.kv()->scoped("summary")->scoped(g_game().getSummaryKeyByType(type))->scoped(id)->set("amount", newAmount);
	g_logger().debug("[{}] type: {}, id: {}, old amount: {}, added amount: {}, new amount: {}", __FUNCTION__, type, id, oldAmount, amount, newAmount);
}
