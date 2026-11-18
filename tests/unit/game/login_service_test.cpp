/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "account/account.hpp"
#include "account/in_memory_account_repository.hpp"
#include "account/in_memory_ban_repository.hpp"
#include "config/in_memory_config_manager.hpp"
#include "creatures/players/grouping/groups.hpp"
#include "creatures/players/player.hpp"
#include "game/login/login_service.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "utils/tools.hpp"

// The admission rules of a login, with the bans, the config and the world as plain fakes (before,
// this lived in ProtocolGame::login and only a client against a running server exercised it).
namespace {

	class FakeLoginWorld final : public LoginWorld {
	public:
		GameState_t gameState() const override {
			return state;
		}

		std::vector<std::shared_ptr<Player>> playersOfAccount(const std::shared_ptr<Account> &account) override {
			std::vector<std::shared_ptr<Player>> found;
			for (const auto &player : online) {
				if (account && player->getAccount() && player->getAccount()->getID() == account->getID()) {
					found.push_back(player);
				}
			}
			return found;
		}

		GameState_t state = GAME_STATE_NORMAL;
		std::vector<std::shared_ptr<Player>> online;
	};

	class LoginServiceTest : public ::testing::Test {
	protected:
		void SetUp() override {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			tests::InMemoryAccountRepository::install(injector);
			DI::setTestContainer(&injector);
			accounts = &dynamic_cast<tests::InMemoryAccountRepository &>(DI::get<AccountRepository>());

			config.setNumber(MAX_PLAYERS_PER_ACCOUNT, 1);
			config.setNumber(MAX_PLAYERS_OUTSIDE_PZ_PER_ACCOUNT, 1);
		}

		void TearDown() override {
			DI::setTestContainer(previousContainer);
		}

		void addAccount(uint32_t accountId, std::vector<std::string> names) {
			AccountInfo info;
			info.id = accountId;
			for (const auto &name : names) {
				info.players.emplace(name, 0);
			}
			accounts->addAccount("account" + std::to_string(accountId), info);
		}

		std::shared_ptr<Player> makePlayer(uint32_t guid, const std::string &name, uint32_t accountId) {
			auto player = std::make_shared<Player>(nullptr);
			player->setGroup(std::make_shared<Group>());
			player->setGUID(guid);
			player->setName(name);
			EXPECT_TRUE(player->setAccount(accountId));
			return player;
		}

		std::shared_ptr<Player> makeAccountWithPlayer(uint32_t guid, const std::string &name, uint32_t accountId) {
			addAccount(accountId, { name });
			return makePlayer(guid, name, accountId);
		}

		void putOnline(const std::shared_ptr<Player> &player, const Position &position, bool protectionZone) {
			auto tile = std::make_shared<DynamicTile>(position);
			if (protectionZone) {
				tile->setFlag(TILESTATE_PROTECTIONZONE);
			}
			tile->addThing(player);
			tiles.push_back(tile);
			world.online.push_back(player);
		}

		FakeLoginWorld world;
		InMemoryConfigManager config;
		tests::InMemoryBanRepository bans;
		LoginService service { world, config, bans };
		tests::InMemoryAccountRepository* accounts = nullptr;
		std::vector<std::shared_ptr<Tile>> tiles;

		di::extension::injector<> injector {};
		di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

TEST_F(LoginServiceTest, AnEligibleCharacterIsAdmitted) {
	const auto player = makeAccountWithPlayer(41, "Login Ok", 9001);
	EXPECT_EQ(std::nullopt, service.refuseBeforeLoad(*player, 9001));
}

TEST_F(LoginServiceTest, ANamelockedCharacterIsRefused) {
	const auto player = makeAccountWithPlayer(42, "Login Namelocked", 9002);
	bans.namelocked.insert(42);
	EXPECT_EQ(std::optional<std::string>("Your character has been namelocked."), service.refuseBeforeLoad(*player, 9002));
}

TEST_F(LoginServiceTest, AClosingOrClosedServerAdmitsOnlyWhoCanAlwaysLogin) {
	const auto player = makeAccountWithPlayer(43, "Login Closing", 9003);
	world.state = GAME_STATE_CLOSING;
	EXPECT_EQ(std::optional<std::string>("The game is just going down.\nPlease try again later."), service.refuseBeforeLoad(*player, 9003));

	world.state = GAME_STATE_CLOSED;
	EXPECT_EQ(std::optional<std::string>("Server is currently closed.\nPlease try again later."), service.refuseBeforeLoad(*player, 9003));
	config.setString(MAINTAIN_MODE_MESSAGE, "Back at noon.");
	EXPECT_EQ(std::optional<std::string>("Back at noon."), service.refuseBeforeLoad(*player, 9003));

	player->setFlag(PlayerFlags_t::CanAlwaysLogin);
	EXPECT_EQ(std::nullopt, service.refuseBeforeLoad(*player, 9003));
}

TEST_F(LoginServiceTest, APremiumOnlyServerRefusesFreeAccounts) {
	const auto player = makeAccountWithPlayer(44, "Login Free", 9004);
	config.setBoolean(ONLY_PREMIUM_ACCOUNT, true);
	const auto refusal = service.refuseBeforeLoad(*player, 9004);
	ASSERT_TRUE(refusal.has_value());
	EXPECT_NE(std::string::npos, refusal->find("premium time"));

	player->setFlag(PlayerFlags_t::IsAlwaysPremium);
	EXPECT_EQ(std::nullopt, service.refuseBeforeLoad(*player, 9004));
}

TEST_F(LoginServiceTest, ATemporaryAccountBanShowsWhenWhoAndWhy) {
	const auto player = makeAccountWithPlayer(45, "Login Banned", 9005);
	bans.accountBans[9005] = BanInfo { "Gamemaster", "", 1'900'000'000 };
	EXPECT_EQ(std::optional<std::string>("Your account has been banned until " + formatDateShort(1'900'000'000) + " by Gamemaster.\n\nReason specified:\n(none)"), service.refuseBeforeLoad(*player, 9005));

	bans.accountBans[9005] = BanInfo { "Gamemaster", "Botting", 0 };
	EXPECT_EQ(std::optional<std::string>("Your account has been permanently banned by Gamemaster.\n\nReason specified:\nBotting"), service.refuseBeforeLoad(*player, 9005));

	player->setFlag(PlayerFlags_t::CannotBeBanned);
	EXPECT_EQ(std::nullopt, service.refuseBeforeLoad(*player, 9005));
}

TEST_F(LoginServiceTest, AnAccountCannotHaveMoreCharactersOnlineThanTheLimit) {
	addAccount(9006, { "Login First", "Login Second" });
	const auto online = makePlayer(46, "Login First", 9006);
	putOnline(online, Position(100, 100, 7), true);
	const auto second = makePlayer(47, "Login Second", 9006);

	EXPECT_EQ(std::optional<std::string>("You may only login with 1 character\nof your account at the same time."), service.refuseBeforeLoad(*second, 9006));
	config.setNumber(MAX_PLAYERS_PER_ACCOUNT, 2);
	EXPECT_EQ(std::nullopt, service.refuseBeforeLoad(*second, 9006));
}

TEST_F(LoginServiceTest, OnlySomeCharactersOfAnAccountMayBeOutsideAProtectionZone) {
	config.setNumber(MAX_PLAYERS_PER_ACCOUNT, 2);
	addAccount(9007, { "Login Hunting", "Login Arriving" });
	const auto hunting = makePlayer(48, "Login Hunting", 9007);
	putOnline(hunting, Position(110, 100, 7), false);
	const auto arriving = makePlayer(49, "Login Arriving", 9007);

	DynamicTile outside(Position(120, 100, 7));
	DynamicTile temple(Position(121, 100, 7));
	temple.setFlag(TILESTATE_PROTECTIONZONE);
	EXPECT_EQ(std::optional<std::string>("You can only have one character from your account outside of a protection zone."), service.refuseOutsideProtectionZone(arriving, outside));
	EXPECT_EQ(std::nullopt, service.refuseOutsideProtectionZone(arriving, temple));
}

TEST_F(LoginServiceTest, ABannedAddressIsRefusedBeforeAnyCharacter) {
	EXPECT_EQ(std::nullopt, service.refuseBannedAddress(0x0100007F));
	bans.addressBans[0x0100007F] = BanInfo { "Admin", "Flooding", 1'900'000'000 };
	EXPECT_EQ(std::optional<std::string>("Your IP has been banned until " + formatDateShort(1'900'000'000) + " by Admin.\n\nReason specified:\nFlooding"), service.refuseBannedAddress(0x0100007F));
}
