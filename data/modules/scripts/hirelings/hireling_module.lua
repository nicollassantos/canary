HirelingModule = {}

HirelingModule.Credits = {
	Developer = 'Leonardo "Leu" Pereira (jlcvp)',
	Version = "1.0",
	Date = "30/04/2020 ",
}

HirelingModule.S_Packets = { SendOutfitWindow = 0xC8 }

HirelingModule.C_Packets = { RequestChangeOutfit = 0xD2, ConfirmOutfitChange = 0xD3 }

local function getOutfit(msg)
	local outfitType = 0
	outfitType = msg:getByte()

	-- Like a player's own outfit (ProtocolGame::parseSetOutfit): colours within the 133 the client has
	-- (a larger index reaches every client that sees the hireling), at most 3 addons, and no mount
	-- (hirelings do not ride; the client's mount id was never checked).
	local outfit = {}
	outfit.lookType = msg:getU16()
	outfit.lookHead = math.min(132, msg:getByte())
	outfit.lookBody = math.min(132, msg:getByte())
	outfit.lookLegs = math.min(132, msg:getByte())
	outfit.lookFeet = math.min(132, msg:getByte())
	outfit.lookAddons = math.min(3, msg:getByte())
	outfit.lookMount = 0

	if outfitType == 0 then
		msg:getU16() -- mount
	else
		msg:getU32() --discard this for some reason maybe it's the hireling id
	end
	return outfit
end

local function parseChangeOutfit(player, msg)
	local hireling = player:getHirelingChangingOutfit()
	local outfit
	if not hireling then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		player:getPosition():sendMagicEffect(CONST_ME_POFF)

		-- deplete msg to avoid setting an hireling outfit to player
		getOutfit(msg)
	else
		outfit = getOutfit(msg)
		hireling:changeOutfit(outfit)
	end
end

function onRecvbyte(player, msg, byte)
	if byte == HirelingModule.C_Packets.ConfirmOutfitChange then
		if not player:isChangingHirelingOutfit() then
			return
		end
		parseChangeOutfit(player, msg)
	end
end
