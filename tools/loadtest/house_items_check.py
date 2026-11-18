#!/usr/bin/env python3
"""Live check that items in an owned house survive a restart (tile_store, IHouseRepository).

Houses without an owner skip their stored items on startup, so a plain restart proves nothing for the
items players keep in their houses. This gives house 2689 an owner (the first bot of
build/loadtest/characters.json) and adds a sword to one of its stored tiles; after a restart the
saved blob of that tile must still hold the sword: it was loaded into the house and saved back.

    python3 -I tools/loadtest/house_items_check.py setup     # server stopped
    (start the server, wait until online, stop it)
    python3 -I tools/loadtest/house_items_check.py verify    # exit code 0 when the sword is back
    python3 -I tools/loadtest/house_items_check.py restore   # every blob and the house as before

Uses the database of config.lua through `docker exec mysql`, like relog_race_check.py; the backup
goes to build/loadtest/house_items_backup.json.
"""
import json, os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from relog_race_check import query  # noqa: E402

HOUSE = 2689
SWORD = 3264
BACKUP = os.path.join(ROOT, "build", "loadtest", "house_items_backup.json")
manifest = json.load(open(os.path.join(ROOT, "build", "loadtest", "characters.json")))
bot = next(a for a in manifest["accounts"] if a["role"] == "bot")
name = next(c for c in bot["characters"] if c["place"].startswith("temple:"))["name"].replace("'", "''")
owner = int(query(f"SELECT `id` FROM `players` WHERE `name` = '{name}'"))

def blobs(house):
    rows = query(f"SELECT HEX(data) FROM tile_store WHERE house_id = {house}")
    return [bytes.fromhex(r) for r in rows.splitlines() if r]

def describe(blob):
    x, y, z, count = struct.unpack_from("<HHBI", blob, 0)
    return (x, y, z), count

action = sys.argv[1]
if action == "setup":
    original = blobs(HOUSE)
    json.dump({"tile_store": query("SELECT house_id, HEX(data) FROM tile_store"), "house": query(f"SELECT owner, paid FROM houses WHERE id = {HOUSE}")}, open(BACKUP, "w"))
    target = original[0]
    position, count = describe(target)
    changed = target[:5] + struct.pack("<I", count + 1) + target[9:] + struct.pack("<H", SWORD) + b"\x00"
    query(f"UPDATE tile_store SET data = UNHEX('{changed.hex()}') WHERE house_id = {HOUSE} AND data = UNHEX('{target.hex()}') LIMIT 1")
    query(f"UPDATE houses SET owner = {owner}, state = 2 WHERE id = {HOUSE}")
    print(f"house {HOUSE}: owner {owner}, tile {position} items {count} -> {count + 1} (sword added)")
elif action == "verify":
    found = []
    for blob in blobs(HOUSE):
        position, count = describe(blob)
        found.append((position, count, struct.pack("<H", SWORD) + b"\x00" in blob[9:]))
    print("after restart:", found)
    ok = any(has for _, _, has in found)
    print("OK: the sword was loaded into the owned house and saved back" if ok else "FAIL: the sword is gone")
    sys.exit(0 if ok else 1)
elif action == "restore":
    backup = json.load(open(BACKUP))
    query("DELETE FROM tile_store")
    rows = [line.split("\t") for line in backup["tile_store"].splitlines() if line]
    for i in range(0, len(rows), 200):
        values = ",".join(f"({h}, UNHEX('{d}'))" for h, d in rows[i:i + 200])
        query(f"INSERT INTO tile_store (house_id, data) VALUES {values}")
    owner_before, paid_before = backup["house"].split("\t")
    query(f"UPDATE houses SET owner = {owner_before}, paid = {paid_before}, state = 0 WHERE id = {HOUSE}")
    print(f"restored {len(rows)} blobs and house {HOUSE}")
