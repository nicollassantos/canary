/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */
#pragma once

#include "account/account_repository_db.hpp"
#include "config/configmanager.hpp"
#include "creatures/appearance/outfit/outfit.hpp"
#include "creatures/combat/spells.hpp"
#include "creatures/interactions/chat.hpp"
#include "creatures/monsters/monsters.hpp"
#include "creatures/npcs/npcs.hpp"
#include "creatures/players/grouping/familiars.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player_repository_db.hpp"
#include "io/guild_repository_db.hpp"
#include "io/market_repository_db.hpp"
#include "creatures/players/vocations/vocation.hpp"
#include "database/database.hpp"
#include "game/scheduling/dispatcher.hpp"
#include "game/scheduling/events_scheduler.hpp"
#include "game/scheduling/save_manager.hpp"
#include "io/io_bosstiary.hpp"
#include "lua/creature/actions.hpp"
#include "lua/creature/creatureevent.hpp"
#include "lua/creature/events.hpp"
#include "lua/creature/movement.hpp"
#include "lua/creature/talkaction.hpp"
#include "lua/scripts/scripts.hpp"
#include "io/iobestiary.hpp"
#include "io/ioprey.hpp"
#include "items/weapons/weapons.hpp"
#include "lua/global/globalevent.hpp"
#include "lib/di/injector.hpp"
#include "lib/logging/log_with_spd_log.hpp"
#include "lib/thread/thread_pool.hpp"
#include "kv/kv_sql.hpp"
#include "security/rsa.hpp"
#include "server/network/message/outputmessage.hpp"
#include "server/network/webhook/webhook.hpp"

namespace di = boost::di;

class DI final {
private:
	inline static di::extension::injector<>* testContainer;
	const inline static auto defaultContainer = di::make_injector(
		di::bind<AccountRepository>().to<AccountRepositoryDB>().in(di::singleton),
		di::bind<IConfigManager>().to<ConfigManager>().in(di::singleton),
		di::bind<IDatabase>().to<Database>().in(di::singleton),
		di::bind<IGuildRepository>().to<GuildRepositoryDB>().in(di::singleton),
		di::bind<IMarketRepository>().to<MarketRepositoryDB>().in(di::singleton),
		di::bind<IPlayerRepository>().to<PlayerRepositoryDB>().in(di::singleton),
		di::bind<KVStore>().to<KVSQL>().in(di::singleton),
		di::bind<Logger>().to<LogWithSpdLog>().in(di::singleton),
		// Phase 7.1 — infrastructure singletons
		di::bind<OutputMessagePool>().in(di::singleton),
		di::bind<RSAManager>().in(di::singleton),
		di::bind<ThreadPool>().in(di::singleton),
		di::bind<Webhook>().in(di::singleton),
		// Phase 7.2 — game data singletons
		di::bind<Chat>().in(di::singleton),
		di::bind<Familiars>().in(di::singleton),
		di::bind<Imbuements>().in(di::singleton),
		di::bind<Monsters>().in(di::singleton),
		di::bind<Npcs>().in(di::singleton),
		di::bind<Outfits>().in(di::singleton),
		di::bind<Spells>().in(di::singleton),
		di::bind<Vocations>().in(di::singleton),
		di::bind<Weapons>().in(di::singleton),
		// Phase 7.3 — IO → repository singletons
		di::bind<IOBestiary>().in(di::singleton),
		di::bind<IOBosstiary>().in(di::singleton),
		di::bind<IOPrey>().in(di::singleton),
		// Phase 7.4 — scheduling singletons
		di::bind<Dispatcher>().in(di::singleton),
		di::bind<EventsScheduler>().in(di::singleton),
		di::bind<GlobalEvents>().in(di::singleton),
		di::bind<SaveManager>().in(di::singleton),
		// Lua subsystems
		di::bind<Actions>().in(di::singleton),
		di::bind<CreatureEvents>().in(di::singleton),
		di::bind<Events>().in(di::singleton),
		di::bind<MoveEvents>().in(di::singleton),
		di::bind<TalkActions>().in(di::singleton)
		// Scripts excluded: holds LuaScriptInterface by value, exceeds boost::di 10-param limit
	);

public:
	inline static void setTestContainer(di::extension::injector<>* container) {
		testContainer = container;
	}
	inline static di::extension::injector<>* getTestContainer() {
		return testContainer;
	}

	/**
	 * Create will always return a new instance, it's used for unique instances or non-shared
	 * states. This can only be used by classes that allow being copied, cloned and moved.
	 * Instances acquired with create need to be managed by the caller using smart pointers.
	 */
	template <class T>
	inline static T create() {
		return testContainer ? testContainer->create<T>() : defaultContainer.create<T>();
	}

	/**
	 * Get returns you a reference of a instance that the DI contains.
	 * It will always return the same instance, it's used for singletons shared instances.
	 * Instances acquired with get are managed by the DI and can be merely references.
	 */
	template <class T>
	inline static T &get() {
		return create<T &>();
	}
};

/**
 * Simplified global function for contextual injection. Keep in mind that constructor injection is always
 * a better choice than contextual injection. This is only a helper to simplify injection in
 * complex legacy contexts.
 */
template <typename Type>
inline Type &inject() {
	return DI::get<Type>();
}
