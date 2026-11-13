/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/in_memory_player_repository.hpp"
#include "creatures/players/grouping/guild.hpp"
#include "io/in_memory_guild_repository.hpp"
#include "creatures/players/player.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "lua/functions/core/game/bank_functions.hpp"
#include "lua/functions/core/game/game_functions.hpp"
#include "lua/functions/lua_functions_loader.hpp"
#include "test_items.hpp"

namespace {

	constexpr auto SourceName = "Offline Source";
	constexpr auto TargetName = "Offline Target";

	// `!transfer Name,amount` is a talkaction any player can say. It called
	// Game.getNormalizedPlayerName(name) and then Bank.transfer(player, name, amount), and both
	// loaded the named offline character whole before the sender's balance was checked.
	class PlayerNameLookupFunctionsTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TestItems::init(); // Player() creates its inbox from the item catalog
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryPlayerRepository::install(injector);
			InMemoryGuildRepository::install(injector);
			DI::setTestContainer(&injector);
			repository = &dynamic_cast<InMemoryPlayerRepository &>(IPlayerRepository::getInstance());
			repository->reset();
			guilds = &dynamic_cast<InMemoryGuildRepository &>(IGuildRepository::getInstance());
			guilds->reset();
			guilds->addGuild(std::make_shared<Guild>(71, "Red Rose"));

			uint32_t guid = 61;
			for (const auto* name : { SourceName, TargetName }) {
				auto offline = std::make_shared<Player>(nullptr);
				offline->setGUID(guid++);
				offline->setName(name);
				repository->savePlayer(offline);
			}

			GameFunctions::init(L.get());
			BankFunctions::init(L.get());
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		// Calls Table.method(args...) with string/number arguments and returns the result's type.
		template <typename... Args>
		int call(const char* table, const char* method, Args... args) {
			lua_settop(L.get(), 0);
			lua_getglobal(L.get(), table);
			lua_getfield(L.get(), -1, method);
			lua_remove(L.get(), -2);
			(push(args), ...);
			EXPECT_EQ(LUA_OK, lua_pcall(L.get(), sizeof...(Args), 1, 0)) << lua_tostring(L.get(), -1);
			return lua_type(L.get(), -1);
		}

		void push(const char* value) {
			lua_pushstring(L.get(), value);
		}

		void push(const std::string &value) {
			lua_pushlstring(L.get(), value.data(), value.size());
		}

		void push(int value) {
			lua_pushinteger(L.get(), value);
		}

		bool loggedError() const {
			const auto &logger = dynamic_cast<InMemoryLogger &>(DI::get<Logger>());
			return std::ranges::any_of(logger.logs, [](const auto &entry) {
				return entry.level == "error";
			});
		}

		std::unique_ptr<lua_State, decltype(&lua_close)> L { luaL_newstate(), &lua_close };
		InMemoryPlayerRepository* repository = nullptr;
		InMemoryGuildRepository* guilds = nullptr;
		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(PlayerNameLookupFunctionsTest, NormalizedNameOfAnOfflinePlayerDoesNotLoadIt) {
	ASSERT_EQ(LUA_TSTRING, call("Game", "getNormalizedPlayerName", "offline target"));
	EXPECT_STREQ(TargetName, lua_tostring(L.get(), -1));

	EXPECT_EQ(0U, repository->playerLoads);
}

TEST_F(PlayerNameLookupFunctionsTest, UnknownNameIsNilWithoutLoadOrError) {
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedPlayerName", "Nobody Here"));
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedPlayerName", "Nobody Here", 1));

	EXPECT_EQ(0U, repository->playerLoads);
	EXPECT_FALSE(loggedError());
}

TEST_F(PlayerNameLookupFunctionsTest, OverlongOrEmptyNameIsNil) {
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedPlayerName", std::string(NETWORKMESSAGE_PLAYERNAME_MAXLENGTH + 1, 'a')));
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedPlayerName", ""));

	EXPECT_EQ(0U, repository->playerLoads);
}

// The source (loaded here by name, with no balance) cannot pay, so the destination must not be
// resolved: before the fix it was loaded too.
TEST_F(PlayerNameLookupFunctionsTest, TransferWithoutBalanceDoesNotLoadTheDestination) {
	ASSERT_EQ(LUA_TBOOLEAN, call("Bank", "transfer", SourceName, TargetName, 1));
	EXPECT_FALSE(lua_toboolean(L.get(), -1));

	EXPECT_EQ(1U, repository->playerLoads);
}

TEST_F(PlayerNameLookupFunctionsTest, TransferOfNothingDoesNotLoadTheDestination) {
	ASSERT_EQ(LUA_TBOOLEAN, call("Bank", "transfer", SourceName, TargetName, 0));
	EXPECT_FALSE(lua_toboolean(L.get(), -1));

	EXPECT_EQ(1U, repository->playerLoads);
}

// The bank NPCs pass the typed guild name: it loaded the guild (name, balance, ranks) per message.
TEST_F(PlayerNameLookupFunctionsTest, NormalizedGuildNameDoesNotLoadTheGuild) {
	ASSERT_EQ(LUA_TSTRING, call("Game", "getNormalizedGuildName", "red rose"));
	EXPECT_STREQ("Red Rose", lua_tostring(L.get(), -1));
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedGuildName", "No Such Guild"));
	EXPECT_EQ(LUA_TNIL, call("Game", "getNormalizedGuildName", ""));

	EXPECT_EQ(0U, guilds->guildLoads);
}
