/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019–present OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include <gtest/gtest.h>

#include "creatures/players/player.hpp"
#include "enums/item_attribute.hpp"
#include "game/game.hpp"
#include "items/item.hpp"
#include "items/tile.hpp"
#include "lib/logging/in_memory_logger.hpp"
#include "test_items.hpp"

namespace {

	constexpr uint16_t kNormalItemId = 100;
	constexpr uint16_t kStackableItemId = 101;
	const Position kPosA { 100, 100, 7 };
	const Position kPosB { 101, 100, 7 };

	std::shared_ptr<Player> makePlacedPlayer(uint32_t guid, const Position &pos) {
		auto player = std::make_shared<Player>();
		player->setName("TradeTest" + std::to_string(guid));
		player->setGUID(guid);
		player->setID();
		auto tile = std::make_shared<DynamicTile>(pos);
		tile->addThing(player);
		return player;
	}

	class TradeServiceTest : public ::testing::Test {
	public:
		static void SetUpTestSuite() {
			test_items::seedFallbackTestItems();

			previousContainer = DI::getTestContainer();
			InMemoryLogger::install(injector);
			DI::setTestContainer(&injector);
		}

		static void TearDownTestSuite() {
			DI::setTestContainer(previousContainer);
		}


	protected:
		Game game;

		inline static di::extension::injector<> injector {};
		inline static di::extension::injector<>* previousContainer = nullptr;
	};

} // namespace

// --- getTradeErrorDescription (pure static logic) ---

// nullptr item → generic message regardless of error code
TEST_F(TradeServiceTest, GetTradeErrorDescription_NullItem_ReturnsGenericMessage) {
	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_NOTPOSSIBLE, nullptr);
	EXPECT_EQ("Trade could not be completed.", msg);
}

// NOTENOUGHCAPACITY + non-stackable item → "this object."
TEST_F(TradeServiceTest, GetTradeErrorDescription_CapacityError_NonStackable_ReturnsThisObject) {
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);
	ASSERT_FALSE(item->isStackable());
	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_NOTENOUGHCAPACITY, item);
	EXPECT_NE(std::string::npos, msg.find("this object."));
	EXPECT_EQ(std::string::npos, msg.find("these objects."));
}

// NOTENOUGHCAPACITY + stackable item count > 1 → "these objects."
// setItemCount is used to force count independently of constructor path;
// stackable flag is set on the type so isStackable() returns true for the call.
TEST_F(TradeServiceTest, GetTradeErrorDescription_CapacityError_StackableMultiple_ReturnsTheseObjects) {
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);
	item->setItemCount(5);

	// make the item type appear stackable for the duration of this call
	auto &itemsVec = Item::items.getItems();
	ASSERT_GT(itemsVec.size(), kNormalItemId);
	itemsVec[kNormalItemId].stackable = true;

	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_NOTENOUGHCAPACITY, item);
	EXPECT_NE(std::string::npos, msg.find("these objects."));
	EXPECT_EQ(std::string::npos, msg.find("this object."));

	itemsVec[kNormalItemId].stackable = false;
}

// NOTENOUGHROOM → "this object."
TEST_F(TradeServiceTest, GetTradeErrorDescription_RoomError_ReturnsThisObject) {
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);
	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_NOTENOUGHROOM, item);
	EXPECT_NE(std::string::npos, msg.find("this object."));
}

// CONTAINERNOTENOUGHROOM → "this object."
TEST_F(TradeServiceTest, GetTradeErrorDescription_ContainerRoomError_ReturnsThisObject) {
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);
	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_CONTAINERNOTENOUGHROOM, item);
	EXPECT_NE(std::string::npos, msg.find("this object."));
}

// Unknown error code + item → generic message
TEST_F(TradeServiceTest, GetTradeErrorDescription_UnknownError_WithItem_ReturnsGenericMessage) {
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);
	const std::string msg = Game::getTradeErrorDescription(RETURNVALUE_NOTPOSSIBLE, item);
	EXPECT_EQ("Trade could not be completed.", msg);
}

// --- internalStartTrade guard paths ---

// player already in TRADE_INITIATED → returns false
TEST_F(TradeServiceTest, InternalStartTrade_PlayerAlreadyTrading_ReturnsFalse) {
	auto playerA = makePlacedPlayer(7001, kPosA);
	auto playerB = makePlacedPlayer(7002, kPosB);
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);

	// Put playerA in an active trade with a third party (not playerB)
	playerA->setTradeState(TRADE_INITIATED);

	game.addPlayer(playerA);
	game.addPlayer(playerB);
	EXPECT_FALSE(game.internalStartTrade(playerA, playerB, item));
}

// trade item is a store item → returns false
TEST_F(TradeServiceTest, InternalStartTrade_StoreItem_ReturnsFalse) {
	auto playerA = makePlacedPlayer(7003, kPosA);
	auto playerB = makePlacedPlayer(7004, kPosB);
	const auto item = Item::CreateItem(kNormalItemId);
	ASSERT_NE(nullptr, item);

	item->setAttribute(ItemAttribute_t::STORE, static_cast<int64_t>(1));
	ASSERT_TRUE(item->isStoreItem());

	game.addPlayer(playerA);
	game.addPlayer(playerB);
	EXPECT_FALSE(game.internalStartTrade(playerA, playerB, item));
}
