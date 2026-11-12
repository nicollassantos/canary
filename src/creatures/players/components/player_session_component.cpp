#include "creatures/players/components/player_session_component.hpp"

#include "creatures/players/player.hpp"
#include "server/network/protocol/protocolgame.hpp"
#include "creatures/combat/condition.hpp"
#include "config/configmanager.hpp"
#include "game/game.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "lua/creature/creatureevent.hpp"
#include "config/config_port.hpp"
#include "creatures/players/components/world_registry.hpp"

void PlayerSessionComponent::addInFightTicks(bool pzlock) {
	m_player.wheel().checkAbilities();

	if (m_player.hasFlag(PlayerFlags_t::NotGainInFight)) {
		return;
	}

	if (pzlock) {
		m_player.pzLockOnlyUntil = 0;
		m_player.pzLocked = true;
		m_player.sendIcons();
	}

	m_player.updateImbuementTrackerStats();

	m_player.safeCall([&player = m_player] {
		player.addCondition(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, IConfigManager::getInstance().getNumber(PZ_LOCKED)));
	});
}

void PlayerSessionComponent::removeList() {
	g_worldRegistry().removePlayer(m_player.getPlayer());

	for (const auto &[key, player] : g_worldRegistry().getPlayers()) {
		player->vip().notifyStatusChange(m_player.getPlayer(), VipStatus_t::Offline);
	}
}

void PlayerSessionComponent::addList() {
	for (const auto &[key, player] : g_worldRegistry().getPlayers()) {
		player->vip().notifyStatusChange(m_player.getPlayer(), m_player.vip().getStatus());
	}

	g_worldRegistry().addPlayer(m_player.getPlayer());
}

void PlayerSessionComponent::removePlayer(bool displayEffect, bool forced) {
	m_player.setAttackedCreature(nullptr);
	g_creatureEvents().playerLogout(m_player.getPlayer());
	if (m_player.client) {
		m_player.client->logout(displayEffect, forced);
	} else {
		g_worldRegistry().removeCreature(m_player.getPlayer());
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

void PlayerSessionComponent::addPzLockTicks() {
	if (m_player.hasFlag(PlayerFlags_t::NotGainInFight)) {
		return;
	}

	const auto duration = static_cast<uint32_t>(IConfigManager::getInstance().getNumber(PZ_LOCKED));
	const auto expiresAt = OTSYS_TIME() + duration;
	if (m_player.pzLockOnlyUntil >= expiresAt) {
		return;
	}

	m_player.pzLockOnlyUntil = expiresAt;
	m_player.pzLocked = true;
	m_player.sendIcons();
	if (m_player.pzLockEventId != 0) {
		g_dispatcher().stopEvent(m_player.pzLockEventId);
	}

	const auto &task = m_player.createPlayerTask(
		duration,
		[playerId = m_player.getID()] {
			const auto &player = g_worldRegistry().getPlayerByID(playerId);
			if (!player) {
				return;
			}

			player->pzLockEventId = 0;
			if (player->hasCondition(CONDITION_INFIGHT) || player->pzLockOnlyUntil > OTSYS_TIME()) {
				return;
			}

			player->pzLockOnlyUntil = 0;
			if (player->pzLocked) {
				player->pzLocked = false;
				player->sendIcons();
			}
			if (player->getSkull() != SKULL_RED && player->getSkull() != SKULL_BLACK) {
				player->setSkull(SKULL_NONE);
			}
		},
		__FUNCTION__
	);
	m_player.pzLockEventId = g_dispatcher().scheduleEvent(task);
}
