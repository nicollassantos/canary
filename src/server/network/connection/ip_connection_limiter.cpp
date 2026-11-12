/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "server/network/connection/ip_connection_limiter.hpp"

bool IpConnectionLimiter::admit(const void* connection, uint32_t ip, int32_t maxPerIp) {
	std::scoped_lock guard(lock);
	if (ipByConnection.contains(connection)) {
		return true;
	}

	auto &count = countByIp[ip];
	if (maxPerIp > 0 && count >= static_cast<size_t>(maxPerIp)) {
		if (count == 0) {
			countByIp.erase(ip);
		}
		return false;
	}

	++count;
	ipByConnection.emplace(connection, ip);
	return true;
}

void IpConnectionLimiter::release(const void* connection) {
	std::scoped_lock guard(lock);
	const auto it = ipByConnection.find(connection);
	if (it == ipByConnection.end()) {
		return;
	}

	const auto counted = countByIp.find(it->second);
	if (counted != countByIp.end() && --counted->second == 0) {
		countByIp.erase(counted);
	}
	ipByConnection.erase(it);
}

size_t IpConnectionLimiter::openFrom(uint32_t ip) const {
	std::scoped_lock guard(lock);
	const auto it = countByIp.find(ip);
	return it == countByIp.end() ? 0 : it->second;
}
