/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/globals/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "config/config_port.hpp"
#include "creatures/players/player.hpp"
#include "game/bank/bank.hpp"
#include "lib/di/container.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "map/town.hpp"

#include "config/in_memory_config_manager.hpp"

namespace {

	// Minimal Bankable stub — no g_game() dependency, no Player overhead.
	class SimpleBankable : public Bankable {
	public:
		explicit SimpleBankable(uint64_t initial = 0) :
			balance_(initial) { }

		void setBankBalance(uint64_t amount) override { balance_ = amount; }
		uint64_t getBankBalance() const override { return balance_; }
		void setOnline(bool v) override { online_ = v; }
		bool isOnline() const override { return online_; }

	private:
		uint64_t balance_ = 0;
		bool online_ = false;
	};

	// Bankable that exposes a Player pointer — needed for transfer town-check path.
	class PlayerBankable : public Bankable {
	public:
		explicit PlayerBankable(std::shared_ptr<Player> p, uint64_t initial = 0) :
			player_(std::move(p)), balance_(initial) { }

		std::shared_ptr<Player> getPlayer() override { return player_; }
		void setBankBalance(uint64_t amount) override { balance_ = amount; }
		uint64_t getBankBalance() const override { return balance_; }
		void setOnline(bool v) override { online_ = v; }
		bool isOnline() const override { return online_; }

	private:
		std::shared_ptr<Player> player_;
		uint64_t balance_ = 0;
		bool online_ = false;
	};

	static std::shared_ptr<Player> makeNamedPlayer(const std::string &name) {
		auto p = std::make_shared<Player>();
		p->setName(name);
		return p;
	}

	// ---- Test fixture ----

	class BankTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			InMemoryConfigManager::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}

	protected:
		// Return the InMemoryConfigManager so tests can configure it.
		static InMemoryConfigManager &cfg() {
			return static_cast<InMemoryConfigManager &>(inject<IConfigManager>());
		}

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- credit ---

TEST_F(BankTest, Credit_IncreasesBalance) {
	auto bankable = std::make_shared<SimpleBankable>(100);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_TRUE(bank->credit(50));
	EXPECT_EQ(150u, bank->balance());
}

TEST_F(BankTest, Credit_ReturnsFalse_WhenBankableIsNull) {
	auto bank = std::make_shared<Bank>(nullptr);
	EXPECT_FALSE(bank->credit(50));
}

// --- debit ---

TEST_F(BankTest, Debit_DecreasesBalance_WhenSufficient) {
	auto bankable = std::make_shared<SimpleBankable>(100);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_TRUE(bank->debit(50));
	EXPECT_EQ(50u, bank->balance());
}

TEST_F(BankTest, Debit_ReturnsFalse_WhenInsufficient) {
	auto bankable = std::make_shared<SimpleBankable>(30);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_FALSE(bank->debit(50));
}

TEST_F(BankTest, Debit_DoesNotChangeBalance_WhenInsufficient) {
	auto bankable = std::make_shared<SimpleBankable>(30);
	auto bank = std::make_shared<Bank>(bankable);
	bank->debit(50);
	EXPECT_EQ(30u, bank->balance());
}

// --- hasBalance ---

TEST_F(BankTest, HasBalance_ReturnsTrue_WhenEqual) {
	auto bankable = std::make_shared<SimpleBankable>(50);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_TRUE(bank->hasBalance(50));
}

TEST_F(BankTest, HasBalance_ReturnsFalse_WhenInsufficient) {
	auto bankable = std::make_shared<SimpleBankable>(30);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_FALSE(bank->hasBalance(50));
}

// --- transferTo (non-player bankables — no config/town check) ---

TEST_F(BankTest, TransferTo_ReturnsFalse_WhenDestinationIsNull) {
	auto bankable = std::make_shared<SimpleBankable>(200);
	auto bank = std::make_shared<Bank>(bankable);
	EXPECT_FALSE(bank->transferTo(nullptr, 80));
}

TEST_F(BankTest, TransferTo_MovesAmount_BetweenGuildBankables) {
	auto srcBankable = std::make_shared<SimpleBankable>(200);
	auto dstBankable = std::make_shared<SimpleBankable>(100);
	auto src = std::make_shared<Bank>(srcBankable);
	auto dst = std::make_shared<Bank>(dstBankable);

	EXPECT_TRUE(src->transferTo(dst, 80));
	EXPECT_EQ(120u, src->balance());
	EXPECT_EQ(180u, dst->balance());
}

TEST_F(BankTest, TransferTo_ReturnsFalse_WhenSourceHasInsufficientBalance) {
	auto srcBankable = std::make_shared<SimpleBankable>(30);
	auto dstBankable = std::make_shared<SimpleBankable>(100);
	auto src = std::make_shared<Bank>(srcBankable);
	auto dst = std::make_shared<Bank>(dstBankable);

	EXPECT_FALSE(src->transferTo(dst, 80));
}

TEST_F(BankTest, TransferTo_BalancesUnchanged_WhenSourceHasInsufficientBalance) {
	auto srcBankable = std::make_shared<SimpleBankable>(30);
	auto dstBankable = std::make_shared<SimpleBankable>(100);
	auto src = std::make_shared<Bank>(srcBankable);
	auto dst = std::make_shared<Bank>(dstBankable);

	src->transferTo(dst, 80);
	EXPECT_EQ(30u, src->balance());
	EXPECT_EQ(100u, dst->balance());
}

// --- transferTo (player bankables — denied-name check) ---

TEST_F(BankTest, TransferTo_ReturnsFalse_WhenDestinationNameIsDenied) {
	auto srcPlayer = makeNamedPlayer("TestSender");
	auto dstPlayer = makeNamedPlayer("accountmanager");
	srcPlayer->setTown(std::make_shared<Town>(1));
	dstPlayer->setTown(std::make_shared<Town>(1));

	auto srcBankable = std::make_shared<PlayerBankable>(srcPlayer, 500);
	auto dstBankable = std::make_shared<PlayerBankable>(dstPlayer, 0);
	auto src = std::make_shared<Bank>(srcBankable);
	auto dst = std::make_shared<Bank>(dstBankable);

	EXPECT_FALSE(src->transferTo(dst, 100));
	EXPECT_EQ(500u, src->balance());
	EXPECT_EQ(0u, dst->balance());
}

// --- transferTo (player bankables — town-ID check via injected config) ---

TEST_F(BankTest, TransferTo_ReturnsFalse_WhenMainPlayerSendsToNewWorldPlayer) {
	cfg().setNumber(MIN_TOWN_ID_TO_BANK_TRANSFER_FROM_MAIN, 5);

	auto mainPlayer = makeNamedPlayer("MainSender");
	auto newPlayer = makeNamedPlayer("NewReceiver");
	mainPlayer->setTown(std::make_shared<Town>(5)); // main world
	newPlayer->setTown(std::make_shared<Town>(2));  // new world

	auto srcBankable = std::make_shared<PlayerBankable>(mainPlayer, 500);
	auto dstBankable = std::make_shared<PlayerBankable>(newPlayer, 0);
	auto src = std::make_shared<Bank>(srcBankable, cfg());
	auto dst = std::make_shared<Bank>(dstBankable, cfg());

	EXPECT_FALSE(src->transferTo(dst, 100));

	cfg().reset();
}

TEST_F(BankTest, TransferTo_Succeeds_WhenBothPlayersInSameTownCategory) {
	cfg().setNumber(MIN_TOWN_ID_TO_BANK_TRANSFER_FROM_MAIN, 5);

	auto p1 = makeNamedPlayer("Player1");
	auto p2 = makeNamedPlayer("Player2");
	p1->setTown(std::make_shared<Town>(6)); // both main world
	p2->setTown(std::make_shared<Town>(7));

	auto b1 = std::make_shared<PlayerBankable>(p1, 300);
	auto b2 = std::make_shared<PlayerBankable>(p2, 100);
	auto bank1 = std::make_shared<Bank>(b1, cfg());
	auto bank2 = std::make_shared<Bank>(b2, cfg());

	EXPECT_TRUE(bank1->transferTo(bank2, 100));
	EXPECT_EQ(200u, bank1->balance());
	EXPECT_EQ(200u, bank2->balance());

	cfg().reset();
}
