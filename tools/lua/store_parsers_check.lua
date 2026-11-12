-- Stubbed check for data/libs/gamestore/parsers.lua (no server needed).
--
--   luajit tools/lua/store_parsers_check.lua data/libs/gamestore/parsers.lua
--   lua5.4 tools/lua/store_parsers_check.lua data/libs/gamestore/parsers.lua
--
-- Exits 0 when every check passes. Against the upstream parser the search check never finishes
-- (run it under `timeout 20`): the client text was used as a Lua pattern.
-- Stubs just enough of the server API to load data/libs/gamestore/parsers.lua.
local parsersPath = arg[1]
local queries, events = {}, {}
package.preload["gamestore.helpers"] = function() return {} end
package.preload["gamestore.senders"] = function()
	local noop = function() end
	return { openStore = noop, sendShowStoreOffers = noop, sendShowStoreOffersOnOldProtocol = noop, sendStoreError = noop,
		sendStorePurchaseSuccessful = noop, sendUpdatedStoreBalances = noop, sendStoreTransactionHistory = noop, sendHomePage = noop }
end
GameStore = {
	RecivedPackets = {}, StoreErrors = { STORE_ERROR_TRANSFER = 1, STORE_ERROR_NETWORK = 2 },
	Categories = { { offers = { { name = "Ferumbras' Ascendant Hat Offer" }, { name = "Exercise Sword" } } } },
}
db = { query = function(q) table.insert(queries, q) return true end, storeQuery = function(q) table.insert(queries, q) return 7 end, escapeString = function(s) return "'" .. s .. "'" end }
Result = { getNumber = function() return 99 end }
addPlayerEvent = function(fn, delay, playerId, code, message) table.insert(events, message or "") end
local player = { getTransferableCoins = function() return 0 end, getName = function() return "Sender" end,
	getAccountId = function() return 1 end, removeTransferableCoinsBalance = function() end }
Player = function() return player end

local parsing = dofile(parsersPath)

local function check(label, cond) print((cond and "PASS " or "FAIL ") .. label) if not cond then os.exit(1) end end

-- Search is literal and cheap.
local t = os.clock()
local hits = parsing.fuzzySearchOffer(string.rep(".-", 12) .. "#")
check("pathological pattern returns fast (" .. string.format("%.4f", os.clock() - t) .. " s)", os.clock() - t < 0.05 and #hits == 0)
check("malformed pattern does not raise", pcall(parsing.fuzzySearchOffer, "[a"))
check("plain word still finds the offer", #parsing.fuzzySearchOffer("hat") == 1)
check("search is case insensitive", #parsing.fuzzySearchOffer("EXERCISE") == 1)
check("over-long search returns nothing", #parsing.fuzzySearchOffer(string.rep("a", 65)) == 0)

-- A zero transfer stops before any query.
local function msgWith(name, amount)
	return { getString = function() return name end, getU32 = function() return amount end }
end
parsing.parseTransferableCoins(1, msgWith("Receiver", 0))
check("zero coin transfer runs no query", #queries == 0 and #events == 1)
parsing.parseTransferableCoins(1, msgWith(string.rep("x", 31), 5))
check("over-long receiver runs no query", #queries == 0 and #events == 2)
