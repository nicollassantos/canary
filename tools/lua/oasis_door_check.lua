-- Stubbed check of the Ancient Tombs oasis door (issue opentibiabr/canary#3453), no server needed.
--
--   luajit tools/lua/oasis_door_check.lua <repo root>
--   lua5.4 tools/lua/oasis_door_check.lua <repo root>
--
-- 1. The startup tables must give action id 12107 to the three levers and to the door.
-- 2. A player who finds the carrot must walk through the door the lever opened: the door keeps
--    action id 12107, and closing_door.lua only lets a player through an open quest door with that
--    storage.
-- Exits 0 when both pass. Against the upstream files both fail: a second [12107] entry drops the
-- levers, and the lever never gives the storage, so the player is teleported back.
local root = arg[1] or "."
local datapack = root .. "/data-otservbr-global"

local function loadInto(path, env)
	local chunk, err
	if setfenv then
		chunk, err = loadfile(path)
		if chunk then
			setfenv(chunk, env)
		end
	else
		chunk, err = loadfile(path, "t", env)
	end
	assert(chunk, err)
	return chunk()
end

local failures = 0
local function check(ok, message)
	print((ok and "ok   " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

local function key(pos)
	return pos.x .. "," .. pos.y .. "," .. pos.z
end

-- A tiny map: one item per tile is enough here.
local map = {}
local function makeItem(id, pos)
	local item = { itemid = id, actionid = 0, pos = pos }
	function item:getId()
		return self.itemid
	end
	function item:transform(newId)
		self.itemid = newId
	end
	function item:setAttribute(_, value)
		self.actionid = value
	end
	function item:remove()
		map[key(self.pos)] = nil
	end
	return item
end

local env = setmetatable({}, { __index = _G })
env.ITEM_ATTRIBUTE_ACTIONID = 1
env.MESSAGE_EVENT_ADVANCE = 1
env.CONST_ME_MAGIC_GREEN = 1
env.CONST_ME_POFF = 2
env.COMBAT_PHYSICALDAMAGE = 1
-- The stub map only has the oasis tiles, so the loader complains about every other entry.
env.logger = { error = function() end, warn = function() end }
env.table = setmetatable({
	contains = function(list, value)
		for _, v in pairs(list) do
			if v == value then
				return true
			end
		end
		return false
	end,
}, { __index = table })
env.Position = function(x, y, z)
	local pos = { x = x, y = y, z = z }
	function pos:sendMagicEffect() end
	function pos:toString()
		return key(self)
	end
	return pos
end
env.Tile = function(pos)
	local tile = {}
	function tile:getItemById(id)
		local item = map[key(pos)]
		return item and item.itemid == id and item or nil
	end
	function tile:getItemCountById(id)
		return tile:getItemById(id) and 1 or 0
	end
	function tile:getTopDownItem()
		return map[key(pos)]
	end
	function tile:getTopTopItem()
		return nil
	end
	function tile:getGround()
		return nil
	end
	function tile:getPosition()
		return env.Position(pos.x, pos.y, pos.z)
	end
	return tile
end
env.Game = {
	createItem = function(id, _, pos)
		map[key(pos)] = makeItem(id, pos)
	end,
}
env.addEvent = function() end
env.doAreaCombatHealth = function() end
env.math = setmetatable({
	random = function()
		return 1 -- always the carrot
	end,
}, { __index = math })

local actions = {}
env.Action = function()
	local action = {}
	function action:aid(id)
		actions[id] = self
	end
	function action:register() end
	return action
end
local stepIn
env.MoveEvent = function()
	local event = {}
	function event:type() end
	function event:id() end
	function event:register()
		if self.onStepIn then
			stepIn = self.onStepIn
		end
	end
	return event
end

local levers = { env.Position(33118, 32761, 14), env.Position(33118, 32762, 14), env.Position(33118, 32763, 14) }
local doorPos = env.Position(33122, 32765, 14)
for _, pos in ipairs(levers) do
	map[key(pos)] = makeItem(2772, pos)
end
map[key(doorPos)] = makeItem(1662, doorPos)

-- 1. Startup tables.
loadInto(datapack .. "/startup/others/functions.lua", env)
loadInto(datapack .. "/startup/tables/item.lua", env)
env.loadLuaMapAction(env.ItemAction)
for _, pos in ipairs(levers) do
	check(map[key(pos)].actionid == 12107, "lever at " .. key(pos) .. " gets action id 12107")
end
check(map[key(doorPos)].actionid == 12107, "door at " .. key(doorPos) .. " gets action id 12107")

-- 2. Lever, then the step onto the opened door.
loadInto(root .. "/data/libs/tables/doors.lua", env)
loadInto(datapack .. "/scripts/quests/the_ancient_tombs/actions_oasis_lever_door.lua", env)
loadInto(root .. "/data/scripts/movements/closing_door.lua", env)

local storages = {}
local teleported = false
local player = {
	getStorageValue = function(_, k)
		return storages[k] or -1
	end,
	setStorageValue = function(_, k, v)
		storages[k] = v
	end,
	sendTextMessage = function() end,
	getPosition = function()
		return env.Position(33121, 32765, 14)
	end,
	teleportTo = function()
		teleported = true
	end,
}
player.getPlayer = function()
	return player
end

local lever = map[key(levers[1])]
local action = actions[12107]
check(action ~= nil, "lever action registered for action id 12107")
action.onUse(player, lever, levers[1], nil, levers[1], false)
local door = map[key(doorPos)]
check(door.itemid == 1663, "the lever opens the door")
local passed = stepIn(player, door, doorPos, env.Position(33122, 32764, 14))
check(passed ~= false and not teleported, "the player who found the carrot walks through the open door")

print(failures == 0 and "all checks passed" or (failures .. " checks failed"))
os.exit(failures == 0 and 0 or 1)
