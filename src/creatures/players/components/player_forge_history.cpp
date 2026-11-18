/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.org/
 */

#include "creatures/players/components/player_forge_history.hpp"

#include "creatures/players/player.hpp"
#include "utils/benchmark.hpp"
#include "creatures/players/player_progress_repository.hpp"
#include "utils/tools.hpp"

PlayerForgeHistory::PlayerForgeHistory(Player &player) :
	m_player(player) { }

const std::vector<ForgeHistory> &PlayerForgeHistory::get() const {
	return m_history;
}

void PlayerForgeHistory::add(const ForgeHistory &history) {
	auto adjustedHistory = history;
	while (std::ranges::any_of(m_history, [&adjustedHistory](const ForgeHistory &existing) {
		return existing.createdAt == adjustedHistory.createdAt;
	})) {
		++adjustedHistory.createdAt;
	}

	m_history.push_back(adjustedHistory);
	m_modifiedHistory.push_back(adjustedHistory);
}

void PlayerForgeHistory::remove(uint32_t id) {
	m_removedHistoryIds.push_back(id);
	std::erase_if(m_history, [id](const ForgeHistory &h) {
		return h.id == id;
	});
}

bool PlayerForgeHistory::load() {
	Benchmark benchmark;
	auto history = g_playerProgressRepository().loadForgeHistory(m_player.getGUID());
	m_history.insert(m_history.end(), std::make_move_iterator(history.begin()), std::make_move_iterator(history.end()));
	benchmark.log("PlayerForgeHistory::load completed");
	return true;
}

bool PlayerForgeHistory::save() {
	if (m_modifiedHistory.empty() && m_removedHistoryIds.empty()) {
		return true;
	}

	Benchmark benchmark;
	if (!g_playerProgressRepository().saveForgeHistory(m_player.getGUID(), m_removedHistoryIds, m_modifiedHistory)) {
		g_logger().error("Failed to save forge history entries for player with ID: {}", m_player.getGUID());
		return false;
	}

	m_removedHistoryIds.clear();
	m_modifiedHistory.clear();

	benchmark.log("PlayerForgeHistory::save completed");
	return true;
}
