-- /ipban does not ban an address shared by several accounts without "force" (Phase 28.1).
-- Run from the repository root: luajit tests/lua/test_ipban_shared_address.lua
--
-- Carriers put many customers behind one IPv4 (CGNAT); banning it bans all of them. The talkaction
-- now counts the accounts on the address and asks for "/ipban name,force" when there is more than
-- one. It also reports a failed INSERT instead of claiming success, and only kicks the target when
-- the ban is saved. The real script runs here; only the engine (db, Result, players) is stubbed.
dofile("data/libs/functions/string.lua")

MESSAGE_ADMINISTRATOR = 1
logCommand = function() end
logger = { info = function() end }

local registered
TalkAction = function()
	local action = {}
	function action:separator() end
	function action:groupType() end
	function action:register()
		registered = self
	end
	return action
end

-- Fake database: players (name, lastip, account), ip_bans, and a result cursor API.
local state
local results = {}
local function newResult(rows)
	if #rows == 0 then
		return false
	end
	table.insert(results, { rows = rows, at = 1 })
	return #results
end
Result = {
	getString = function(id, column)
		return tostring(results[id].rows[results[id].at][column])
	end,
	getNumber = function(id, column)
		return tonumber(results[id].rows[results[id].at][column])
	end,
	next = function(id)
		local result = results[id]
		result.at = result.at + 1
		return result.at <= #result.rows
	end,
	free = function() end,
}
db = {
	escapeString = function(s)
		return "'" .. s .. "'"
	end,
	storeQuery = function(query)
		local name = query:match("WHERE `name` = '(.-)'")
		if name then
			for _, p in ipairs(state.players) do
				if p.name == name then
					return newResult({ { name = p.name, lastip = p.lastip } })
				end
			end
			return false
		end
		local banned = query:match("FROM `ip_bans` WHERE `ip` = (%d+)")
		if banned then
			return state.bans[tonumber(banned)] and newResult({ { one = 1 } }) or false
		end
		local lastip = query:match("WHERE `lastip` = (%d+)")
		if lastip then
			local rows, seen = {}, {}
			for _, p in ipairs(state.players) do
				if p.lastip == tonumber(lastip) and not seen[p.account] then
					seen[p.account] = true
					table.insert(rows, { account_id = p.account })
				end
			end
			return newResult(rows)
		end
		error("unexpected query: " .. query)
	end,
	query = function(query)
		table.insert(state.inserts, query)
		return state.insertWorks
	end,
}

local function onlinePlayer(name, ip, account)
	local p = { name = name, ip = ip, account = account, removed = false }
	function p.getIp()
		return p.ip
	end
	function p.getAccountId()
		return p.account
	end
	function p.remove()
		p.removed = true
	end
	return p
end
Player = function(name)
	for _, p in ipairs(state.online) do
		if p.name == name then
			return p
		end
	end
	return nil
end
Game = {
	getPlayers = function()
		return state.online
	end,
}

dofile("data/scripts/talkactions/god/ip_ban.lua")
assert(registered, "the talkaction did not register")

local gm = {
	messages = {},
	sendTextMessage = function(self, _, text)
		table.insert(self.messages, text)
	end,
	sendCancelMessage = function(self, text)
		table.insert(self.messages, text)
	end,
	getGuid = function()
		return 1
	end,
	getName = function()
		return "GM"
	end,
}

local failures = 0
local function check(ok, message)
	print((ok and "PASS " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

-- 200.150.10.200 in network order: above a signed int(11).
local HIGH = 3356137160
local function reset(players, online, insertWorks)
	state = { players = players, online = online or {}, bans = {}, inserts = {}, insertWorks = insertWorks ~= false }
	gm.messages = {}
end

-- 1. An address of one account: banned as before, the target is kicked.
do
	local target = onlinePlayer("Cheater", HIGH, 10)
	reset({ { name = "Cheater", lastip = HIGH, account = 10 } }, { target })
	registered.onSay(gm, "/ipban", "Cheater")
	check(#state.inserts == 1 and state.inserts[1]:find(tostring(HIGH), 1, true) ~= nil, "a single-account address is banned with its full value")
	check(target.removed, "the banned target is kicked")
end

-- 2. An address shared by three accounts: refused, nothing saved, nobody kicked.
do
	local target = onlinePlayer("Cheater", HIGH, 10)
	reset({ { name = "Cheater", lastip = HIGH, account = 10 }, { name = "Neighbour", lastip = HIGH, account = 11 }, { name = "Other", lastip = HIGH, account = 12 } }, { target })
	registered.onSay(gm, "/ipban", "Cheater")
	check(#state.inserts == 0, "a shared address is not banned without force")
	check(not target.removed, "the target is not kicked when nothing is banned")
	check(gm.messages[1] ~= nil and gm.messages[1]:find("3 accounts", 1, true) ~= nil, "the GM sees how many accounts share it: " .. tostring(gm.messages[1]))
end

-- 3. The same with force: banned.
do
	local target = onlinePlayer("Cheater", HIGH, 10)
	reset({ { name = "Cheater", lastip = HIGH, account = 10 }, { name = "Neighbour", lastip = HIGH, account = 11 } }, { target })
	registered.onSay(gm, "/ipban", "Cheater, force")
	check(#state.inserts == 1 and target.removed, "force bans a shared address")
end

-- 4. Someone else online on the address counts, even with a different last address saved.
do
	local target = onlinePlayer("Cheater", HIGH, 10)
	local neighbour = onlinePlayer("Neighbour", HIGH, 11)
	reset({ { name = "Cheater", lastip = 999, account = 10 }, { name = "Neighbour", lastip = 999, account = 11 } }, { target, neighbour })
	registered.onSay(gm, "/ipban", "Cheater")
	check(#state.inserts == 0 and not neighbour.removed, "an online neighbour on the address blocks the ban")
end

-- 5. A failed INSERT is reported, and nobody is kicked.
do
	local target = onlinePlayer("Cheater", HIGH, 10)
	reset({ { name = "Cheater", lastip = HIGH, account = 10 } }, { target }, false)
	registered.onSay(gm, "/ipban", "Cheater")
	check(gm.messages[1] ~= nil and gm.messages[1]:find("Could not IP ban", 1, true) ~= nil, "a failed save is reported: " .. tostring(gm.messages[1]))
	check(not target.removed, "the target is not kicked when the ban was not saved")
end

print(failures == 0 and "ipban checks passed" or (failures .. " ipban checks failed"))
os.exit(failures == 0 and 0 or 1)
