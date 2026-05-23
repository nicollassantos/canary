#include "creatures/players/components/player_session_component.hpp"

#include "creatures/players/player.hpp"
#include "creatures/combat/condition.hpp"
#include "config/configmanager.hpp"
#include "game/game.hpp"
#include "lua/creature/creatureevent.hpp"

void PlayerSessionComponent::addInFightTicks(bool pzlock) {
	m_player.wheel().checkAbilities();

	if (m_player.hasFlag(PlayerFlags_t::NotGainInFight)) {
		return;
	}

	if (pzlock) {
		m_player.pzLocked = true;
		m_player.sendIcons();
	}

	m_player.updateImbuementTrackerStats();

	m_player.safeCall([&player = m_player] {
		player.addCondition(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_configManager().getNumber(PZ_LOCKED)));
	});
}

void PlayerSessionComponent::removeList() {
	g_game().removePlayer(m_player.getPlayer());

	for (const auto &[key, player] : g_game().getPlayers()) {
		player->vip().notifyStatusChange(m_player.getPlayer(), VipStatus_t::Offline);
	}
}

void PlayerSessionComponent::addList() {
	for (const auto &[key, player] : g_game().getPlayers()) {
		player->vip().notifyStatusChange(m_player.getPlayer(), m_player.vip().getStatus());
	}

	g_game().addPlayer(m_player.getPlayer());
}

void PlayerSessionComponent::removePlayer(bool displayEffect, bool forced) {
	m_player.setAttackedCreature(nullptr);
	g_creatureEvents().playerLogout(m_player.getPlayer());
	if (m_player.client) {
		m_player.client->logout(displayEffect, forced);
	} else {
		g_game().removeCreature(m_player.getPlayer());
	}
}

bool PlayerSessionComponent::canLogout() {
	if (m_player.isConnecting) {
		return false;
	}

	const auto &tile = m_player.getTile();
	if (!tile) {
		return false;
	}

	if (tile->hasFlag(TILESTATE_NOLOGOUT)) {
		return false;
	}

	if (tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return true;
	}

	return !m_player.isPzLocked() && !m_player.hasCondition(CONDITION_INFIGHT);
}

void PlayerSessionComponent::setLoginProtection(int64_t time) {
	m_player.loginProtectionTime = OTSYS_TIME() + time;
}

bool PlayerSessionComponent::isLoginProtected() const {
	return m_player.loginProtectionTime > OTSYS_TIME();
}

void PlayerSessionComponent::resetLoginProtection() {
	m_player.loginProtectionTime = 0;
}

void PlayerSessionComponent::setProtection(bool status) {
	m_player.connProtected = status;
}

bool PlayerSessionComponent::isProtected() {
	return m_player.connProtected;
}
