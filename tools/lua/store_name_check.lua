-- Stubbed check for GameStore.canChangeToName in data/libs/gamestore/store.lua (no server needed).
--
--   luajit tools/lua/store_name_check.lua data/libs/gamestore/store.lua
--   lua5.4 tools/lua/store_name_check.lua data/libs/gamestore/store.lua
--
-- Exits 0 when every check passes. Against the upstream file the look-alike checks fail: the
-- name rules were a list of forbidden characters, so control bytes and non-ASCII letters passed.
local storePath = arg[1]

function string.split(str, sep)
	local parts = {}
	for part in str:gmatch("([^" .. sep .. "]+)") do
		table.insert(parts, part)
	end
	return parts
end
MonsterType = function() return nil end
Npc = function() return nil end
GameStore = { Categories = {} }

local store = dofile(storePath)

local failed = false
local function check(label, cond)
	print((cond and "PASS " or "FAIL ") .. label)
	failed = failed or not cond
end
local function allowed(name)
	return store.canChangeToName(name).ability
end

check("plain name is allowed", allowed("Bob Smith"))
check("staff word is refused", not allowed("Gm Bob"))
check("cyrillic look-alike is refused", not allowed("G\208\176memaster Bob"))
check("accented letter is refused", not allowed("\195\129damo"))
check("tab is refused", not allowed("Bob\tSmith"))
check("nul byte is refused", not allowed("Bob\0Smith"))
check("apostrophe stays refused", not allowed("Bob's"))
os.exit(failed and 1 or 0)
