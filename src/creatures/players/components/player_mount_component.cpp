/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "creatures/players/components/player_mount_component.hpp"

#include "creatures/appearance/mounts/mounts.hpp"
#include "creatures/appearance/outfit/outfit.hpp"
#include "creatures/players/player.hpp"
#include "config/configmanager.hpp"
#include "game/game.hpp"
#include "kv/kv.hpp"
#include "utils/const.hpp"
#include "utils/tools.hpp"

uint8_t PlayerMountComponent::getLastMount() const {
	const int32_t value = m_player.getStorageValue(PSTRG_MOUNTS_CURRENTMOUNT);
	if (value > 0) {
		return static_cast<uint8_t>(value);
	}
	const auto lastMount = m_player.kv()->get("last-mount");
	if (!lastMount.has_value()) {
		return 0;
	}
	return static_cast<uint8_t>(lastMount->get<int>());
}

uint8_t PlayerMountComponent::getCurrentMount() const {
	const int32_t value = m_player.getStorageValue(PSTRG_MOUNTS_CURRENTMOUNT);
	if (value > 0) {
		return value;
	}
	return 0;
}

void PlayerMountComponent::setCurrentMount(uint8_t mountId) {
	m_player.addStorageValue(PSTRG_MOUNTS_CURRENTMOUNT, mountId);
}

bool PlayerMountComponent::hasAnyMount() const {
	const auto &mounts = g_game().mounts->getMounts();
	return std::ranges::any_of(mounts, [&](const auto &mount) {
		return hasMount(mount);
	});
}

uint8_t PlayerMountComponent::getRandomMountId() const {
	std::vector<uint8_t> playerMounts;
	const auto mounts = g_game().mounts->getMounts();
	for (const auto &mount : mounts) {
		if (hasMount(mount)) {
			playerMounts.emplace_back(mount->id);
		}
	}

	if (playerMounts.empty()) {
		return 0;
	}

	const auto randomIndex = uniform_random(0, static_cast<int32_t>(playerMounts.size() - 1));
	if (randomIndex >= 0 && static_cast<size_t>(randomIndex) < playerMounts.size()) {
		return playerMounts[randomIndex];
	}

	return 0;
}

bool PlayerMountComponent::toggleMount(bool mount) {
	if ((OTSYS_TIME() - m_player.lastToggleMount) < 3000 && !m_player.wasMounted) {
		m_player.sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (m_player.isWearingSupportOutfit()) {
		return false;
	}

	if (mount) {
		if (!g_game().outfitSupportsMount(m_player.defaultOutfit.lookType)) {
			m_player.sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return false;
		}

		if (m_player.isMounted()) {
			return false;
		}

		const auto &tile = m_player.getTile();
		if (!g_configManager().getBoolean(TOGGLE_MOUNT_IN_PZ) && !m_player.group->access && tile && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
			m_player.sendCancelMessage(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
			return false;
		}

		const auto &playerOutfit = Outfits::getInstance().getOutfitByLookType(m_player.getPlayer(), m_player.defaultOutfit.lookType);
		if (!playerOutfit) {
			return false;
		}

		uint8_t currentMountId = getLastMount();
		if (currentMountId == 0) {
			m_player.sendOutfitWindow();
			return false;
		}

		if (m_player.isRandomMounted()) {
			currentMountId = getRandomMountId();
		}

		const auto &currentMount = g_game().mounts->getMountByID(currentMountId);
		if (!currentMount) {
			return false;
		}

		if (!hasMount(currentMount)) {
			setCurrentMount(0);
			m_player.kv()->set("last-mount", 0);
			m_player.sendOutfitWindow();
			return false;
		}

		if (currentMount->premium && !m_player.isPremium()) {
			m_player.sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
			return false;
		}

		if (m_player.hasCondition(CONDITION_OUTFIT)) {
			m_player.sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return false;
		}

		m_player.defaultOutfit.lookMount = currentMount->clientId;
		setCurrentMount(currentMount->id);
		m_player.kv()->set("last-mount", currentMount->id);

		if (currentMount->speed != 0) {
			g_game().changeSpeed(m_player.static_self_cast<Player>(), currentMount->speed);
		}
	} else {
		if (!m_player.isMounted()) {
			return false;
		}

		dismount();
	}

	g_game().internalCreatureChangeOutfit(m_player.static_self_cast<Player>(), m_player.defaultOutfit);
	m_player.lastToggleMount = OTSYS_TIME();
	return true;
}

bool PlayerMountComponent::tameMount(uint8_t mountId) {
	if (!g_game().mounts->getMountByID(mountId)) {
		return false;
	}

	const uint8_t tmpMountId = mountId - 1;
	const uint32_t key = PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31);

	int32_t value = m_player.getStorageValue(key);
	if (value != -1) {
		value |= (1 << (tmpMountId % 31));
	} else {
		value = (1 << (tmpMountId % 31));
	}

	m_player.addStorageValue(key, value);
	return true;
}

bool PlayerMountComponent::untameMount(uint8_t mountId) {
	if (!g_game().mounts->getMountByID(mountId)) {
		return false;
	}

	const uint8_t tmpMountId = mountId - 1;
	const uint32_t key = PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31);

	int32_t value = m_player.getStorageValue(key);
	if (value == -1) {
		return true;
	}

	value &= ~(1 << (tmpMountId % 31));
	m_player.addStorageValue(key, value);

	if (getCurrentMount() == mountId) {
		if (m_player.isMounted()) {
			dismount();
			g_game().internalCreatureChangeOutfit(m_player.static_self_cast<Player>(), m_player.defaultOutfit);
		}

		setCurrentMount(0);
		m_player.kv()->set("last-mount", 0);
	}

	return true;
}

bool PlayerMountComponent::hasMount(const std::shared_ptr<Mount> &mount) const {
	if (m_player.isAccessPlayer()) {
		return true;
	}

	if (mount->premium && !m_player.isPremium()) {
		return false;
	}

	const uint8_t tmpMountId = mount->id - 1;
	const int32_t value = m_player.getStorageValue(PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31));
	if (value == -1) {
		return false;
	}

	return ((1 << (tmpMountId % 31)) & value) != 0;
}

void PlayerMountComponent::dismount() {
	const auto &mount = g_game().mounts->getMountByID(getCurrentMount());
	if (mount && mount->speed > 0) {
		g_game().changeSpeed(m_player.static_self_cast<Player>(), -mount->speed);
	}

	m_player.defaultOutfit.lookMount = 0;
}

uint8_t PlayerMountComponent::isRandomMounted() const {
	return m_player.randomMount;
}

void PlayerMountComponent::setRandomMount(uint8_t isMountRandomized) {
	m_player.randomMount = isMountRandomized;
}
