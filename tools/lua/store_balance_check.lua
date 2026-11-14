-- Stubbed check of the coin balance the store sends when coins are spent (issue
-- opentibiabr/canary#3742), no server needed.
--
--   luajit tools/lua/store_balance_check.lua data/libs/gamestore/player.lua
--   lua5.4 tools/lua/store_balance_check.lua data/libs/gamestore/player.lua
--
-- Spending coins must send the balance once, after the coins leave. Against the upstream file the
-- client gets the balance from before the purchase (it was sent before the removal).
local playerPath = arg[1] or "data/libs/gamestore/player.lua"

local sent = {}
local fake
package.preload["gamestore.senders"] = function()
	return {
		sendStoreBalanceUpdating = function()
			sent[#sent + 1] = { coins = fake.coins, transferable = fake.transferable }
		end,
	}
end
package.preload["gamestore.taskboard"] = function()
	return {}
end
GameStore = { OfferTypes = { OFFER_TYPE_EXPBOOST = 1 }, CoinType = { Coin = 1, Transferable = 2 }, HistoryTypes = { HISTORY_TYPE_NONE = 0 } }
GameStore.insertHistory = function() end

local store = assert(loadfile(playerPath))()

local function newPlayer(coins, transferable)
	local p = { coins = coins, transferable = transferable }
	function p:getId()
		return 1
	end
	function p:getAccountId()
		return 1
	end
	function p:getTibiaCoins()
		return self.coins
	end
	function p:getTransferableCoins()
		return self.transferable
	end
	function p:removeTibiaCoins(n)
		self.coins = self.coins - n
		return true
	end
	function p:removeTransferableCoins(n)
		self.transferable = self.transferable - n
		return true
	end
	function p:removeTransferableAndTibiaCoins(n)
		local fromTransferable = math.min(n, self.transferable)
		self.transferable = self.transferable - fromTransferable
		self.coins = self.coins - (n - fromTransferable)
		return true
	end
	for name, fn in pairs(store) do
		p[name] = fn
	end
	return p
end

local failures = 0
local function check(ok, message)
	print((ok and "PASS " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

local function spend(label, coinType, price, coins, transferable, expectCoins, expectTransferable)
	sent = {}
	fake = newPlayer(coins, transferable)
	local ok = fake:makeCoinTransaction({ name = label, price = price, coinType = coinType, type = 0 })
	check(ok, label .. ": purchase succeeds")
	check(#sent == 1, label .. ": balance sent once (" .. #sent .. ")")
	local last = sent[#sent] or {}
	check(last.coins == expectCoins and last.transferable == expectTransferable, string.format("%s: client sees %s/%s, expected %d/%d", label, tostring(last.coins), tostring(last.transferable), expectCoins, expectTransferable))
end

spend("regular coins", GameStore.CoinType.Coin, 30, 100, 0, 70, 0)
spend("transferable offer", GameStore.CoinType.Transferable, 30, 100, 20, 90, 0)

sent = {}
fake = newPlayer(0, 50)
check(fake:removeTransferableCoinsBalance(20), "coin transfer removes the coins")
check(#sent == 1 and sent[1].transferable == 30, "coin transfer sends the balance after the removal")

print(failures == 0 and "all checks passed" or (failures .. " checks failed"))
os.exit(failures == 0 and 0 or 1)
