-- House offer delivery is all or nothing (upstream PR #4184, bug fixes only).
-- Run from the repository root: luajit tests/lua/test_gamestore_house_delivery.lua
--
-- processHouseRelatedPurchase used to call canReceiveStoreItems(#itemIds): the item count went in
-- as the item id, so it checked 1 slot while carpet offers deliver 5 kits; a full inbox then took
-- only part of the kits and the purchase was still charged in full. Kits created before a failure
-- stayed in the inbox. Catalog, purchase logic and the inbox capacity check are the real ones.
package.path = "data/libs/?.lua;" .. package.path
GameStore = require("gamestore.constants")
package.loaded["gamestore.senders"] = {}
package.loaded["gamestore.taskboard"] = {}
local purchases = require("gamestore.purchases")
local storePlayer = require("gamestore.player")

ITEM_HEALTH_CASK_START, ITEM_HEALTH_CASK_END = 25879, 25883
ITEM_MANA_CASK_START, ITEM_MANA_CASK_END = 25889, 25893
ITEM_SPIRIT_CASK_START, ITEM_SPIRIT_CASK_END = 25899, 25902
ITEM_DECORATION_KIT = 23398
ITEM_ATTRIBUTE_DESCRIPTION, ITEM_ATTRIBUTE_STORE, ITEM_ATTRIBUTE_DATE = "description", "store", "date"
function systemTime()
	return 1
end
function BatchUpdate()
	return { add = function() end, delete = function() end }
end
function ItemType(id)
	return {
		getName = function()
			return "item " .. id
		end,
		isStackable = function()
			return false
		end,
		getStackSize = function()
			return 1
		end,
		getWeight = function(_, count)
			return 10 * (count or 1)
		end,
	}
end

local failures = 0
local function check(ok, message)
	print((ok and "PASS " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

-- An inbox with a real capacity: addItem returns nil when it is full, like the engine.
local function newInbox(maxCapacity, occupied, failOnCall)
	local inbox = { items = {}, calls = 0, removed = 0 }
	for i = 1, occupied do
		inbox.items[i] = { preexisting = true }
	end
	function inbox:getItems()
		return self.items
	end
	function inbox:getMaxCapacity()
		return maxCapacity
	end
	function inbox:addItem(id, count)
		assert(id == ITEM_DECORATION_KIT and count == 1)
		self.calls = self.calls + 1
		if #self.items >= maxCapacity or self.calls == failOnCall then
			return nil
		end
		local kit = { attributes = {}, custom = {} }
		function kit.setAttribute(k, key, value)
			k.attributes[key] = value
		end
		function kit.setCustomAttribute(k, key, value)
			k.custom[key] = value
		end
		function kit.remove(k)
			for i, item in ipairs(inbox.items) do
				if item == k then
					table.remove(inbox.items, i)
					inbox.removed = inbox.removed + 1
					return true
				end
			end
			return false
		end
		table.insert(self.items, kit)
		return kit
	end
	return inbox
end

local function newPlayer(inbox)
	local player = {}
	function player:getStoreInbox()
		return inbox
	end
	function player:getFreeCapacity()
		return 1000000
	end
	player.canReceiveStoreItems = storePlayer.canReceiveStoreItems
	return player
end

local function kitsIn(inbox)
	local count = 0
	for _, item in ipairs(inbox.items) do
		if not item.preexisting then
			count = count + 1
		end
	end
	return count
end

local function findOffer(name, count)
	for _, path in ipairs({ "house_decorations", "house_furniture", "consumables_casks", "beds" }) do
		local category = dofile("data/modules/scripts/gamestore/catalog/" .. path .. ".lua")
		for _, offer in ipairs(category.offers) do
			if offer.name == name and (count == nil or offer.count == count) then
				return offer
			end
		end
	end
	error("offer not found: " .. name)
end

local carpet = findOffer("Azure Carpet", 5)
assert(carpet.count == 5, "the carpet offer delivers 5 kits")

-- 1. Three free slots for a five-kit offer: refused before anything is created.
do
	local inbox = newInbox(10, 7)
	local ok, err = pcall(purchases.processHouseRelatedPurchase, newPlayer(inbox), carpet)
	check(not ok and type(err) == "table" and err.code == 0, "5 kits with 3 free slots is refused as a handled error")
	check(kitsIn(inbox) == 0, "nothing is delivered when the kits do not fit (" .. kitsIn(inbox) .. " kits)")
end

-- 2. A kit that fails in the middle: the kits already created are removed.
do
	local inbox = newInbox(100, 0, 3)
	local ok, err = pcall(purchases.processHouseRelatedPurchase, newPlayer(inbox), carpet)
	check(not ok and type(err) == "table" and err.code == 0, "a kit that cannot be created fails the purchase")
	check(kitsIn(inbox) == 0, "the kits created before the failure are removed (" .. kitsIn(inbox) .. " left)")
end

-- 3. Normal purchase: every kit delivered, and the returned rollback takes them back (payment failure).
do
	local inbox = newInbox(100, 0)
	local ok, rollback = pcall(purchases.processHouseRelatedPurchase, newPlayer(inbox), carpet)
	check(ok and kitsIn(inbox) == 5, "a purchase with room delivers the 5 kits")
	check(type(rollback) == "function", "the delivery returns a rollback for a failed payment")
	if type(rollback) == "function" then
		rollback()
		check(kitsIn(inbox) == 0, "the rollback removes every delivered kit")
	end
end

-- 4. A cask offer counts as one kit (its charges go into the kit), so one free slot is enough.
do
	local cask = findOffer("Health Cask")
	local inbox = newInbox(10, 9)
	local ok = pcall(purchases.processHouseRelatedPurchase, newPlayer(inbox), cask)
	check(ok and kitsIn(inbox) == 1, "a cask offer needs one slot and delivers one kit")
end

-- 5. A bed delivers one kit per half: one free slot is not enough, two are.
do
	local bed
	local category = dofile("data/modules/scripts/gamestore/catalog/beds.lua")
	for _, offer in ipairs(category.offers) do
		if type(offer.itemtype) == "table" and #offer.itemtype == 2 then
			bed = offer
			break
		end
	end
	assert(bed, "no two-part bed offer in the catalog")
	local full = newInbox(10, 9)
	local ok = pcall(purchases.processHouseRelatedPurchase, newPlayer(full), bed)
	check(not ok and kitsIn(full) == 0, "a two-part bed with one free slot is refused, nothing delivered")
	local roomy = newInbox(10, 8)
	ok = pcall(purchases.processHouseRelatedPurchase, newPlayer(roomy), bed)
	check(ok and kitsIn(roomy) == 2, "a two-part bed with two free slots delivers both halves")
end

print(failures == 0 and "house delivery checks passed" or (failures .. " house delivery checks failed"))
os.exit(failures == 0 and 0 or 1)
