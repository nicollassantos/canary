-- A house purchase whose payment fails takes the delivered kits back (upstream PR #4184, bug fixes).
-- Run from the repository root: luajit tests/lua/test_gamestore_house_payment.lua
--
-- House kits are delivered inside the purchase handler, before makeCoinTransaction. When the payment
-- failed, the kits stayed in the inbox for free and the purchase summary was already recorded.
-- The real parseBuyStoreOffer runs here; only engine objects (player, kv, message) are stubbed.
package.path = "data/libs/?.lua;" .. package.path
GameStore = require("gamestore.constants")
package.loaded["gamestore.helpers"] = {
	useOfferConfigure = function()
		return GameStore.ConfigureOffers.SHOW_NORMAL
	end,
}
local noop = function() end
package.loaded["gamestore.senders"] = setmetatable({}, {
	__index = function()
		return noop
	end,
})

function table.contains(list, value)
	for _, v in pairs(list) do
		if v == value then
			return true
		end
	end
	return false
end
MESSAGE_EVENT_ADVANCE = 1
logger = { warn = noop }
addPlayerEvent = noop

local parsing = dofile("data/libs/gamestore/parsers.lua")

local failures = 0
local function check(ok, message)
	print((ok and "PASS " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

local function purchase(paymentSucceeds)
	local state = { rolledBack = 0, summaries = 0, delivered = 0, modal = nil }
	local offer = { id = 777, name = "Azure Carpet", type = GameStore.OfferTypes.OFFER_TYPE_HOUSE, itemtype = 23710, count = 5, price = 10 }
	GameStore.getOfferById = function()
		return offer
	end
	GameStore.processHouseRelatedPurchase = function()
		state.delivered = state.delivered + 1
		return function()
			state.rolledBack = state.rolledBack + 1
		end
	end
	local kv = { get = noop, set = noop }
	local player = {
		kv = function()
			return kv
		end,
		getVocation = function()
			return {
				getId = function()
					return 1
				end,
			}
		end,
		canPayForOffer = function()
			return true
		end,
		makeCoinTransaction = function()
			return paymentSucceeds
		end,
		showInfoModal = function(_, title)
			state.modal = title
		end,
		createTransactionSummary = function()
			state.summaries = state.summaries + 1
		end,
		updateUIExhausted = noop,
		sendTextMessage = noop,
	}
	Player = function()
		return player
	end
	local msg = {
		getU32 = function()
			return offer.id
		end,
		getByte = function()
			return 0
		end,
		getString = function()
			return ""
		end,
	}
	parsing.parseBuyStoreOffer(1, msg)
	return state
end

local failed = purchase(false)
check(failed.delivered == 1, "the kits are delivered before the payment")
check(failed.rolledBack == 1, "a failed payment takes the delivered kits back")
check(failed.summaries == 0, "a failed payment records no purchase summary")
check(failed.modal == "Error", "a failed payment tells the player")

local paid = purchase(true)
check(paid.rolledBack == 0, "a paid purchase keeps the kits")
check(paid.summaries == 1, "a paid purchase records its summary")

print(failures == 0 and "house payment checks passed" or (failures .. " house payment checks failed"))
os.exit(failures == 0 and 0 or 1)
