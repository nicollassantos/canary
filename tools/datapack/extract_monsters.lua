-- Loads monster definition files with stubs and prints one JSON object per monster.
--
--   find data-otservbr-global/monster -name '*.lua' | luajit tools/datapack/extract_monsters.lua
--
-- Reads file paths from stdin. Unknown globals (constants, helpers, storages) resolve to a proxy
-- that remembers its name, so constants such as COMBAT_FIREDAMAGE come out as strings and calls
-- to helpers the files use (RegisterPrimalPackBeast, ...) do nothing. Files that fail to load are
-- reported on stderr as `ERROR <path>: <message>` and skipped.

local Any
local anyMeta = {}
local function makeAny(name)
	return setmetatable({ __anyName = name }, anyMeta)
end
anyMeta.__index = function(self, key)
	return makeAny(rawget(self, "__anyName") .. "." .. tostring(key))
end
anyMeta.__newindex = function() end
anyMeta.__call = function(self)
	return makeAny(rawget(self, "__anyName") .. "()")
end
anyMeta.__tostring = function(self)
	return rawget(self, "__anyName")
end
anyMeta.__concat = function(a, b)
	return tostring(a) .. tostring(b)
end
-- Some files compute intervals from quest settings; the values are irrelevant to the report.
local function zero()
	return 0
end
for _, op in ipairs({ "__add", "__sub", "__mul", "__div", "__mod", "__pow", "__unm" }) do
	anyMeta[op] = zero
end
anyMeta.__lt = function()
	return false
end
anyMeta.__le = function()
	return false
end
Any = makeAny

local registered = {}
local currentFile

local function newMonsterType(name)
	local mType = { __name = name }
	function mType:register(monster)
		table.insert(registered, { file = currentFile, name = name, monster = monster })
	end
	return mType
end

local function makeEnv()
	local env = {
		Game = { createMonsterType = function(name) return newMonsterType(name) end },
		string = string, table = table, math = math, pairs = pairs, ipairs = ipairs,
		type = type, tostring = tostring, tonumber = tonumber, next = next, select = select,
		unpack = unpack or table.unpack, print = function() end,
	}
	return setmetatable(env, {
		__index = function(_, key)
			return Any(key)
		end,
	})
end

-- Minimal JSON encoder: tables with only 1..n keys become arrays.
local function encode(value, out)
	local kind = type(value)
	if kind == "nil" then
		out[#out + 1] = "null"
	elseif kind == "boolean" then
		out[#out + 1] = value and "true" or "false"
	elseif kind == "number" then
		if value ~= value or value == math.huge or value == -math.huge then
			out[#out + 1] = "null"
		elseif math.floor(value) == value and math.abs(value) < 2 ^ 53 then
			out[#out + 1] = string.format("%d", value)
		else
			out[#out + 1] = string.format("%.17g", value)
		end
	elseif kind == "string" then
		out[#out + 1] = '"' .. value:gsub('[%c"\\]', function(c)
			local map = { ['"'] = '\\"', ["\\"] = "\\\\", ["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t" }
			return map[c] or string.format("\\u%04x", c:byte())
		end) .. '"'
	elseif kind == "table" then
		if getmetatable(value) == anyMeta then
			return encode(tostring(value), out)
		end
		local count = 0
		for _ in pairs(value) do
			count = count + 1
		end
		if count == #value then
			out[#out + 1] = "["
			for i = 1, #value do
				if i > 1 then
					out[#out + 1] = ","
				end
				encode(value[i], out)
			end
			out[#out + 1] = "]"
		else
			out[#out + 1] = "{"
			local first = true
			for key, item in pairs(value) do
				if type(item) ~= "function" then
					if not first then
						out[#out + 1] = ","
					end
					first = false
					encode(tostring(key), out)
					out[#out + 1] = ":"
					encode(item, out)
				end
			end
			out[#out + 1] = "}"
		end
	else
		out[#out + 1] = "null"
	end
end

-- Only the fields the report compares; attacks and voices stay out.
local function summarize(entry)
	local m = entry.monster or {}
	local defenses = type(m.defenses) == "table" and m.defenses or {}
	return {
		file = entry.file,
		name = entry.name,
		experience = m.experience,
		health = m.health,
		maxHealth = m.maxHealth,
		raceId = m.raceId,
		bestiary = type(m.Bestiary) == "table" and m.Bestiary.class or nil,
		bosstiary = type(m.bosstiary) == "table" and true or nil,
		rewardBoss = type(m.flags) == "table" and m.flags.rewardBoss == true or nil,
		armor = defenses.armor,
		mitigation = defenses.mitigation,
		elements = m.elements,
		immunities = m.immunities,
		loot = m.loot,
	}
end

for path in io.lines() do
	currentFile = path
	local chunk, err = loadfile(path)
	if not chunk then
		io.stderr:write("ERROR " .. path .. ": " .. tostring(err) .. "\n")
	else
		if setfenv then
			setfenv(chunk, makeEnv())
		end
		local ok, runErr = pcall(chunk)
		if not ok then
			io.stderr:write("ERROR " .. path .. ": " .. tostring(runErr) .. "\n")
		end
	end
end

for _, entry in ipairs(registered) do
	local out = {}
	encode(summarize(entry), out)
	io.write(table.concat(out), "\n")
end
