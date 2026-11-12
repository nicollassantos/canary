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
	#include <cstddef>
	#include <cstdint>
	#include <mutex>
	#include <parallel_hashmap/phmap.h>
#endif

/// Counts connections per remote IP that have not logged in yet, and refuses new ones above a limit.
///
/// Ban::acceptConnection only limits how fast an IP connects; without this, a client opening one
/// connection every ~600 ms keeps them all. Logged-in players are released (their account already
/// limits them), so many real players behind one carrier-grade NAT IP are not affected.
/// Connections are identified by an opaque pointer. Thread-safe (network threads).
class IpConnectionLimiter {
public:
	/// Records `connection` for `ip` unless the IP already holds `maxPerIp` connections.
	/// `maxPerIp` <= 0 disables the limit. Admitting the same connection twice is a no-op.
	bool admit(const void* connection, uint32_t ip, int32_t maxPerIp);
	/// Forgets `connection` (closed or logged in); unknown connections are ignored.
	void release(const void* connection);

	[[nodiscard]] size_t openFrom(uint32_t ip) const;

private:
	mutable std::mutex lock;
	phmap::flat_hash_map<const void*, uint32_t> ipByConnection;
	phmap::flat_hash_map<uint32_t, size_t> countByIp;
};
