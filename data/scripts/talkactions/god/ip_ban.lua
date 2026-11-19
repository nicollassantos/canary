local ipBanDays = 7

local ipBan = TalkAction("/ipban")

-- Accounts behind an address: the last address of offline characters plus whoever is online on it.
-- Operators put many customers behind one IPv4 (CGNAT), so banning it would also ban them.
local function accountsOnAddress(ip)
	local accounts, count = {}, 0
	local resultId = db.storeQuery("SELECT DISTINCT `account_id` FROM `players` WHERE `lastip` = " .. ip)
	if resultId ~= false then
		repeat
			local accountId = Result.getNumber(resultId, "account_id")
			if not accounts[accountId] then
				accounts[accountId] = true
				count = count + 1
			end
		until not Result.next(resultId)
		Result.free(resultId)
	end
	local online = 0
	for _, onlinePlayer in ipairs(Game.getPlayers()) do
		if onlinePlayer:getIp() == ip then
			online = online + 1
			local accountId = onlinePlayer:getAccountId()
			if not accounts[accountId] then
				accounts[accountId] = true
				count = count + 1
			end
		end
	end
	return count, online
end

function ipBan.onSay(player, words, param)
	-- create log
	logCommand(player, words, param)

	if param == "" then
		player:sendCancelMessage("Command param required: /ipban name or /ipban name,force")
		return true
	end

	local split = param:split(",")
	local name = split[1]:trim()
	local force = split[2] ~= nil and split[2]:trim():lower() == "force"

	local resultId = db.storeQuery("SELECT `name`, `lastip` FROM `players` WHERE `name` = " .. db.escapeString(name))
	if resultId == false then
		return true
	end

	local targetName = Result.getString(resultId, "name")
	local targetIp = Result.getNumber(resultId, "lastip")
	Result.free(resultId)

	local targetPlayer = Player(name)
	if targetPlayer then
		targetIp = targetPlayer:getIp()
	end

	if targetIp == 0 then
		return true
	end

	resultId = db.storeQuery("SELECT 1 FROM `ip_bans` WHERE `ip` = " .. targetIp)
	if resultId ~= false then
		player:sendTextMessage(MESSAGE_ADMINISTRATOR, targetName .. "  is already IP banned.")
		Result.free(resultId)
		return true
	end

	local accounts, online = accountsOnAddress(targetIp)
	if accounts > 1 and not force then
		player:sendTextMessage(MESSAGE_ADMINISTRATOR, string.format("%s's IP is shared by %d accounts (%d characters online on it), probably a carrier NAT: an IP ban would also ban them. Not banned. Use /ipban %s,force to ban it anyway.", targetName, accounts, online, targetName))
		return true
	end

	local timeNow = os.time()
	if not db.query("INSERT INTO `ip_bans` (`ip`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES (" .. targetIp .. ", '', " .. timeNow .. ", " .. timeNow + (ipBanDays * 86400) .. ", " .. player:getGuid() .. ")") then
		-- Reported instead of claiming success (the old int(11) column refused every address whose last octet was 128+).
		player:sendTextMessage(MESSAGE_ADMINISTRATOR, "Could not IP ban " .. targetName .. ": the ban was not saved (see the server log).")
		return true
	end
	logger.info("[/ipban] {} banned the IP of {} ({} accounts, {} online on it{})", player:getName(), targetName, accounts, online, force and ", forced" or "")
	if targetPlayer then
		targetPlayer:remove()
	end
	player:sendTextMessage(MESSAGE_ADMINISTRATOR, targetName .. "  has been IP banned.")
	return true
end

ipBan:separator(" ")
ipBan:groupType("god")
ipBan:register()
