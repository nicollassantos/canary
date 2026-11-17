#!/usr/bin/env python3
"""Create the load-test accounts and characters (Phase 25) in the server's database.

Two groups, so the bots never spoil the characters a person tests with:
- bots: 100 accounts x 2 characters ("Swarm ..."), used only by tools/loadtest/swarm.py; with
  maxPlayersOnlinePerAccount = 1 the swarm puts one character per account online (100 bots);
- manual: 5 accounts x 4 characters ("Tester ..."), for a session with the real client.

Levels: 8-600, most of them 8-150; the five vocations (promoted from level 20, premium accounts);
half the characters stand in a town temple, half in a hunting area whose monsters fit the level
(spawn centres from the monster spawn file, temples from the .otbm). Every character carries a
backpack with potions and a rope in the ammo slot (the swarm uses it at a distance, which makes the
server compute a path).

Running it again replaces every load-test account (email ending in @loadtest.invalid) and so
restores levels and places after a load test killed or moved the bots. Stop the server first: a
character that is online is saved over the new values when it logs out.

    python3 -I tools/loadtest/seed.py            # dry run: writes the SQL and the manifest, changes nothing
    python3 -I tools/loadtest/seed.py --apply    # replaces the load-test accounts in the database

The database and its credentials come from config.lua; the password is never printed. Logins go to
build/loadtest/characters.json (gitignored); --password sets one password for every account,
otherwise a random one is generated.
"""

import argparse
import hashlib
import json
import math
import os
import random
import re
import secrets
import socket
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET  # the repository's own spawn file (expat resolves no external entities)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EMAIL_DOMAIN = "loadtest.invalid"

# Vocation id -> (name, base vocation, gain hp, gain mana, gain cap) from data/XML/vocations.xml.
VOCATIONS = {
    1: ("Sorcerer", 1, 5, 30, 10), 2: ("Druid", 2, 5, 30, 10), 3: ("Paladin", 3, 10, 15, 20),
    4: ("Knight", 4, 15, 5, 25), 9: ("Monk", 9, 10, 10, 25),
}
PROMOTION = {1: 5, 2: 6, 3: 7, 4: 8, 9: 10}
PROMOTED_NAMES = {5: "Master Sorcerer", 6: "Elder Druid", 7: "Royal Paladin", 8: "Elite Knight", 10: "Exalted Monk"}
# Towns a vocation character can live in (no tutorial island, Rookgaard or Dawnport).
MAIN_TOWNS = {"Ab'Dendriel", "Carlin", "Kazordoon", "Thais", "Venore", "Ankrahmun", "Edron", "Farmine", "Darashia",
              "Liberty Bay", "Port Hope", "Svargrond", "Yalahar", "Krailos", "Rathleton", "Issavi"}
# Rookgaard, the Island of Destiny and Dawnport: no vocation hunts there.
NOVICE_AREA = (31900, 31700, 32180, 32320)

ITEM_BACKPACK, ITEM_HEALTH_POTION, ITEM_MANA_POTION, ITEM_ROPE = 2854, 266, 268, 3003
SLOT_BACKPACK, SLOT_AMMO = 3, 10

SYLLABLES = ["ka", "lo", "mi", "ra", "ten", "vu", "sel", "dor", "an", "bri", "cos", "fel", "gar", "hul", "ish", "jor",
             "kel", "lun", "mor", "nax", "oth", "pel", "quin", "ros", "sul", "tir", "ul", "var", "wen", "yor", "zan"]


# --- server data ---------------------------------------------------------------------------------

def read_config(path):
    text = open(path, encoding="utf-8").read()

    def value(key, default=None):
        match = re.search(rf'^{key}\s*=\s*"?([^"\n]*?)"?\s*(?:--.*)?$', text, flags=re.M)
        return match.group(1).strip() if match else default

    return {
        "user": value("mysqlUser", "root"), "password": value("mysqlPass", ""), "database": value("mysqlDatabase"),
        "game_port": int(value("gameProtocolPort", "7172")), "datapack": value("dataPackDirectory", "data-otservbr-global"),
        "max_online": int(value("maxPlayersOnlinePerAccount", "1")),
    }


def otbm_towns(path):
    """(id, name, x, y, z) of every town in the map's towns node (OTBM node type 12, towns type 13)."""
    data = open(path, "rb").read()
    start = data.rfind(b"\xfe\x0c")
    if start < 0:
        raise ValueError("no towns node in the map")

    def unescape(raw):
        out, k = bytearray(), 0
        while k < len(raw):
            if raw[k] == 0xFD:
                out.append(raw[k + 1])
                k += 2
            else:
                out.append(raw[k])
                k += 1
        return bytes(out)

    towns, pos = [], start + 2
    while data[pos] == 0xFE and data[pos + 1] == 0x0D:
        end = pos + 2
        while data[end] != 0xFF:
            end += 2 if data[end] == 0xFD else 1
        raw = unescape(data[pos + 2:end])
        town_id, length = struct.unpack_from("<IH", raw, 0)
        name = raw[6:6 + length].decode("latin-1")
        x, y, z = struct.unpack_from("<HHB", raw, 6 + length)
        towns.append((town_id, name, x, y, z))
        pos = end + 1
    return towns


def monster_experience(monster_dir):
    """Monster name (lower case) -> experience, leaving out bosses."""
    experience = {}
    for folder, _, files in os.walk(monster_dir):
        for file in files:
            if not file.endswith(".lua"):
                continue
            text = open(os.path.join(folder, file), encoding="utf-8", errors="replace").read()
            name = re.search(r'createMonsterType\("([^"]+)"', text)
            exp = re.search(r"monster\.experience\s*=\s*(\d+)", text)
            if not name or not exp or "bosstiary" in text or re.search(r"rewardBoss\s*=\s*true", text):
                continue
            experience[name.group(1).lower()] = int(exp.group(1))
    return experience


def hunting_spawns(spawn_file, experience):
    """(x, y, z, average experience, monster names) of spawns with 2+ known non-boss monsters."""
    spawns = []
    for spawn in ET.parse(spawn_file).getroot():
        x, y, z = int(spawn.get("centerx")), int(spawn.get("centery")), int(spawn.get("centerz"))
        names = [m.get("name", "").lower() for m in spawn]
        values = [experience[n] for n in names if n in experience]
        if len(values) < 2 or len(values) != len(names) or min(values) == 0:
            continue
        x0, y0, x1, y1 = NOVICE_AREA
        if x0 <= x <= x1 and y0 <= y <= y1:
            continue
        spawns.append((x, y, z, sum(values) / len(values), sorted(set(names))))
    return spawns


# --- characters ----------------------------------------------------------------------------------

def target_experience(level):
    """Experience of a monster a character of `level` usually hunts (rough rule of thumb)."""
    return 2 * level ** 1.4


def exp_for_level(level):
    return (((level - 6) * level + 17) * level - 12) // 6 * 100  # Player::getExpForLevel


def bot_levels(rng, count):
    levels = []
    for _ in range(count):
        roll = rng.random()
        if roll < 0.70:
            levels.append(int(8 + rng.random() ** 1.6 * 142))  # 8-150, most near the bottom
        elif roll < 0.95:
            levels.append(rng.randint(151, 400))
        else:
            levels.append(rng.randint(401, 600))
    return levels


def skills(rng, base, level):
    def scaled(start, rate, cap=130):
        return max(10, min(cap, int(start + level * rate)))

    melee = {"skill_sword": 10, "skill_axe": 10, "skill_club": 10, "skill_fist": 10, "skill_dist": 10, "skill_shielding": 10}
    if base == 4:
        melee[rng.choice(["skill_sword", "skill_axe", "skill_club"])] = scaled(10, 0.55)
        melee["skill_shielding"] = scaled(10, 0.5)
        maglevel = min(12, level // 20 + 2)
    elif base == 3:
        melee["skill_dist"] = scaled(10, 0.5)
        melee["skill_shielding"] = scaled(10, 0.3)
        maglevel = min(35, level // 10 + 4)
    elif base == 9:
        melee["skill_fist"] = scaled(10, 0.5)
        melee["skill_shielding"] = scaled(10, 0.3)
        maglevel = min(40, level // 10 + 3)
    else:
        maglevel = min(130, int(5 + level * 0.25))
    return maglevel, melee


def character_name(rng, prefix, used):
    while True:
        word = "".join(rng.choice(SYLLABLES) for _ in range(rng.randint(2, 3)))
        name = f"{prefix} {word.capitalize()}"
        if name not in used and len(name) <= 29:
            used.add(name)
            return name


def place_character(rng, level, in_temple, towns, spawns):
    """(place label, town id, x, y, z) for a temple of a main town or a hunting spawn fitting the level."""
    if in_temple or not spawns:
        town_id, name, x, y, z = rng.choice(towns)
        return f"temple:{name}", town_id, x, y, z
    target = target_experience(level)
    fitting = [s for s in spawns if target / 2 <= s[3] <= target * 2] or sorted(spawns, key=lambda s: abs(s[3] - target))[:20]
    x, y, z, _, names = rng.choice(fitting)
    town_id, town_name, _, _, _ = min(towns, key=lambda t: math.hypot(x - t[2], y - t[3]))
    return f"hunt:{', '.join(names[:3])} (near {town_name})", town_id, x, y, z


def make_character(rng, role, name, level, vocation_base, place):
    vocation = PROMOTION[vocation_base] if level >= 20 else vocation_base
    _, base, gain_hp, gain_mana, gain_cap = VOCATIONS[vocation_base]
    sex = rng.randint(0, 1)
    maglevel, melee = skills(rng, base, level)
    label, town_id, x, y, z = place
    return {
        "role": role, "name": name, "level": level, "vocation": vocation,
        "vocation_name": PROMOTED_NAMES.get(vocation, VOCATIONS[vocation_base][0]), "sex": sex,
        "looktype": 128 if sex == 1 else 136, "health": 185 + (level - 8) * gain_hp, "mana": 90 + (level - 8) * gain_mana,
        "cap": 470 + (level - 8) * gain_cap, "experience": exp_for_level(level), "maglevel": maglevel, "skills": melee,
        "town_id": town_id, "place": label, "position": [x, y, z], "balance": rng.randint(1000, 1_000_000),
    }


def build(seed, password):
    rng = random.Random(seed)
    config = read_config(os.path.join(ROOT, "config.lua"))
    datapack = os.path.join(ROOT, config["datapack"])
    towns = [t for t in otbm_towns(os.path.join(datapack, "world", "otservbr.otbm")) if t[1] in MAIN_TOWNS]
    spawns = hunting_spawns(os.path.join(datapack, "world", "otservbr-monster.xml"), monster_experience(os.path.join(datapack, "monster")))

    used, accounts = set(), []
    bases = [1, 2, 3, 4, 9]
    levels = bot_levels(rng, 200)
    for index in range(100):
        characters = []
        for slot in range(2):
            level = levels[index * 2 + slot]
            # One character per account goes online at a time; its partner stands in a temple, so the
            # pair never meets the "one character outside a protection zone" rule.
            place = place_character(rng, level, slot == 1, towns, spawns)
            characters.append(make_character(rng, "bot", character_name(rng, "Swarm", used), level, bases[(index * 2 + slot) % 5], place))
        accounts.append({"role": "bot", "name": f"lt_bot_{index + 1:03d}", "email": f"bot{index + 1:03d}@{EMAIL_DOMAIN}", "characters": characters})

    manual_levels = [8, 15, 30, 50, 60, 80, 100, 120, 150, 180, 200, 250, 300, 350, 400, 450, 500, 550, 600, 100]
    for index in range(5):
        characters = []
        for slot in range(4):
            n = index * 4 + slot
            place = place_character(rng, manual_levels[n], n % 2 == 0, towns, spawns)
            characters.append(make_character(rng, "manual", character_name(rng, "Tester", used), manual_levels[n], bases[n % 5], place))
        accounts.append({"role": "manual", "name": f"lt_manual_{index + 1}", "email": f"tester{index + 1}@{EMAIL_DOMAIN}", "characters": characters})

    return config, {"password": password, "seed": seed, "accounts": accounts}


# --- output --------------------------------------------------------------------------------------

def sql_string(text):
    return "'" + text.replace("\\", "\\\\").replace("'", "\\'") + "'"


def to_sql(manifest):
    password_hash = hashlib.sha1(manifest["password"].encode()).hexdigest()  # passwordType = "sha1"
    now = int(time.time())
    lines = ["SET NAMES utf8;", "START TRANSACTION;",
             f"DELETE FROM `accounts` WHERE `email` LIKE {sql_string('%@' + EMAIL_DOMAIN)};"]
    for account in manifest["accounts"]:
        lines.append(
            "INSERT INTO `accounts` (`name`, `password`, `email`, `premdays`, `lastday`, `type`, `creation`) VALUES "
            f"({sql_string(account['name'])}, {sql_string(password_hash)}, {sql_string(account['email'])}, 365, {now + 365 * 86400}, 1, {now});")
        lines.append("SET @account = LAST_INSERT_ID();")
        for c in account["characters"]:
            skill_columns = "".join(f", `{k}`" for k in c["skills"])
            skill_values = "".join(f", {v}" for v in c["skills"].values())
            x, y, z = c["position"]
            lines.append(
                "INSERT INTO `players` (`name`, `group_id`, `account_id`, `level`, `vocation`, `health`, `healthmax`, `experience`, "
                "`looktype`, `maglevel`, `mana`, `manamax`, `town_id`, `posx`, `posy`, `posz`, `conditions`, `cap`, `sex`, `balance`"
                f"{skill_columns}) VALUES ({sql_string(c['name'])}, 1, @account, {c['level']}, {c['vocation']}, {c['health']}, {c['health']}, "
                f"{c['experience']}, {c['looktype']}, {c['maglevel']}, {c['mana']}, {c['mana']}, {c['town_id']}, {x}, {y}, {z}, '', "
                f"{c['cap']}, {c['sex']}, {c['balance']}{skill_values});")
            lines.append("SET @player = LAST_INSERT_ID();")
            lines.append(
                "INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES "
                f"(@player, {SLOT_BACKPACK}, 101, {ITEM_BACKPACK}, 1, ''), (@player, 101, 102, {ITEM_HEALTH_POTION}, 50, ''), "
                f"(@player, 101, 103, {ITEM_MANA_POTION}, 50, ''), (@player, {SLOT_AMMO}, 104, {ITEM_ROPE}, 1, '');")
    lines.append("COMMIT;")
    return "\n".join(lines) + "\n"


def port_in_use(port):
    with socket.socket() as probe:
        probe.settimeout(0.5)
        return probe.connect_ex(("127.0.0.1", port)) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--apply", action="store_true", help="replace the load-test accounts in the database")
    parser.add_argument("--password", help="password of every load-test account (default: random)")
    parser.add_argument("--seed", type=int, default=25, help="random seed (same seed, same characters)")
    parser.add_argument("--container", default="mysql", help="docker container running MySQL")
    parser.add_argument("--force", action="store_true", help="apply even if the game port answers (online characters overwrite the seed on logout)")
    args = parser.parse_args()

    password = args.password or secrets.token_urlsafe(9)
    config, manifest = build(args.seed, password)
    out_dir = os.path.join(ROOT, "build", "loadtest")
    os.makedirs(out_dir, exist_ok=True)
    sql_path = os.path.join(out_dir, "seed.sql")
    with open(sql_path, "w", encoding="utf-8") as handle:
        handle.write(to_sql(manifest))

    characters = [c for a in manifest["accounts"] for c in a["characters"]]
    hunting = sum(1 for c in characters if c["place"].startswith("hunt:"))
    print(f"{len(manifest['accounts'])} accounts, {len(characters)} characters ({hunting} in hunting areas), "
          f"levels {min(c['level'] for c in characters)}-{max(c['level'] for c in characters)} -> {sql_path}")
    if config["max_online"] < 1:
        print("warning: maxPlayersOnlinePerAccount < 1 in config.lua")

    if not args.apply:
        print("dry run: nothing changed (use --apply)")
        return 0
    if port_in_use(config["game_port"]) and not args.force:
        print(f"refused: something answers on the game port {config['game_port']}; stop the server first (or --force)")
        return 1
    env = dict(os.environ, MYSQL_PWD=config["password"])
    with open(sql_path, "rb") as handle:
        done = subprocess.run(["docker", "exec", "-i", "-e", "MYSQL_PWD", args.container, "mysql", f"-u{config['user']}", config["database"]],
                              stdin=handle, env=env, capture_output=True)
    if done.returncode != 0:
        print(done.stderr.decode(errors="replace")[-2000:])
        return done.returncode
    manifest_path = os.path.join(out_dir, "characters.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, ensure_ascii=False)
    os.chmod(manifest_path, 0o600)
    print(f"applied to database {config['database']}; logins in {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
