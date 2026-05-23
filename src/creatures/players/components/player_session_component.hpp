#pragma once

#include <cstdint>

class Player;

class PlayerSessionComponent {
public:
	PlayerSessionComponent() = delete;
	explicit PlayerSessionComponent(Player &player) :
		m_player(player) { }

	void addList();
	void removeList();
	void removePlayer(bool displayEffect, bool forced = true);
	bool canLogout();

	void addInFightTicks(bool pzlock = false);

	void setLoginProtection(int64_t time);
	bool isLoginProtected() const;
	void resetLoginProtection();

	void setProtection(bool status);
	bool isProtected();

private:
	Player &m_player;
};
