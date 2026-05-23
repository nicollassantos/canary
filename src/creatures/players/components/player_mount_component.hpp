/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include <cstdint>
#include <memory>

class Player;
struct Mount;

class PlayerMountComponent {
public:
	PlayerMountComponent() = delete;
	explicit PlayerMountComponent(Player &player) :
		m_player(player) { }

	uint8_t getLastMount() const;
	uint8_t getCurrentMount() const;
	void setCurrentMount(uint8_t mountId);
	bool hasAnyMount() const;
	uint8_t getRandomMountId() const;
	bool toggleMount(bool mount);
	bool tameMount(uint8_t mountId);
	bool untameMount(uint8_t mountId);
	bool hasMount(const std::shared_ptr<Mount> &mount) const;
	void dismount();
	uint8_t isRandomMounted() const;
	void setRandomMount(uint8_t isMountRandomized);

private:
	Player &m_player;
};
