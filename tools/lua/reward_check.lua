-- Stubbed check for the !reward talkaction in data/scripts/talkactions/player/reward.lua (no server
-- needed): saying !reward twice opens two windows; answering both must deliver one weapon.
--
--   luajit tools/lua/reward_check.lua data/scripts/talkactions/player/reward.lua
--   lua5.4 tools/lua/reward_check.lua data/scripts/talkactions/player/reward.lua
--
-- Exits 0 when every check passes. Against the upstream file the second window delivers a second
-- weapon: the storage was only checked when the window was opened.
local scriptPath = arg[1]

Storage = { PlayerWeaponReward = 77001 }
TOWNS_LIST = { AB_DENDRIEL = 2 }
MESSAGE_LOOK = 1
IMMOVABLE_ACTION_ID = 100
ITEM_ATTRIBUTE_STORE = 1
ITEM_ATTRIBUTE_DESCRIPTION = 2
configKeys = { TOGGLE_RECEIVE_REWARD = 1, SERVER_NAME = 2 }
configManager = {
	getBoolean = function()
		return true
	end,
	getString = function()
		return "Test"
	end,
}
systemTime = function()
	return 0
end
ItemType = function(id)
	return {
		getName = function()
			return "exercise weapon " .. id
		end,
		getWeight = function()
			return 1
		end,
	}
end

-- Minimal ModalWindow helper: keeps every window sent, with its choice callbacks.
local sentWindows = {}
ModalWindow = function()
	local window = { choices = {} }
	function window:addChoice(text, callback)
		table.insert(self.choices, { text = text, callback = callback })
	end
	function window:addButton() end
	function window:setDefaultEnterButton() end
	function window:setDefaultEscapeButton() end
	function window:sendToPlayer()
		table.insert(sentWindows, self)
	end
	return window
end

local talkaction
TalkAction = function()
	talkaction = {}
	function talkaction:separator() end
	function talkaction:groupType() end
	function talkaction:register() end
	return talkaction
end

local delivered = 0
local storage = {}
local inbox = {
	getItems = function()
		return {}
	end,
	getMaxCapacity = function()
		return 100
	end,
	addItem = function()
		delivered = delivered + 1
		return {
			setActionId = function() end,
			setAttribute = function() end,
		}
	end,
}
local player = {
	getTown = function()
		return {
			getId = function()
				return 5
			end,
		}
	end,
	getStorageValue = function(_, key)
		return storage[key] or -1
	end,
	setStorageValue = function(_, key, value)
		storage[key] = value
	end,
	getStoreInbox = function()
		return inbox
	end,
	getFreeCapacity = function()
		return 1000
	end,
	sendTextMessage = function() end,
}

dofile(scriptPath)

local failed = false
local function check(label, cond)
	print((cond and "PASS " or "FAIL ") .. label)
	failed = failed or not cond
end

talkaction.onSay(player, "!reward", "")
talkaction.onSay(player, "!reward", "")
check("two windows were opened", #sentWindows == 2)

for _, window in ipairs(sentWindows) do
	window.choices[1].callback(player, { name = "Select" }, window.choices[1])
end
check("only one weapon is delivered", delivered == 1)
check("the reward is marked as received", (storage[Storage.PlayerWeaponReward] or -1) > 0)
os.exit(failed and 1 or 0)
