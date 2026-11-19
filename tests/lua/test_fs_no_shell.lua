-- FS.mkdir()/FS.mkdir_p() must never start a shell (upstream PR #4102).
-- Run: luajit tests/lua/test_fs_no_shell.lua
--
-- The old FS.mkdir ran os.execute('mkdir "' .. path .. '"'): a quote, backtick or $( ) in the path
-- ran a command. Against that version every case below fails; with the native binding none of
-- them reaches a shell. tests/lua/test_fs.lua (from the PR) covers the binding's contract.

local shellCommands = {}
local realExecute, realPopen = os.execute, io.popen
os.execute = function(command)
	table.insert(shellCommands, tostring(command))
	return 0
end
io.popen = function(command)
	table.insert(shellCommands, tostring(command))
	return nil
end

local created = {}
fsCreateDirectories = function(path)
	table.insert(created, path)
	return true, nil
end

dofile("data/libs/functions/fs.lua")

local failures = 0
local function check(ok, message)
	print((ok and "PASS " or "FAIL ") .. message)
	if not ok then
		failures = failures + 1
	end
end

local names = {
	'reports/bugs/a" ; touch injected ; echo "',
	"reports/bugs/b`touch injected`",
	"reports/bugs/c$(touch injected)",
	"reports/bugs/d & e | f",
}
for _, name in ipairs(names) do
	shellCommands, created = {}, {}
	local ok = FS.mkdir_p(name)
	check(ok == true, "mkdir_p succeeds: " .. name)
	check(#shellCommands == 0, "no shell for: " .. name .. (shellCommands[1] and (" (ran: " .. shellCommands[1] .. ")") or ""))
	check(created[#created] == name, "the native binding got the path unchanged: " .. name)
end

os.execute, io.popen = realExecute, realPopen
print(failures == 0 and "all checks passed" or (failures .. " checks failed"))
os.exit(failures == 0 and 0 or 1)
