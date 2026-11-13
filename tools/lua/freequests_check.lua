-- Stubbed check of the free quests list against the quest log catalog (no server needed).
--
--   luajit tools/lua/freequests_check.lua data-otservbr-global
--   lua5.4 tools/lua/freequests_check.lua data-otservbr-global
--
-- Runs freequests.lua on a fake player and reads the storages it leaves. Every mission whose
-- storage the list sets must show in the quest log: its value has to sit inside
-- [startValue, endValue] (unless the mission sets ignoreendvalue), and its quest has to be started
-- (startStorageId >= startStorageValue). Exits 1 and lists each mission that would stay hidden.
-- Against the upstream list it reports 5: The Secret Library and Factions never start (no start
-- storage), Barbarian Test missions 2 and 3 (value 3, endValue 2) and Grave Danger (14 for a
-- 1..2 mission), issue opentibiabr/canary#3478.
local datapack = arg[1] or "data-otservbr-global"

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

-- Globals the datapack files touch while loading.
local env = setmetatable({}, { __index = _G })
env.configKeys = { FREE_QUEST_STAGE = 1, TOGGLE_FREE_QUEST = 2 }
env.configManager = {
	getNumber = function()
		return 1
	end,
	getBoolean = function()
		return true
	end,
}
env.logger = {
	warn = function(message)
		print("warn: " .. tostring(message))
	end,
}
env.MESSAGE_LOOK = 1

local pending = {}
env.addEvent = function(callback, _, ...)
	pending[#pending + 1] = { callback = callback, args = { ... } }
end

local storages = {}
local player = {
	getId = function()
		return 1
	end,
	getStorageValue = function(_, key)
		return storages[key] or -1
	end,
	setStorageValue = function(_, key, value)
		storages[key] = value
	end,
	sendTextMessage = function() end,
	addOutfit = function() end,
}
env.Player = function()
	return player
end

local freeQuestsEvent
env.CreatureEvent = function()
	freeQuestsEvent = { register = function() end }
	return freeQuestsEvent
end

loadInto(datapack .. "/lib/core/storages.lua", env)
loadInto(datapack .. "/scripts/creaturescripts/customs/freequests.lua", env)
freeQuestsEvent.onLogin(player)
while #pending > 0 do
	local event = table.remove(pending, 1)
	event.callback((table.unpack or unpack)(event.args))
end

-- Read the module list from the catalog index instead of guessing file names.
local index = io.open(datapack .. "/lib/core/quests/catalog/init.lua"):read("*a")
local quests = {}
for moduleName in index:gmatch('"(%d%d%d_[%w_]+)"') do
	quests[#quests + 1] = loadInto(datapack .. "/lib/core/quests/catalog/" .. moduleName .. ".lua", env)
end
assert(#quests > 0, "no quest modules found")

local problems = 0
local function report(fmt, ...)
	problems = problems + 1
	print(string.format(fmt, ...))
end

for _, quest in ipairs(quests) do
	local touched = false
	-- A questline often reuses one storage for several missions, one range per step; the value is
	-- fine when at least one mission on that storage shows it.
	local byStorage = {}
	for _, mission in pairs(quest.missions or {}) do
		local value = mission.storageId and storages[mission.storageId]
		if value then
			touched = true
			local entry = byStorage[mission.storageId] or { value = value, shown = false, ranges = {} }
			byStorage[mission.storageId] = entry
			local tooHigh = not mission.ignoreendvalue and value > mission.endValue
			if value >= mission.startValue and not tooHigh then
				entry.shown = true
			end
			entry.ranges[#entry.ranges + 1] = string.format("%s %d..%d", mission.name, mission.startValue, mission.endValue)
		end
	end
	for storage, entry in pairs(byStorage) do
		if not entry.shown then
			table.sort(entry.ranges)
			report("%s: free quests set storage %s to %d, no mission shows it (%s)", quest.name, tostring(storage), entry.value, table.concat(entry.ranges, "; "))
		end
	end
	if touched and quest.startStorageId then
		local started = storages[quest.startStorageId] or -1
		if started == -1 or started < quest.startStorageValue then
			report("%s: missions are set but the quest never starts (start storage %s is %d, needs %d)", quest.name, tostring(quest.startStorageId), started, quest.startStorageValue)
		end
	end
end

local count = 0
for _ in pairs(storages) do
	count = count + 1
end
print(string.format("%d quests, %d storages set by free quests, %d problems", #quests, count, problems))
os.exit(problems == 0 and 0 or 1)
